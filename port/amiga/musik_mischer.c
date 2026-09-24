/*
 * Musikmischer -- siehe musik_mischer.h fuer das Warum.
 *
 * Aufbau:
 *   mischer_fuellen() teilt den angeforderten Block in SCHRITTE zu je
 *   1/120 s. Vor jedem Schritt werden die faelligen Ereignisse des
 *   ZMU1-Stroms abgearbeitet, danach wird der Abschnitt gemischt. Damit
 *   ist die Zeit abtastgenau und haengt an keinem Interrupt.
 *
 *   Die Rahmen je Schritt sind selten ganzzahlig (22050/120 = 183,75).
 *   Deshalb laeuft ein 16.16-Rest mit, und der Fehler summiert sich nicht
 *   auf -- sonst liefe das Stueck ueber sechs Minuten merklich davon.
 */
#include "musik_mischer.h"

#include <string.h>

/* --------------------------------------------------------------- Bank */
#define INSTRUMENTE 184        /* 128 Melodie + 7 Fuellnullen + Perkussion */

typedef struct
{
	const BYTE *proben;
	ULONG  laenge;
	ULONG  rate;
	ULONG  schleife;           /* 0 = einmalig */
	ULONG  grundton;
} Instrument;

static Instrument instr[INSTRUMENTE];
static int instr_da = 0;

/* --------------------------------------------------------------- Stimmen */
typedef struct
{
	const BYTE *proben;
	ULONG pos;                 /* 16.16 in die Probe            */
	ULONG schritt;             /* 16.16 Vorschub je Rahmen      */
	ULONG ende;                /* 16.16 Probenende              */
	ULONG schleife;            /* 16.16 Schleifenanfang, 0 = keine */
	WORD  vol_l, vol_r;        /* 0..256                        */
	UBYTE aktiv;
	UBYTE kanal;
	UBYTE gruppe;              /* Tonhoehe bzw. Schlaggruppe    */
	UBYTE einmalig;
	UBYTE ausklang;            /* 1 = Lautstaerke laeuft aus */
	ULONG begonnen;            /* Schritt, fuer die Verdraengung */
} Stimme;

static Stimme stimme[MISCHER_MAX_STIMMEN];
static int stimmen_da = 0;

/* Die oberen Plaetze gehoeren den Effekten. 0 = keine, dann mischt dieser
 * Mischer nur Musik (so laeuft es in den Sonden). */
static int effekt_stimmen = 0;

static int musik_plaetze(void)
{
	int n = stimmen_da - effekt_stimmen;

	return n < 1 ? 1 : n;
}

/* --------------------------------------------------------------- Stueck */
static const UBYTE *lauf, *lauf_ende;
static const UBYTE *lauf_anfang = 0;
static ULONG lauf_laenge = 0;
static ULONG naechster_schritt;
static ULONG schritt_nr;
static int   zu_ende;

static UBYTE kanal_prog[16];
static UBYTE kanal_vol[16];
static UBYTE kanal_pan[16];

/* --------------------------------------------------------------- Takt */
static ULONG mischrate = 22050;
static ULONG rahmen_je_schritt;    /* 16.16 */
static ULONG rahmen_rest;          /* 16.16, laeuft mit */

/* --------------------------------------------------------------- Zahlen */
static ULONG z_noten, z_spitze, z_verdraengt, z_ohne_instrument;
static int   vorlauf = 0;      /* 1 = spulen, keine Noten anschlagen */
static ULONG z_geklemmt;

/* Halbtonfaktoren 16.16, Index 0 = -64 Halbtoene. Fest eingebaut, damit
 * der Mischer ohne Fliesskomma und ohne Anlaufrechnung auskommt.
 * Erzeugt mit: round(2**((i-64)/12) * 65536) */
static const ULONG faktor[128] = {
	      1625,       1722,       1825,       1933,       2048,       2170,       2299,       2435,
	      2580,       2734,       2896,       3069,       3251,       3444,       3649,       3866,
	      4096,       4340,       4598,       4871,       5161,       5468,       5793,       6137,
	      6502,       6889,       7298,       7732,       8192,       8679,       9195,       9742,
	     10321,      10935,      11585,      12274,      13004,      13777,      14596,      15464,
	     16384,      17358,      18390,      19484,      20643,      21870,      23170,      24548,
	     26008,      27554,      29193,      30929,      32768,      34716,      36781,      38968,
	     41285,      43740,      46341,      49097,      52016,      55109,      58386,      61858,
	     65536,      69433,      73562,      77936,      82570,      87480,      92682,      98193,
	    104032,     110218,     116772,     123715,     131072,     138866,     147123,     155872,
	    165140,     174960,     185364,     196386,     208064,     220436,     233544,     247431,
	    262144,     277732,     294247,     311744,     330281,     349920,     370728,     392772,
	    416128,     440872,     467088,     494862,     524288,     555464,     588493,     623487,
	    660561,     699841,     741455,     785544,     832255,     881744,     934175,     989724,
	   1048576,    1110928,    1176987,    1246974,    1321123,    1399681,    1482910,    1571089,
	   1664511,    1763488,    1868350,    1979448,    2097152,    2221855,    2353974,    2493948
};

static ULONG be32(const UBYTE *p)
{
	return ((ULONG)p[0] << 24) | ((ULONG)p[1] << 16) | ((ULONG)p[2] << 8) | p[3];
}

static UWORD be16(const UBYTE *p)
{
	return (UWORD)(((UWORD)p[0] << 8) | p[1]);
}

/* ------------------------------------------------------------------ Bank */
int mischer_init(const UBYTE *bank, ULONG banklen, ULONG rate, int stimmen)
{
	memset(instr, 0, sizeof(instr));
	memset(stimme, 0, sizeof(stimme));

	instr_da = 0;

	mischrate = rate ? rate : 22050;

	if(stimmen < 1) stimmen = 1;
	if(stimmen > MISCHER_MAX_STIMMEN) stimmen = MISCHER_MAX_STIMMEN;

	stimmen_da = stimmen;
	rahmen_je_schritt = (ULONG)(((unsigned long long)mischrate << 16)
	                            / MISCHER_TAKT_HZ);

	/* OHNE BANK ist das kein Fehler: dann mischt dieser Mischer nur
	 * Klangeffekte. Genau so laeuft es, wenn musik/MIDIBANK fehlt -- das
	 * Spiel soll dann seine Klaenge behalten, nicht verstummen. */
	if(!bank || banklen < 8) return 1;

	ULONG n = be32(bank) / 4;

	if(n < 128 || n > INSTRUMENTE) return 1;

	for(ULONG i = 0; i < n; i++)
	{
		ULONG v = be32(bank + 4 * i);

		if(!v || v + 14 > banklen) continue;

		ULONG laenge = be16(bank + v + 8);

		if(!laenge || v + 14 + laenge > banklen) continue;

		instr[i].rate     = be32(bank + v);
		instr[i].schleife = be16(bank + v + 4);
		instr[i].laenge   = laenge;
		instr[i].grundton = be16(bank + v + 12);
		instr[i].proben   = (const BYTE *)(bank + v + 14);

		if(!instr[i].rate) instr[i].rate = 11025;

		instr_da++;
	}

	return instr_da;
}

/* ------------------------------------------------------------------ Stueck */
static void naechste_wartezeit(void)
{
	if(lauf >= lauf_ende) { zu_ende = 1; return; }

	ULONG d = *lauf++;

	if(d == 255)
	{
		if(lauf + 2 > lauf_ende) { zu_ende = 1; return; }
		d = be16(lauf);
		lauf += 2;
	}

	naechster_schritt += d;
}

int mischer_stueck(const UBYTE *zmu, ULONG len)
{
	/* NUR die Musikstimmen zuruecksetzen. Ein Kartenwechsel darf die
	 * gerade laufenden Klangeffekte nicht abschneiden. */
	memset(stimme, 0, (size_t)musik_plaetze() * sizeof(Stimme));

	z_noten = z_spitze = z_verdraengt = z_ohne_instrument = 0;
	z_geklemmt = 0;
	schritt_nr = 0;
	rahmen_rest = 0;
	zu_ende = 0;

	if(!zmu || len < 16) { zu_ende = 1; return 0; }

	if(zmu[0] != 'Z' || zmu[1] != 'M' || zmu[2] != 'U' || zmu[3] != '1')
	{ zu_ende = 1; return 0; }

	if(be16(zmu + 4) != MISCHER_TAKT_HZ) { zu_ende = 1; return 0; }

	lauf = zmu + 16;
	lauf_ende = zmu + len;
	lauf_anfang = zmu;
	lauf_laenge = len;
	naechster_schritt = 0;

	for(int i = 0; i < 16; i++)
	{
		kanal_prog[i] = 0;
		kanal_vol[i] = 100;
		kanal_pan[i] = 64;          /* Mitte */
	}

	naechste_wartezeit();

	return 1;
}

/* -------------------------------------------------------------- Stimmwahl */
static UBYTE gruppe_von(UBYTE kanal, UBYTE note)
{
	/* HiHats teilen sich eine Stimme, wie am echten Schlagzeug. */
	if(kanal == 9 && (note == 42 || note == 44 || note == 46)) return 200;

	return note;
}

static int stimme_waehlen(UBYTE kanal, UBYTE gruppe)
{
	int aeltester = 0;
	int grenze = musik_plaetze();
	ULONG alter = 0xFFFFFFFFUL;

	for(int i = 0; i < grenze; i++)
		if(stimme[i].aktiv && stimme[i].kanal == kanal
		   && stimme[i].gruppe == gruppe)
			return i;

	for(int i = 0; i < grenze; i++)
		if(!stimme[i].aktiv) return i;

	for(int i = 0; i < grenze; i++)
		if(stimme[i].begonnen < alter)
		{ alter = stimme[i].begonnen; aeltester = i; }

	z_verdraengt++;

	return aeltester;
}

/* Ersatz fuer Schlaginstrumente, die die Bank nicht hat.
 *
 * Belegt, nicht vermutet: AOPTIONS schlaegt 896-mal die Note 82 (Shaker)
 * an -- 13,5 % aller Noten des Menuestuecks. Beide vorliegenden Baenke
 * enden bei Note 81; die Plaetze 82 und 83 sind in der Tabelle vorhanden
 * und leer. Ohne Ersatz waere jede dieser Noten stumm. */
static const UBYTE schlag_ersatz[][2] = {
	{ 82, 70 },        /* Shaker      -> Maracas          */
	{ 83, 81 }         /* Jingle Bell -> offenes Triangel */
};

static void note_an(UBYTE kanal, UBYTE note, UBYTE anschlag)
{
	if(vorlauf) return;

	int idx = (kanal == 9) ? (int)note + 100 : (int)kanal_prog[kanal];

	if(kanal == 9 && idx < INSTRUMENTE && !instr[idx].proben)
		for(unsigned e = 0; e < sizeof(schlag_ersatz) / 2; e++)
			if(schlag_ersatz[e][0] == note)
			{ idx = (int)schlag_ersatz[e][1] + 100; break; }

	if(idx < 0 || idx >= INSTRUMENTE || !instr[idx].proben)
	{ z_ohne_instrument++; return; }

	Instrument *in = &instr[idx];

	/* Schlagzeug ungestimmt, Melodie nach der Tonhoehe. */
	int halbton = (kanal == 9) ? 0 : ((int)note - (int)in->grundton);

	if(halbton < -64) halbton = -64;
	if(halbton >  63) halbton =  63;

	ULONG quellrate = (ULONG)(((unsigned long long)in->rate
	                           * faktor[halbton + 64]) >> 16);

	if(quellrate < 100) quellrate = 100;

	UBYTE gruppe = gruppe_von(kanal, note);
	int i = stimme_waehlen(kanal, gruppe);

	/* Lautstaerke 0..256, aus Anschlag und Kanallautstaerke.
	 * Panorama: 0 = links, 64 = Mitte, 127 = rechts. */
	ULONG v = ((ULONG)anschlag * kanal_vol[kanal]) >> 6;   /* 0..252 */

	if(v > 256) v = 256;

	ULONG pan = kanal_pan[kanal];

	if(pan > 127) pan = 127;

	stimme[i].proben   = in->proben;
	stimme[i].pos      = 0;
	stimme[i].ende     = in->laenge << 16;
	stimme[i].schleife = in->schleife ? (in->schleife << 16) : 0;
	stimme[i].einmalig = in->schleife ? 0 : 1;
	stimme[i].schritt  = (ULONG)(((unsigned long long)quellrate << 16)
	                             / mischrate);
	stimme[i].vol_l    = (WORD)((v * (127 - pan)) / 127);
	stimme[i].vol_r    = (WORD)((v * pan) / 127);
	stimme[i].kanal    = kanal;
	stimme[i].gruppe   = gruppe;
	stimme[i].begonnen = schritt_nr;
	stimme[i].ausklang = 0;
	stimme[i].aktiv    = 1;

	z_noten++;

	ULONG n = 0;

	for(int k = 0; k < stimmen_da; k++) if(stimme[k].aktiv) n++;

	if(n > z_spitze) z_spitze = n;
}

static void note_aus(UBYTE kanal, UBYTE note)
{
	UBYTE gruppe = gruppe_von(kanal, note);

	for(int i = 0; i < musik_plaetze(); i++)
	{
		if(!stimme[i].aktiv || stimme[i].kanal != kanal) continue;
		if(stimme[i].gruppe != gruppe) continue;

		/* NUR Schlagzeug klingt aus -- das Notenende gilt dort nicht.
		 * Alles andere endet mit der Note, auch einmalige Proben. Ohne
		 * diese Unterscheidung belegte jeder Klavieranschlag seine Stimme
		 * bis zu 3,3 s; gemessen: Spitze 32 von 32 statt 13. */
		/* NICHT hart abschneiden -- das knackt hoerbar. Die Lautstaerke
		 * laeuft ueber wenige Bloecke aus (je Block auf drei Viertel,
		 * bei 256 Rahmen also rund 80 ms). Die Python-Hoerprobe mit
		 * 120 ms Ausklang klang deutlich besser als die erste
		 * C-Fassung ohne. */
		if(kanal != 9) stimme[i].ausklang = 1;

		return;
	}
}

/* ----------------------------------------------------------- Ereignisse */
static void schritt_ereignisse(void)
{
	while(!zu_ende && naechster_schritt <= schritt_nr)
	{
		if(lauf >= lauf_ende) { zu_ende = 1; return; }

		UBYTE art = *lauf++;

		switch(art)
		{
		case 0:
			if(lauf + 3 > lauf_ende) { zu_ende = 1; return; }
			note_an(lauf[0] & 15, lauf[1], lauf[2]);
			lauf += 3;
			break;

		case 1:
			if(lauf + 2 > lauf_ende) { zu_ende = 1; return; }
			note_aus(lauf[0] & 15, lauf[1]);
			lauf += 2;
			break;

		case 2:
			if(lauf + 2 > lauf_ende) { zu_ende = 1; return; }
			kanal_prog[lauf[0] & 15] = lauf[1];
			lauf += 2;
			break;

		case 3:
			if(lauf + 2 > lauf_ende) { zu_ende = 1; return; }
			kanal_vol[lauf[0] & 15] = lauf[1];
			lauf += 2;
			break;

		case 5:
			break;

		default:
			zu_ende = 1;
			return;
		}

		naechste_wartezeit();
	}
}

/* --------------------------------------------------------------- Mischen */
/* Gemischt wird in 32 Bit und erst beim Schreiben begrenzt.
 *
 * Ein erster Stand addierte unmittelbar in die 16-Bit-Ausgabe. Das
 * UEBERLAEUFT bei lauten Stellen, statt zu begrenzen -- aus einem Fortissimo
 * wird dann Krachen, und zwar nur manchmal. Aufgefallen ist es nicht am
 * Klang, sondern an der Pegelmessung des Host-Tests: Spitze 119 von 32767,
 * also war ausserdem der Massstab um den Faktor 250 daneben.
 *
 * Massstab: eine Probe ist -128..127, die Stimmlautstaerke 0..256. Das
 * Produkt fuellt mit MISCH_SCHIEBUNG 2 die 16 Bit bei vier gleichzeitig
 * voll ausgesteuerten Stimmen. Der Host-Test meldet, wie oft wirklich
 * begrenzt wird -- daran wird der Wert entschieden, nicht am Gefuehl. */
#define MISCH_BLOCK      256
#ifndef MISCH_SCHIEBUNG
#define MISCH_SCHIEBUNG    1
#endif

static LONG misch[2 * MISCH_BLOCK];

/* Eine Stimme in den 32-Bit-Mischpuffer addieren.
 *
 * Diese Fassung ist der MASSSTAB: der Assemblerkern in musik_kern.s muss
 * byteweise dasselbe liefern, und mischer_selbsttest() prueft genau das.
 *
 * Rueckgabe: neue Leseposition, oder MIX_ENDE, wenn die Probe ausgelaufen
 * ist (dann hat die Stimme nur die ersten Rahmen beigetragen). */
#define MIX_ENDE  0xFFFFFFFFUL

ULONG mischer_stimme_c(LONG *ziel, const BYTE *pr, ULONG pos, ULONG schritt,
                       ULONG ende, ULONG schleife, LONG vl, LONG vr,
                       ULONG rahmen)
{
	ULONG probenzahl = ende >> 16;
	ULONG rund = ende - schleife;

	if(!rund) schleife = 0;

	for(ULONG i = 0; i < rahmen; i++)
	{
		if(pos >= ende)
		{
			if(!schleife) return MIX_ENDE;

			while(pos >= ende) pos -= rund;
		}

		ULONG idx = pos >> 16;
		LONG a = pr[idx];
		LONG b = (idx + 1 < probenzahl)
		       ? pr[idx + 1]
		       : (schleife ? pr[schleife >> 16] : a);

		LONG p = a + (((b - a) * (LONG)((pos >> 8) & 0xFF)) >> 8);

		*ziel++ += p * vl;
		*ziel++ += p * vr;

		pos += schritt;
	}

	return pos;
}

#ifdef ZOD_MIX_ASM
extern ULONG zod_mix_kern(const LONG *block, ULONG rahmen);

/* Erst nach bestandenem Selbsttest. Ein Assemblerkern, der ungeprueft
 * laeuft, ist eine Fehlerquelle ohne Gegenprobe -- dieselbe Regel wie
 * beim AMMX-Blitter. */
static int asm_an = 0;

int mischer_selbsttest(void)
{
	/* Faelle, die alle Zweige treffen: mitten in der Probe, genau am
	 * Ende, ueber das Ende hinaus, mit und ohne Schleife, verschiedene
	 * Bruchteile und Schrittweiten. */
	static const BYTE proben[64] = {
		0, 40, 80, 120, 127, 100, 60, 20, -20, -60, -100, -127, -120, -80, -40, 0,
		1, -1, 2, -2, 3, -3, 4, -4, 5, -5, 6, -6, 7, -7, 8, -8,
		127, -128, 127, -128, 0, 0, 64, -64, 32, -32, 16, -16, 96, -96, 48, -48,
		10, 20, 30, 40, 50, 60, 70, 80, 90, 100, 110, 120, 125, 126, 127, 0
	};

	static const ULONG schritte[] = { 0x8000, 0x10000, 0x18000, 0x20000,
	                                  0x04000, 0x1C3F7, 0x40000 };
	static const ULONG starts[]   = { 0, 0x8000, 0x137F0, 0x3F0000 };
	static const ULONG schleifen[] = { 0, 0x100000, 0x200000 };

	LONG c_puffer[2 * 40];
	LONG a_puffer[2 * 40];
	int faelle = 0;

	asm_an = 0;

	for(unsigned si = 0; si < sizeof(schritte) / sizeof(schritte[0]); si++)
	for(unsigned st = 0; st < sizeof(starts) / sizeof(starts[0]); st++)
	for(unsigned sl = 0; sl < sizeof(schleifen) / sizeof(schleifen[0]); sl++)
	for(int rahmen = 1; rahmen <= 40; rahmen += 13)
	{
		ULONG ende = 64UL << 16;

		if(schleifen[sl] >= ende) continue;

		LONG blk[8];
		int i;

		for(i = 0; i < 2 * 40; i++) c_puffer[i] = a_puffer[i] = (LONG)(i * 7 - 100);

		ULONG c_pos = mischer_stimme_c(c_puffer, proben, starts[st], schritte[si],
		                               ende, schleifen[sl], 200, 130, (ULONG)rahmen);

		blk[0] = (LONG)a_puffer;
		blk[1] = (LONG)proben;
		blk[2] = (LONG)starts[st];
		blk[3] = (LONG)schritte[si];
		blk[4] = (LONG)ende;
		blk[5] = (LONG)schleifen[sl];
		blk[6] = 200;
		blk[7] = 130;

		ULONG a_pos = zod_mix_kern(blk, (ULONG)rahmen);

		faelle++;

		if(a_pos != c_pos) return -faelle;

		for(i = 0; i < 2 * rahmen; i++)
			if(c_puffer[i] != a_puffer[i]) return -faelle;
	}

	asm_an = 1;

	return faelle;
}

int mischer_asm_laeuft(void) { return asm_an; }
#else
int mischer_selbsttest(void) { return 0; }
int mischer_asm_laeuft(void) { return 0; }
#endif

static void block_mischen(WORD *ziel, ULONG rahmen)
{
	memset(misch, 0, rahmen * 2 * sizeof(LONG));

	for(int s = 0; s < stimmen_da; s++)
	{
		Stimme *v = &stimme[s];

		if(!v->aktiv) continue;

		ULONG neu;

#ifdef ZOD_MIX_ASM
		if(asm_an)
		{
			LONG blk[8];

			blk[0] = (LONG)misch;
			blk[1] = (LONG)v->proben;
			blk[2] = (LONG)v->pos;
			blk[3] = (LONG)v->schritt;
			blk[4] = (LONG)v->ende;
			blk[5] = (LONG)v->schleife;
			blk[6] = v->vol_l;
			blk[7] = v->vol_r;

			neu = zod_mix_kern(blk, rahmen);
		}
		else
#endif
		neu = mischer_stimme_c(misch, v->proben, v->pos, v->schritt,
		                       v->ende, v->schleife, v->vol_l, v->vol_r,
		                       rahmen);

		if(neu == MIX_ENDE) v->aktiv = 0;
		else                v->pos = neu;
	}

	for(int s = 0; s < stimmen_da; s++)
	{
		Stimme *v = &stimme[s];

		if(!v->aktiv || !v->ausklang) continue;

		v->vol_l = (WORD)((v->vol_l * 3) >> 2);
		v->vol_r = (WORD)((v->vol_r * 3) >> 2);

		if(v->vol_l < 2 && v->vol_r < 2) v->aktiv = 0;
	}

	for(ULONG i = 0; i < rahmen * 2; i++)
	{
		LONG w = misch[i] >> MISCH_SCHIEBUNG;

		if(w >  32767) { w =  32767; z_geklemmt++; }
		if(w < -32768) { w = -32768; z_geklemmt++; }

		ziel[i] = (WORD)w;
	}
}

static void abschnitt_mischen(WORD *ziel, ULONG rahmen)
{
	while(rahmen)
	{
		ULONG n = rahmen > MISCH_BLOCK ? MISCH_BLOCK : rahmen;

		block_mischen(ziel, n);

		ziel += n * 2;
		rahmen -= n;
	}
}

void mischer_fuellen(WORD *ziel, ULONG rahmen)
{
	while(rahmen)
	{
		if(!rahmen_rest)
		{
			schritt_ereignisse();
			schritt_nr++;
			rahmen_rest = rahmen_je_schritt;
		}

		ULONG jetzt = rahmen_rest >> 16;

		if(jetzt > rahmen) jetzt = rahmen;

		if(!jetzt)
		{
			/* Weniger als ein ganzer Rahmen uebrig: Rest verfaellt in den
			 * naechsten Schritt, damit sich der Fehler NICHT aufsummiert. */
			rahmen_rest = 0;
			continue;
		}

		abschnitt_mischen(ziel, jetzt);

		ziel += jetzt * 2;
		rahmen -= jetzt;
		rahmen_rest -= jetzt << 16;
	}
}

void mischer_effektstimmen(int anzahl)
{
	if(anzahl < 0) anzahl = 0;
	if(anzahl > stimmen_da - 1) anzahl = stimmen_da - 1;

	effekt_stimmen = anzahl;
}

int mischer_effekt(const BYTE *proben, ULONG laenge, ULONG rate,
                   int vol, int pan, int schleife)
{
	int erster = musik_plaetze();
	int platz = -1;
	ULONG alter = 0xFFFFFFFFUL;

	if(!proben || !laenge || effekt_stimmen < 1) return -1;

	if(!rate) rate = 11025;

	for(int i = erster; i < stimmen_da; i++)
		if(!stimme[i].aktiv) { platz = i; break; }

	/* Kein freier Platz: den aeltesten Effekt verdraengen. Ein Schuss, der
	 * nicht zu hoeren ist, ist besser als einer, der die Musik abwuergt --
	 * deshalb bleibt die Verdraengung im Effektbereich. */
	if(platz < 0)
	{
		for(int i = erster; i < stimmen_da; i++)
			if(stimme[i].begonnen < alter)
			{ alter = stimme[i].begonnen; platz = i; }

		z_verdraengt++;
	}

	if(platz < 0) return -1;

	if(vol < 0) vol = 0;
	if(vol > 128) vol = 128;
	if(pan < 0) pan = 0;
	if(pan > 127) pan = 127;

	ULONG v = ((ULONG)vol * 256) / 128;

	Stimme *e = &stimme[platz];

	e->proben   = proben;
	e->pos      = 0;
	e->ende     = laenge << 16;
	e->schleife = schleife ? (1UL << 16) : 0;
	e->einmalig = schleife ? 0 : 1;
	e->schritt  = (ULONG)(((unsigned long long)rate << 16) / mischrate);
	e->vol_l    = (WORD)((v * (127 - pan)) / 127);
	e->vol_r    = (WORD)((v * pan) / 127);
	e->kanal    = 255;                 /* kein MIDI-Kanal */
	e->gruppe   = 0;
	e->ausklang = 0;
	e->begonnen = schritt_nr;
	e->aktiv    = 1;

	return platz;
}

void mischer_effekt_stop(int platz)
{
	if(platz < musik_plaetze() || platz >= stimmen_da) return;

	stimme[platz].aktiv = 0;
}

int mischer_effekt_laeuft(int platz)
{
	if(platz < musik_plaetze() || platz >= stimmen_da) return 0;

	return stimme[platz].aktiv ? 1 : 0;
}

void mischer_effekt_vol(int platz, int vol)
{
	if(platz < musik_plaetze() || platz >= stimmen_da) return;

	if(vol < 0) vol = 0;
	if(vol > 128) vol = 128;

	ULONG v = ((ULONG)vol * 256) / 128;
	Stimme *e = &stimme[platz];
	ULONG summe = (ULONG)e->vol_l + (ULONG)e->vol_r;

	/* Panorama beibehalten, nur die Summe aendern. */
	if(!summe) { e->vol_l = e->vol_r = (WORD)(v / 2); return; }

	e->vol_l = (WORD)(((ULONG)e->vol_l * v) / summe);
	e->vol_r = (WORD)(((ULONG)e->vol_r * v) / summe);
}

void mischer_springen(ULONG schritte)
{
	const UBYTE *z = lauf_anfang;
	ULONG len = lauf_laenge;

	if(!z || !len) return;

	mischer_stueck(z, len);

	vorlauf = 1;

	while(!zu_ende && schritt_nr < schritte)
	{
		schritt_ereignisse();
		schritt_nr++;
	}

	vorlauf = 0;
	rahmen_rest = 0;
}

int mischer_laeuft(void)
{
	if(!zu_ende) return 1;

	for(int i = 0; i < stimmen_da; i++) if(stimme[i].aktiv) return 1;

	return 0;
}

void mischer_zahlen(ULONG *noten, ULONG *spitze, ULONG *verdraengt,
                    ULONG *ohne_instrument, ULONG *geklemmt)
{
	if(geklemmt)        *geklemmt = z_geklemmt;
	if(noten)           *noten = z_noten;
	if(spitze)          *spitze = z_spitze;
	if(verdraengt)      *verdraengt = z_verdraengt;
	if(ohne_instrument) *ohne_instrument = z_ohne_instrument;
}
