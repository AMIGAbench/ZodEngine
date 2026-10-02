/*
 * music_probe -- kann AHI die Spielmusik als MIDI-Stimmen abspielen?
 *
 * Diese Sonde beantwortet die drei Fragen, die KEIN Host- und kein
 * Emulatorlauf beantworten kann, und sie beantwortet sie VOR dem Umbau der
 * Engine:
 *
 *   1. Wie viele Kanaele und Klangplaetze gibt der AHI-Treiber wirklich her?
 *      Die Engine oeffnet heute 16 Kanaele und 320 Klangplaetze; fuer Musik
 *      braucht es rund 24 Stimmen und ueber 500 Plaetze.
 *   2. Laeuft AHIA_PlayerFunc? Die Ereignisse liegen im 8,3-ms-Raster, ein
 *      Bild dauert 12-20 ms. Einmal je Bild abzuspielen hiesse bis zu 20 ms
 *      Zittern -- bei einem Schlagzeug mit 59 ms Abstand hoerbar.
 *   3. Was kostet das an Rechenzeit?
 *
 * Aufruf:
 *   music_probe <bank> <stueck.zmu> [sekunden]
 *
 * Meldet [OK] music, wenn alle drei Fragen beantwortet sind, sonst [FAIL].
 *
 * ZWEI REGELN, DIE HIER GELTEN UND DIE DAS PROGRAMM BESTIMMEN:
 *
 * - Im Hook laeuft ALLES im Interruptkontext. Kein dbg_printf, kein Open,
 *   kein AllocMem, keine Fliesskommarechnung. Gezaehlt wird in Feldern,
 *   ausgegeben wird NACH dem Lauf. Dieselbe Regel wie zwischen LockBitMap
 *   und UnLockBitMap -- dort ist sie mich
 *   schon einmal einen schwarzen Bildschirm gekostet.
 * - Die Rechenlast wird als Differenz zweier Zaehlschleifen gemessen, nicht
 *   mit einer Uhr im Hook. Eine Uhr im Interrupt waere teurer als das
 *   Gemessene -- dieselbe Ueberlegung wie bei ZOD_EFFSTUFE.
 */
#include <exec/types.h>
#include <exec/memory.h>
#include <exec/interrupts.h>
#include <devices/ahi.h>
#include <devices/timer.h>
#include <dos/dos.h>
#include <utility/hooks.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/ahi.h>
#include <proto/timer.h>

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "../../platform/amiga/debug.h"

extern "C" ULONG HookEntry(void);      /* aus libamiga: (a0,a2,a1) -> C-Reihenfolge */

struct Library *AHIBase = 0;
struct Device  *TimerBase = 0;      /* proto/timer.h deklariert es nur */

/* ------------------------------------------------------------------ Groessen */
#define STIMMEN      32          /* AHI-Kanaele, die wir anfordern            */
#define KLANGPLAETZE 600         /* AHI-Klangplaetze, die wir anfordern       */
#define INSTRUMENTE  184         /* Plaetze in der Bank-Versatztabelle        */
/* 184, nicht 182: 128 Melodie + 7 Fuellnullen + Perkussion fuer die GM-Noten
 * 35..83, also Index = Note + 100. Die beiden obersten (Shaker 82, Jingle
 * Bell 83) sind in beiden vorliegenden Baenken LEER -- der Platz ist da, die
 * Probe nicht. Ein erster Stand stand auf 182 (aus der Quelldatei der ersten
 * Bank abgezaehlt) und hat die Bank deshalb rundheraus abgelehnt. */
#define TAKT_HZ      120

/* ------------------------------------------------------------------ Bank */
struct Instrument
{
	UBYTE *proben;        /* zeigt in den Bankpuffer, nichts eigenes */
	ULONG  laenge;
	ULONG  rate;
	UWORD  schleife;      /* 0 = keine Schleife, Probe laeuft einmal */
	UWORD  grundton;
	WORD   platz_ganz;    /* AHI-Klangplatz, -1 = nicht angemeldet   */
	WORD   platz_schleife;
};

static Instrument instr[INSTRUMENTE];

/* ------------------------------------------------------------------ Stueck */
static UBYTE *zmu = 0;
static ULONG  zmu_len = 0;
static ULONG  zmu_ereignisse = 0;
static ULONG  zmu_ticks = 0;

/* ------------------------------------------------------------------ Stimmen */
struct Stimme
{
	UBYTE benutzt;
	UBYTE kanal;          /* MIDI-Kanal   */
	UBYTE note;
	UBYTE gruppe;         /* Abwuergegruppe bei Schlagzeug */
	UBYTE schleift;
	ULONG frei_ab;        /* Tick, ab dem der Platz sicher frei ist */
	ULONG begonnen;
};

static Stimme stimme[STIMMEN];
static int stimmen_da = 0;

/* ------------------------------------------------------------- Zaehlwerte */
static volatile ULONG z_hook = 0;        /* Aufrufe des Player-Hooks        */
static volatile ULONG z_ereignis = 0;
static volatile ULONG z_note_an = 0;
static volatile ULONG z_verdraengt = 0;
static volatile ULONG z_kein_instrument = 0;
static volatile ULONG z_spitze = 0;
static volatile ULONG z_tick = 0;
static volatile ULONG z_fertig = 0;
static volatile int   musik_an = 1;   /* 0 = Hook laeuft, spielt aber nichts */

/* ------------------------------------------------------------- Abspielstand */
static UBYTE *lauf = 0;          /* Leseposition im Ereignisstrom */
static UBYTE *lauf_ende = 0;
static ULONG  naechste_tick = 0;
static UBYTE  kanal_prog[16];
static UBYTE  kanal_vol[16];

static struct AHIAudioCtrl *ctrl = 0;
static struct Hook spieler_hook;

/* Halbtonfaktoren in 16.16, Index 0 = -64 Halbtoene. EINMAL beim Start
 * gerechnet -- im Interrupt gibt es keine Fliesskommarechnung. */
static ULONG faktor[128];

/* ------------------------------------------------------------------ Datei */
static UBYTE *datei_lesen(const char *name, ULONG *laenge)
{
	BPTR f = Open((CONST_STRPTR)name, MODE_OLDFILE);
	if(!f) return 0;

	Seek(f, 0, OFFSET_END);
	LONG n = Seek(f, 0, OFFSET_BEGINNING);
	if(n < 0) { Close(f); return 0; }

	/* Seek liefert die ALTE Position -- das ist hier die Dateilaenge. */
	ULONG len = (ULONG)n;
	UBYTE *p = (UBYTE *)AllocVec(len, MEMF_ANY);

	if(!p) { Close(f); return 0; }

	if(Read(f, p, len) != (LONG)len) { FreeVec(p); Close(f); return 0; }

	Close(f);
	*laenge = len;

	return p;
}

/* ------------------------------------------------------------- Ausgabe
 * Auf der Zielhardware haengt kein Nullmodem. Der Bericht geht deshalb in
 * die Shell UND in eine Datei; der serielle Kanal bleibt zusaetzlich
 * bedient, damit die Emulatorlaeufe weiter automatisch auswertbar sind.
 *
 * Formatiert wird mit vsnprintf, NICHT mit sprintf: sprintf kommt auf dieser
 * Werkzeugkette aus libamiga.a und liest %d als 16 Bit.
 * Die fertige Zeichenkette geht dann als reines %s an den seriellen Kanal.
 */
static BPTR bericht_datei = 0;

static void melde(const char *fmt, ...)
{
	char zeile[256];
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(zeile, sizeof(zeile), fmt, ap);
	va_end(ap);

	dbg_printf("%s", zeile);

	BPTR aus = Output();
	if(aus) Write(aus, zeile, (LONG)strlen(zeile));

	if(bericht_datei) Write(bericht_datei, zeile, (LONG)strlen(zeile));
}

static ULONG be32(const UBYTE *p)
{
	return ((ULONG)p[0] << 24) | ((ULONG)p[1] << 16) | ((ULONG)p[2] << 8) | p[3];
}

static UWORD be16(const UBYTE *p)
{
	return (UWORD)(((UWORD)p[0] << 8) | p[1]);
}

/* ------------------------------------------------------------------ Bank */
static int bank_lesen(UBYTE *b, ULONG len)
{
	if(len < 8) return 0;

	ULONG erste = be32(b);
	ULONG n = erste / 4;

	if(n < 128 || n > INSTRUMENTE)
	{
		melde("Bank: Tabelle mit %ld Eintraegen -- erwartet 128..%ld\n",
		           (long)n, (long)INSTRUMENTE);
		return 0;
	}

	for(ULONG i = 0; i < n; i++)
	{
		ULONG v = be32(b + 4 * i);

		instr[i].platz_ganz = -1;
		instr[i].platz_schleife = -1;

		if(!v || v + 14 > len) { instr[i].proben = 0; continue; }

		instr[i].rate     = be32(b + v);
		instr[i].schleife = be16(b + v + 4);
		instr[i].laenge   = be16(b + v + 8);
		instr[i].grundton = be16(b + v + 12);
		instr[i].proben   = b + v + 14;

		if(v + 14 + instr[i].laenge > len) instr[i].proben = 0;
	}

	return 1;
}

/* ------------------------------------------------------------ Klangplaetze */
static int klaenge_anmelden(void)
{
	struct AHISampleInfo info;
	int platz = 0;
	int angemeldet = 0;

	for(int i = 0; i < INSTRUMENTE; i++)
	{
		if(!instr[i].proben || !instr[i].laenge) continue;

		info.ahisi_Type    = AHIST_M8S;
		info.ahisi_Address = instr[i].proben;
		info.ahisi_Length  = instr[i].laenge;

		if(AHI_LoadSound((UWORD)platz, AHIST_SAMPLE, &info, ctrl))
		{
			melde("AHI: Klangplatz %ld abgelehnt (Instrument %ld)\n",
			           (long)platz, (long)i);
			return angemeldet;
		}

		instr[i].platz_ganz = (WORD)platz++;
		angemeldet++;

		if(instr[i].schleife > 0 && instr[i].schleife < instr[i].laenge)
		{
			info.ahisi_Address = instr[i].proben + instr[i].schleife;
			info.ahisi_Length  = instr[i].laenge - instr[i].schleife;

			if(AHI_LoadSound((UWORD)platz, AHIST_SAMPLE, &info, ctrl))
			{
				melde("AHI: Schleifenplatz %ld abgelehnt\n", (long)platz);
				return angemeldet;
			}

			instr[i].platz_schleife = (WORD)platz++;
			angemeldet++;
		}
	}

	return angemeldet;
}

/* -------------------------------------------------------------- Stimmwahl */
static UBYTE gruppe_von(UBYTE note)
{
	/* HiHats teilen sich eine Stimme, wie am echten Schlagzeug. */
	if(note == 42 || note == 44 || note == 46) return 200;
	return note;
}

static int stimme_waehlen(UBYTE kanal, UBYTE note, ULONG tick)
{
	UBYTE gr = (kanal == 9) ? gruppe_von(note) : note;
	int aeltester = 0;
	ULONG alter = 0xFFFFFFFFUL;

	/* 1. dieselbe Tonhoehe bzw. dasselbe Schlaginstrument -> abwuergen */
	for(int i = 0; i < stimmen_da; i++)
		if(stimme[i].benutzt && stimme[i].kanal == kanal && stimme[i].gruppe == gr)
			return i;

	/* 2. eine, die sicher ausgeklungen ist */
	for(int i = 0; i < stimmen_da; i++)
	{
		if(!stimme[i].benutzt) return i;
		if(!stimme[i].schleift && tick >= stimme[i].frei_ab) return i;
	}

	/* 3. die aelteste */
	for(int i = 0; i < stimmen_da; i++)
		if(stimme[i].begonnen < alter) { alter = stimme[i].begonnen; aeltester = i; }

	z_verdraengt++;

	return aeltester;
}

static void stimme_stoppen(int i)
{
	struct TagItem t[] = {
		{ AHIP_BeginChannel, (ULONG)i },
		{ AHIP_Sound,        (ULONG)AHI_NOSOUND },
		{ AHIP_EndChannel,   0 },
		{ TAG_DONE,          0 }
	};

	AHI_PlayA(ctrl, t);
	stimme[i].benutzt = 0;
}

static void note_an(UBYTE kanal, UBYTE note, UBYTE anschlag, ULONG tick)
{
	int idx = (kanal == 9) ? (int)note + 100 : (int)kanal_prog[kanal];

	if(idx < 0 || idx >= INSTRUMENTE || !instr[idx].proben
	   || instr[idx].platz_ganz < 0)
	{
		z_kein_instrument++;
		return;
	}

	Instrument *in = &instr[idx];

	/* Schlagzeug spielt ungestimmt, Melodie nach der Tonhoehe. */
	int halbton = (kanal == 9) ? 0 : ((int)note - (int)in->grundton);

	if(halbton < -64) halbton = -64;
	if(halbton >  63) halbton =  63;

	ULONG freq = (ULONG)(((unsigned long long)in->rate * faktor[halbton + 64]) >> 16);

	if(freq < 100) freq = 100;

	ULONG v = (ULONG)anschlag * kanal_vol[kanal];      /* 0..16129 */
	ULONG vol = (v * 0x10000UL) / (127UL * 127UL);

	if(vol > 0x10000UL) vol = 0x10000UL;

	int i = stimme_waehlen(kanal, note, tick);
	int schleift = (in->platz_schleife >= 0);

	struct TagItem t[] = {
		{ AHIP_BeginChannel, (ULONG)i },
		{ AHIP_Freq,         freq },
		{ AHIP_Vol,          vol },
		{ AHIP_Pan,          0x8000 },
		{ AHIP_Sound,        (ULONG)in->platz_ganz },
		{ AHIP_LoopSound,    (ULONG)(schleift ? in->platz_schleife : AHI_NOSOUND) },
		{ AHIP_EndChannel,   0 },
		{ TAG_DONE,          0 }
	};

	AHI_PlayA(ctrl, t);

	stimme[i].benutzt  = 1;
	stimme[i].kanal    = kanal;
	stimme[i].note     = note;
	stimme[i].gruppe   = (kanal == 9) ? gruppe_von(note) : note;
	stimme[i].schleift = (UBYTE)schleift;
	stimme[i].begonnen = tick;
	/* Wann ist die Probe durch? Ticks = Laenge * 120 / Abspielrate. */
	stimme[i].frei_ab  = tick + (in->laenge * TAKT_HZ) / (freq ? freq : 1) + 1;

	z_note_an++;

	ULONG aktiv = 0;

	for(int k = 0; k < stimmen_da; k++)
		if(stimme[k].benutzt && (stimme[k].schleift || tick < stimme[k].frei_ab))
			aktiv++;

	if(aktiv > z_spitze) z_spitze = aktiv;
}

static void note_aus(UBYTE kanal, UBYTE note)
{
	UBYTE gr = (kanal == 9) ? gruppe_von(note) : note;

	for(int i = 0; i < stimmen_da; i++)
	{
		if(!stimme[i].benutzt || stimme[i].kanal != kanal) continue;
		if(stimme[i].gruppe != gr) continue;

		/* NUR Schlagzeug klingt aus -- das Notenende gilt dort nicht.
		 * Alles andere endet mit der Note, auch einmalige Proben.
		 *
		 * Ein erster Stand stoppte nur geschleifte Stimmen. Folge im
		 * Emulatorlauf: jeder Klavieranschlag belegte seine Stimme fuer
		 * die volle Probenlaenge (bis 3,3 s), die Spitze lag bei 32 von
		 * 32 Stimmen und es gab 54 Verdraengungen. Genau derselbe Fehler
		 * war vorher schon in der Hoerprobe auf dem Host. */
		if(kanal != 9) stimme_stoppen(i);

		return;
	}
}

/* ------------------------------------------------------------------ Hook */
/* Laeuft im INTERRUPT. Nur rechnen und AHI_PlayA -- sonst nichts. */
extern "C" ULONG spieler(struct Hook *, struct AHIAudioCtrl *, APTR)
{
	z_hook++;

	if(!musik_an || z_fertig) return 0;

	ULONG tick = z_tick++;

	while(lauf < lauf_ende && naechste_tick <= tick)
	{
		UBYTE art = *lauf++;

		z_ereignis++;

		switch(art)
		{
		case 0:                                   /* Note an   */
			if(lauf + 3 > lauf_ende) { z_fertig = 1; return 0; }
			note_an(lauf[0], lauf[1], lauf[2], tick);
			lauf += 3;
			break;

		case 1:                                   /* Note aus  */
			if(lauf + 2 > lauf_ende) { z_fertig = 1; return 0; }
			note_aus(lauf[0], lauf[1]);
			lauf += 2;
			break;

		case 2:                                   /* Programm  */
			if(lauf + 2 > lauf_ende) { z_fertig = 1; return 0; }
			kanal_prog[lauf[0] & 15] = lauf[1];
			lauf += 2;
			break;

		case 3:                                   /* CC7       */
			if(lauf + 2 > lauf_ende) { z_fertig = 1; return 0; }
			kanal_vol[lauf[0] & 15] = lauf[1];
			lauf += 2;
			break;

		case 5:                                   /* nur warten */
			break;

		default:                                  /* 4 = Ende   */
			z_fertig = 1;
			return 0;
		}

		/* Wartezeit bis zum naechsten Ereignis */
		if(lauf >= lauf_ende) { z_fertig = 1; return 0; }

		ULONG d = *lauf++;

		if(d == 255)
		{
			if(lauf + 2 > lauf_ende) { z_fertig = 1; return 0; }
			d = be16(lauf);
			lauf += 2;
		}

		naechste_tick += d;
	}

	return 0;
}

/* ------------------------------------------------------- Rechenlast messen */
/* Eine Zaehlschleife fester Dauer. Der Unterschied zwischen "Musik aus" und
 * "Musik an" ist der Preis des Abspielers -- eine Uhr im Interrupt waere
 * teurer als das Gemessene. */
static ULONG zaehlen(ULONG ticks_50hz)
{
	struct timeval a, b;
	ULONG n = 0;
	static volatile ULONG senke;

	GetSysTime(&a);

	for(;;)
	{
		/* Echte Rechenarbeit, die der Uebersetzer nicht wegwerfen kann.
		 * Ein erster Stand zaehlte n += 2000 je Uhrabfrage -- gemessen
		 * wurde damit fast nur GetSysTime (auf der V1200 9,9 us je
		 * Aufruf), und der Zaehler lief nach wenigen Sekunden ueber 2^32.
		 * Der Lauf meldete "Leerlauf ohne Audio: -210847184". */
		ULONG w = n + 1;

		for(int k = 0; k < 256; k++) w = w * 1103515245UL + 12345UL;

		senke = w;
		n++;

		/* Die Uhr nur jeden 32. Durchgang -- sonst misst man sie selbst. */
		if((n & 31) == 0)
		{
			GetSysTime(&b);

			ULONG us = (ULONG)((b.tv_secs - a.tv_secs) * 1000000
			                   + (b.tv_micro - a.tv_micro));

			if(us >= ticks_50hz * 20000UL) break;
		}
	}

	return n;
}

/* ------------------------------------------------------------------ Main */
static struct MsgPort *ahi_port = 0;
static struct AHIRequest *ahi_req = 0;
static struct MsgPort *timer_port = 0;
static struct timerequest *timer_req = 0;

static struct AHIAudioCtrl *auf(ULONG id, ULONG chans, ULONG sounds,
                                struct Hook *hook, ULONG player_freq,
                                ULONG mixfreq)
{
	struct TagItem t[] = {
		{ AHIA_AudioID,    id },
		{ AHIA_MixFreq,    mixfreq },
		{ AHIA_Channels,   chans },
		{ AHIA_Sounds,     sounds },
		{ AHIA_PlayerFunc, (ULONG)hook },
		{ AHIA_PlayerFreq, player_freq },
		{ AHIA_MinPlayerFreq, player_freq },
		{ AHIA_MaxPlayerFreq, player_freq },
		{ TAG_DONE,        0 }
	};

	if(!hook) { t[4].ti_Tag = TAG_IGNORE; t[5].ti_Tag = TAG_IGNORE;
	            t[6].ti_Tag = TAG_IGNORE; t[7].ti_Tag = TAG_IGNORE; }

	return AHI_AllocAudioA(t);
}

static void aufraeumen(void)
{
	if(bericht_datei) { Close(bericht_datei); bericht_datei = 0; }

	if(ctrl)
	{
		struct TagItem stop[] = { { AHIC_Play, FALSE }, { TAG_DONE, 0 } };
		AHI_ControlAudioA(ctrl, stop);
		AHI_FreeAudio(ctrl);
		ctrl = 0;
	}

	if(ahi_req)
	{
		CloseDevice((struct IORequest *)ahi_req);
		DeleteIORequest((struct IORequest *)ahi_req);
		ahi_req = 0;
	}

	if(ahi_port) { DeleteMsgPort(ahi_port); ahi_port = 0; }

	if(timer_req)
	{
		CloseDevice((struct IORequest *)timer_req);
		DeleteIORequest((struct IORequest *)timer_req);
		timer_req = 0;
	}

	if(timer_port) { DeleteMsgPort(timer_port); timer_port = 0; }
}

/* ------------------------------------------------------- Stufenmessung
 * Die V1200 meldete 638 Promille Rechenlast bei FUENF gleichzeitigen
 * Stimmen und 100 Noten in 30 s. Das koennen die Noten nicht sein. Um
 * Mischer, Kanalzahl, Mischfrequenz und Hook auseinanderzuhalten, laufen
 * alle Stufen in EINEM Lauf -- getrennte Laeufe waeren nicht vergleichbar
 * (dieselbe Ueberlegung wie bei ZOD_SWEEP und ZOD_EFFSTUFE).
 */
struct Ergebnis
{
	ULONG zaehl;      /* Zaehlschleife waehrend der Stufe */
	ULONG hooks;
	ULONG spitze;
	ULONG noten;
	ULONG mixfreq;    /* was AHI wirklich mischt          */
	int   ok;
};

static void strom_zuruecksetzen(void)
{
	lauf = zmu + 16;
	lauf_ende = zmu + zmu_len;

	ULONG d = *lauf++;
	if(d == 255) { d = be16(lauf); lauf += 2; }
	naechste_tick = d;

	z_tick = 0; z_fertig = 0;
	z_hook = 0; z_ereignis = 0; z_note_an = 0;
	z_verdraengt = 0; z_kein_instrument = 0; z_spitze = 0;

	memset(stimme, 0, sizeof(stimme));

	for(int i = 0; i < 16; i++) { kanal_prog[i] = 0; kanal_vol[i] = 100; }
}

static int stufe(ULONG chans, ULONG wunschfreq, int mit_musik, int mit_hook,
                 ULONG sek, Ergebnis *e)
{
	e->ok = 0;

	ctrl = auf(AHI_DEFAULT_ID, chans, KLANGPLAETZE,
	           mit_hook ? &spieler_hook : 0,
	           mit_hook ? ((ULONG)TAKT_HZ << 16) : 0, wunschfreq);

	if(!ctrl) return 0;

	/* Was AHI wirklich mischt -- die gewuenschte Frequenz ist nur ein Wunsch */
	ULONG echt = 0;
	struct TagItem frage[] = { { AHIC_MixFreq_Query, (ULONG)&echt }, { TAG_DONE, 0 } };
	AHI_ControlAudioA(ctrl, frage);
	e->mixfreq = echt;

	stimmen_da = (int)(chans < STIMMEN ? chans : STIMMEN);

	musik_an = mit_musik;
	strom_zuruecksetzen();

	if(mit_musik && !klaenge_anmelden())
	{
		AHI_FreeAudio(ctrl); ctrl = 0;
		return 0;
	}

	struct TagItem los[] = { { AHIC_Play, TRUE }, { TAG_DONE, 0 } };

	if(AHI_ControlAudioA(ctrl, los))
	{
		AHI_FreeAudio(ctrl); ctrl = 0;
		return 0;
	}

	e->zaehl = zaehlen(sek * 50);

	struct TagItem stopp[] = { { AHIC_Play, FALSE }, { TAG_DONE, 0 } };
	AHI_ControlAudioA(ctrl, stopp);

	e->hooks  = z_hook;
	e->spitze = z_spitze;
	e->noten  = z_note_an;
	e->ok     = 1;

	AHI_FreeAudio(ctrl);
	ctrl = 0;

	/* Klangplaetze gehoeren dem AudioCtrl -- nach dem Freigeben sind alle
	 * Anmeldungen weg und muessen in der naechsten Stufe erneut erfolgen. */
	for(int i = 0; i < INSTRUMENTE; i++)
	{ instr[i].platz_ganz = -1; instr[i].platz_schleife = -1; }

	return 1;
}


int main(int argc, char **argv)
{
	dbg_boot();

	if(argc < 3)
	{
		dbg_fail("music Aufruf: music_probe <bank> <stueck.zmu> [sekunden]");
		return 1;
	}

	LONG sekunden = 20;

	if(argc > 3)
	{
		sekunden = 0;
		for(const char *p = argv[3]; *p >= '0' && *p <= '9'; p++)
			sekunden = sekunden * 10 + (*p - '0');
		if(sekunden < 1) sekunden = 20;
	}

	/* Bericht in eine Datei, weil an der Zielhardware kein Nullmodem
	 * haengt. Name als viertes Argument, sonst neben dem Programm. */
	const char *berichtname = (argc > 4) ? argv[4] : "music_probe.txt";

	bericht_datei = Open((CONST_STRPTR)berichtname, MODE_NEWFILE);

	melde("music_probe -- Bank %s, Stueck %s, %ld s\n",
	      argv[1], argv[2], (long)sekunden);

	if(!bericht_datei)
		melde("Hinweis: Berichtsdatei %s nicht anzulegen\n", berichtname);

	/* --- timer.device fuer die Lastmessung --- */
	timer_port = CreateMsgPort();
	timer_req = timer_port
	          ? (struct timerequest *)CreateIORequest(timer_port, sizeof(struct timerequest))
	          : 0;

	if(!timer_req || OpenDevice((CONST_STRPTR)TIMERNAME, UNIT_MICROHZ,
	                            (struct IORequest *)timer_req, 0))
	{
		dbg_fail("music timer.device nicht zu oeffnen");
		aufraeumen();
		return 1;
	}

	TimerBase = timer_req->tr_node.io_Device;

	/* --- Halbtontabelle, einmalig in Fliesskomma --- */
	for(int i = 0; i < 128; i++)
		faktor[i] = (ULONG)(pow(2.0, (double)(i - 64) / 12.0) * 65536.0 + 0.5);

	/* --- Bank --- */
	ULONG bank_len = 0;
	UBYTE *bank = datei_lesen(argv[1], &bank_len);

	if(!bank) { melde("Bank %s nicht lesbar\n", argv[1]);
	            dbg_fail("music Bank fehlt"); aufraeumen(); return 1; }

	if(!bank_lesen(bank, bank_len))
	{ dbg_fail("music Bank unbrauchbar"); FreeVec(bank); aufraeumen(); return 1; }

	int hat = 0;
	for(int i = 0; i < INSTRUMENTE; i++) if(instr[i].proben) hat++;

	melde("Bank: %ld Byte, %ld Instrumente\n", (long)bank_len, (long)hat);

	/* --- Stueck --- */
	zmu = datei_lesen(argv[2], &zmu_len);

	if(!zmu || zmu_len < 16 || zmu[0] != 'Z' || zmu[1] != 'M' || zmu[2] != 'U'
	   || zmu[3] != '1')
	{
		dbg_fail("music ZMU1 fehlt oder falsche Kennung");
		if(zmu) FreeVec(zmu);
		FreeVec(bank);
		aufraeumen();
		return 1;
	}

	UWORD takt = be16(zmu + 4);
	zmu_ereignisse = be32(zmu + 8);
	zmu_ticks = be32(zmu + 12);

	melde("Stueck: %ld Ereignisse, %ld Schritte = %ld s, Takt %ld Hz\n",
	           (long)zmu_ereignisse, (long)zmu_ticks,
	           (long)(takt ? zmu_ticks / takt : 0), (long)takt);

	if(takt != TAKT_HZ)
	{ dbg_fail("music unerwarteter Takt"); FreeVec(zmu); FreeVec(bank);
	  aufraeumen(); return 1; }

	/* Erste Wartezeit vor dem ersten Ereignis einlesen */
	lauf = zmu + 16;
	lauf_ende = zmu + zmu_len;

	{
		ULONG d = *lauf++;
		if(d == 255) { d = be16(lauf); lauf += 2; }
		naechste_tick = d;
	}

	for(int i = 0; i < 16; i++) { kanal_prog[i] = 0; kanal_vol[i] = 100; }

	/* --- AHI --- */
	ahi_port = CreateMsgPort();
	ahi_req = ahi_port
	        ? (struct AHIRequest *)CreateIORequest(ahi_port, sizeof(struct AHIRequest))
	        : 0;

	if(!ahi_req) { dbg_fail("music kein IORequest"); FreeVec(zmu); FreeVec(bank);
	               aufraeumen(); return 1; }

	ahi_req->ahir_Version = 4;

	if(OpenDevice((CONST_STRPTR)AHINAME, AHI_NO_UNIT, (struct IORequest *)ahi_req, 0))
	{
		dbg_fail("music ahi.device nicht zu oeffnen");
		FreeVec(zmu); FreeVec(bank); aufraeumen();
		return 1;
	}

	AHIBase = (struct Library *)ahi_req->ahir_Std.io_Device;

	spieler_hook.h_Entry    = (ULONG (*)())HookEntry;
	spieler_hook.h_SubEntry = (ULONG (*)())spieler;
	spieler_hook.h_Data     = 0;

	/* ---------------------------------------------------------- Stufen
	 * Sechs Aufsetzungen, alle in EINEM Lauf. Die Differenzen sagen,
	 * wo die Rechenzeit hingeht -- Kanalzahl, Mischfrequenz, Hook oder
	 * die Noten selbst.
	 */
	struct Aufsetzung { ULONG chans; ULONG freq; int musik; int hook; const char *name; };

	static const Aufsetzung plan[] = {
		{ 32, 28867, 1, 1, "32 Kanaele, 28867 Hz, Musik" },
		{ 32, 11025, 1, 1, "32 Kanaele, 11025 Hz, Musik" },
		{ 16, 28867, 1, 1, "16 Kanaele, 28867 Hz, Musik" },
		{  8, 28867, 1, 1, " 8 Kanaele, 28867 Hz, Musik" },
		{ 32, 28867, 0, 1, "32 Kanaele, 28867 Hz, Hook OHNE Noten" },
		{ 32, 28867, 0, 0, "32 Kanaele, 28867 Hz, weder Hook noch Noten" },
		/* KONTROLLE: noch einmal genau Stufe 1. Weichen beide stark
		 * voneinander ab, ist die Streuung groesser als jeder gesuchte
		 * Unterschied und die ganze Messreihe unbrauchbar -- dieselbe
		 * Regel wie bei den Host-Laeufen. */
		{ 32, 28867, 1, 1, "32 Kanaele, 28867 Hz, Musik (KONTROLLE)" }
	};

	const int stufen = (int)(sizeof(plan) / sizeof(plan[0]));

	/* Leerlauf OHNE jedes offene Audio -- die Bezugsgroesse. */
	ULONG leer = zaehlen(sekunden * 50);

	melde("Leerlauf ohne Audio: %ld (je Stufe %ld s)\n", (long)leer, (long)sekunden);

	Ergebnis erg[7];
	int gelungen = 0;

	for(int i = 0; i < stufen; i++)
	{
		if(!stufe(plan[i].chans, plan[i].freq, plan[i].musik, plan[i].hook,
		          (ULONG)sekunden, &erg[i]))
		{
			melde("%s: LIESS SICH NICHT OEFFNEN\n", plan[i].name);
			continue;
		}

		gelungen++;

		long promille = leer ? (long)(((long long)leer - (long long)erg[i].zaehl)
		                              * 1000 / (long long)leer)
		                     : 0;

		melde("%s: %ld Promille, mischt %ld Hz, Hook %ld Hz, "
		      "Spitze %ld Stimmen, %ld Noten\n",
		      plan[i].name, promille, (long)erg[i].mixfreq,
		      (long)(sekunden ? erg[i].hooks / sekunden : 0),
		      (long)erg[i].spitze, (long)erg[i].noten);
	}

	if(!gelungen)
	{
		dbg_fail("music keine einzige Stufe lief");
		FreeVec(zmu); FreeVec(bank); aufraeumen();
		return 1;
	}

	/* Zuerst: taugt die Messreihe ueberhaupt? */
	if(erg[0].ok && erg[6].ok && leer)
	{
		long p1 = (long)(((long long)leer - erg[0].zaehl) * 1000 / leer);
		long p7 = (long)(((long long)leer - erg[6].zaehl) * 1000 / leer);
		long d  = p1 > p7 ? p1 - p7 : p7 - p1;

		melde("KONTROLLE: Stufe 1 %ld gegen Wiederholung %ld Promille,"
		      " Streuung %ld\n", p1, p7, d);

		if(d > 50)
			melde("WARNUNG: Streuung groesser als 50 Promille --"
			      " Unterschiede unter dieser Groesse sind NICHT deutbar.\n");
	}

	/* Die beiden Differenzen, auf die es ankommt. */
	if(erg[0].ok && erg[4].ok && erg[5].ok && leer)
	{
		long p_voll = (long)(((long long)leer - erg[0].zaehl) * 1000 / leer);
		long p_leer = (long)(((long long)leer - erg[4].zaehl) * 1000 / leer);
		long p_ohne = (long)(((long long)leer - erg[5].zaehl) * 1000 / leer);

		melde("AUFTEILUNG: AHI allein %ld, + Hook %ld, + Noten %ld Promille\n",
		      p_ohne, p_leer - p_ohne, p_voll - p_leer);
	}

	int gut = 1;

	if(!erg[0].ok)
	{
		melde("FEHLER: die Grundstufe lief nicht\n");
		gut = 0;
	}
	else
	{
		ULONG soll = (ULONG)sekunden * TAKT_HZ;
		ULONG abw = erg[0].hooks > soll ? erg[0].hooks - soll : soll - erg[0].hooks;

		if(erg[0].hooks == 0)
		{ melde("FEHLER: der Hook wurde NIE gerufen\n"); gut = 0; }
		else if(abw * 10 > soll)
		{ melde("FEHLER: Hookrate weicht um mehr als 10 %% ab\n"); gut = 0; }

		if(erg[0].noten == 0)
		{ melde("FEHLER: keine einzige Note gespielt\n"); gut = 0; }
	}

	FreeVec(zmu);
	FreeVec(bank);
	aufraeumen();

	/* Die Marke geht NUR ueber dbg_ok/dbg_fail auf den seriellen Kanal --
	 * melde() wuerde sie dort ein zweites Mal ausgeben. */
	{
		const char *marke = gut ? "[OK] music\n" : "[FAIL] music siehe Meldungen\n";
		BPTR aus = Output();

		if(aus) Write(aus, (APTR)marke, (LONG)strlen(marke));
		if(bericht_datei) Write(bericht_datei, (APTR)marke, (LONG)strlen(marke));
	}

	if(gut) dbg_ok("music");
	else    dbg_fail("music siehe Meldungen");

	if(bericht_datei) { Close(bericht_datei); bericht_datei = 0; }

	return gut ? 0 : 1;
}
