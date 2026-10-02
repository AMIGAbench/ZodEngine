/*
 * Musikausgabe -- siehe musik_ausgabe.h fuer das Warum.
 *
 * Aufbau:
 *
 *   Interrupt/SoundFunc  setzt ein Signal, sonst nichts
 *          |
 *   Mischprocess         wartet, fuellt den freien Puffer, meldet ihn an
 *          |
 *   AHI oder Paula       spielt abwechselnd Puffer 0 und 1
 *
 * Der Process laeuft mit Prioritaet 5. Hoeher waere schaedlich (er wuerde
 * die Eingabe verdraengen), niedriger riskant: das Spiel haelt in der
 * Bot-Wegsuche schon mal 350 ms am Stueck, und in der Zeit muss der
 * Nachschub trotzdem kommen.
 */
#include "musik_ausgabe.h"
#include "musik_mischer.h"
#include "amiga_audio.h"

#include <exec/memory.h>
#include <exec/interrupts.h>
#include <devices/ahi.h>
#include <devices/audio.h>
#include <hardware/custom.h>
#include <hardware/intbits.h>
#include <dos/dostags.h>
#include <utility/hooks.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/ahi.h>
#include <clib/alib_protos.h>   /* BeginIO */

#include <string.h>

#include "zod_log.h"

/* Die Chipregister. "far" ist SAS/C-Syntax und hier falsch; amiga-gcc
 * bindet das Symbol ueber -lamiga an 0xDFF000. */
extern struct Custom custom;

/* HookEntry steht in clib/alib_protos.h (oben eingebunden) und kommt aus
 * der libamiga: es schiebt (a0,a2,a1) in die C-Reihenfolge und springt
 * h_SubEntry an. Eine eigene Deklaration hier waere ein Typkonflikt. */

/* ------------------------------------------------------------- Zustand */
#define PUFFER_ZAHL 2

/* proto/ahi.h erwartet GENAU diesen Namen. Definiert wird er nicht hier:
 * in der Engine gehoert er amiga_audio.cpp, die AHI fuer die Klangeffekte
 * ohnehin schon oeffnet. Beide OpenDevice-Aufrufe liefern dieselbe
 * Geraetebasis -- wer zuerst schliesst, macht sie aber fuer den anderen
 * ungueltig. Das ist bei der Einbindung in die Engine zu beachten. */
extern struct Library *AHIBase;
static struct MsgPort *ahi_port = 0;
static struct AHIRequest *ahi_req = 0;
static struct AHIAudioCtrl *ctrl = 0;
static struct Hook klang_hook;

static struct MsgPort *audio_port = 0;
static struct IOAudio *audio_req = 0;

static struct Process *misch_proc = 0;
static struct Task *misch_task = 0;
static volatile LONG misch_signal = -1;
static volatile int laeuft = 0;
static volatile int beenden = 0;
static volatile int pausiert = 0;

static WORD  *puffer16[PUFFER_ZAHL];     /* AHI: 16 Bit stereo verschraenkt */
static BYTE  *paula_l[PUFFER_ZAHL];      /* Paula: 8 Bit, je Seite getrennt */
static BYTE  *paula_r[PUFFER_ZAHL];
static ULONG  puffer_rahmen = 0;
static volatile int naechster = 0;       /* welchen Puffer der Process fuellt */

static int   weg_aktiv = -1;
static ULONG ausgaberate = 22050;
static int   lautstaerke = 128;

static const UBYTE *stueck = 0;
static ULONG stueck_len = 0;
static int   stueck_schleife = 0;
static volatile int stueck_neu = 0;
static volatile ULONG sprung_ziel = 0;
static volatile int   sprung_faellig = 0;

static ULONG z_puffer = 0, z_zu_spaet = 0;

/* ------------------------------------------------------- Signal setzen */
/* Laeuft im Interrupt bzw. in AHIs SoundFunc. NUR das Signal, nichts
 * weiter -- kein Mischen, keine Ausgabe, kein Speicher. */
static void wecken(void)
{
	if(misch_task && misch_signal >= 0)
		Signal(misch_task, 1UL << misch_signal);
}

static ULONG ahi_klang_hook(struct Hook *h, struct AHIAudioCtrl *ac, APTR msg)
{
	(void)h; (void)ac; (void)msg;
	wecken();
	return 0;
}

/* ------------------------------------------------------------- Paula
 * NACHGEBAUT AUS ADooms amiga_music.s (Joe Fenton / Peter McGavin) -- das
 * ist erwiesenermassen lauffaehiger Code, und der erste eigene Entwurf war
 * an mehreren Stellen falsch:
 *
 *  - AddIntServer(INTB_AUD2, ...) gibt es NICHT. Exec fuehrt Serverketten
 *    nur fuer PORTS, COPER, VERTB, EXTER und NMI. AUD0..AUD3 sind HANDLER
 *    mit genau einem Besitzer und werden ueber SetIntVector gesetzt. Der
 *    erste Entwurf haengte in eine Kette ein, die es nicht gibt -- auf der
 *    V1200 Guru 80000006.
 *  - Die Kanalmaske {1,2,4,8} mit Laenge 4 fordert EINEN Kanal an ("einer
 *    von diesen vieren"), nicht vier. Richtig ist eine Maske {0x0F} der
 *    Laenge 1.
 *  - adkcon muss die Modulation abschalten, sonst haengen Kanaele
 *    aneinander.
 *  - Vor dem Einschalten zeigen die Kanaele auf einen Stillepuffer mit
 *    Lautstaerke 0, und der Handler wird EINMAL von Hand gerufen
 *    (bei ADoom "bsr AudioINT2") -- sonst wird der erste Puffer nie
 *    angestossen.
 */
static UBYTE audio_alloc[1];             /* Kanalmaske, Laenge 1 */
static struct Interrupt paula_handler;
static APTR paula_alt = 0;
static BYTE *stille = 0;                 /* Chip, Lautstaerke-0-Puffer */
static int paula_int_bit = INTB_AUD0;
static UWORD paula_int_maske = 0;

static void paula_server(void)
{
	custom.intreq = paula_int_maske;
	wecken();
}

static void paula_puffer_setzen(int nr)
{
	/* ch0 + ch3 links, ch1 + ch2 rechts -- so ordnet Paula die Kanaele den
	 * Seiten zu. Die Periode ergibt die Rate: PAL-Takt 3546895 Hz durch
	 * die gewuenschte Abtastrate. */
	UWORD per = (UWORD)(3546895UL / ausgaberate);
	UWORD len = (UWORD)(puffer_rahmen / 2);       /* Paula zaehlt WORTE */
	UWORD vol = (UWORD)((lautstaerke * 64) / 128);

	custom.aud[0].ac_ptr = (UWORD *)paula_l[nr];
	custom.aud[0].ac_len = len;
	custom.aud[0].ac_per = per;
	custom.aud[0].ac_vol = vol;

	custom.aud[3].ac_ptr = (UWORD *)paula_l[nr];
	custom.aud[3].ac_len = len;
	custom.aud[3].ac_per = per;
	custom.aud[3].ac_vol = vol;

	custom.aud[1].ac_ptr = (UWORD *)paula_r[nr];
	custom.aud[1].ac_len = len;
	custom.aud[1].ac_per = per;
	custom.aud[1].ac_vol = vol;

	custom.aud[2].ac_ptr = (UWORD *)paula_r[nr];
	custom.aud[2].ac_len = len;
	custom.aud[2].ac_per = per;
	custom.aud[2].ac_vol = vol;
}

/* 16-Bit-Stereo in zwei 8-Bit-Spuren zerlegen. Paula kann nichts anderes. */
static void nach_paula(const WORD *quelle, int nr)
{
	BYTE *l = paula_l[nr];
	BYTE *r = paula_r[nr];

	for(ULONG i = 0; i < puffer_rahmen; i++)
	{
		*l++ = (BYTE)(*quelle++ >> 8);
		*r++ = (BYTE)(*quelle++ >> 8);
	}
}

/* --------------------------------------------------------- Mischprocess */
static void misch_schleife(void)
{
	misch_task = FindTask(0);
	misch_signal = AllocSignal(-1);

	if(misch_signal < 0)
	{
		ZLOG("Musik: kein Signal frei\n");
		laeuft = 0;
		return;
	}

	/* Beide Puffer vorab fuellen, sonst beginnt das Stueck mit Stille. */
	for(int i = 0; i < PUFFER_ZAHL; i++)
	{
		mischer_fuellen(puffer16[i], puffer_rahmen);

		if(weg_aktiv == MUSIK_PAULA) nach_paula(puffer16[i], i);

		z_puffer++;
	}

	naechster = 0;

	while(!beenden)
	{
		Wait(1UL << misch_signal);

		if(beenden) break;

		if(stueck_neu)
		{
			mischer_stueck(stueck, stueck_len);
			stueck_neu = 0;
		}

		if(sprung_faellig)
		{
			mischer_springen(sprung_ziel);
			sprung_faellig = 0;
		}

		if(pausiert)
		{
			memset(puffer16[naechster], 0,
			       puffer_rahmen * 2 * sizeof(WORD));
		}
		else
		{
			mischer_fuellen(puffer16[naechster], puffer_rahmen);

			/* Am Ende des Stueckes von vorn, wenn gewuenscht. */
			if(stueck_schleife && !mischer_laeuft())
				mischer_stueck(stueck, stueck_len);
		}

		/* DEN FRISCH GEFUELLTEN PUFFER ANMELDEN. Ohne das spielt AHI
		 * den beim Start gesetzten LoopSound ENDLOS weiter, waehrend der
		 * Process daneben abwechselnd beide Puffer neu fuellt -- man
		 * hoert dann Bruchstuecke doppelt und es ruckelt. Genau so hat
		 * es der Nutzer auf der V1200 beschrieben.
		 *
		 * Dasselbe gilt fuer Paula: die Kanaele laden ihre Laenge nach
		 * dem Durchlauf aus den zuletzt geschriebenen Registern neu. */
		if(weg_aktiv == MUSIK_PAULA)
		{
			nach_paula(puffer16[naechster], naechster);
			paula_puffer_setzen(naechster);
		}
		else
			zod_audio_stream_queue(naechster);

		z_puffer++;
		naechster ^= 1;
	}

	if(misch_signal >= 0) { FreeSignal(misch_signal); misch_signal = -1; }

	misch_task = 0;
	laeuft = 0;
}

/* ------------------------------------------------------------------ AHI
 * KEIN eigenes AHI_AllocAudio. Ein Treiber laesst nur EINEN Kontext zu,
 * und die Engine hat ihn fuer die Klangeffekte bereits offen -- ein
 * zweiter scheiterte im Emulatorlauf bei JEDEM Modus, obwohl AHI
 * nachweislich lief. Die fertig gemischten Puffer gehen deshalb ueber
 * zod_audio_stream_* auf den Kanal, den amiga_audio.cpp ohnehin fuer
 * Musik freihaelt.
 *
 * Nebenwirkung, die zum Entwurf passt: Modus, Mischfrequenz und
 * Kanalzahl bestimmt weiterhin EINE Stelle -- die, die auch die
 * Klangeffekte aufsetzt. */
static int ahi_oeffnen(int stimmen)
{
	(void)stimmen;

	return 1;                    /* nichts zu tun, der Kontext steht schon */
}

static int ahi_anwerfen(void)
{
	return zod_audio_stream_open(ausgaberate, puffer_rahmen,
	                             puffer16[0], puffer16[1], wecken);
}

/* ---------------------------------------------------------------- Paula */
static int paula_oeffnen(void)
{
	audio_port = CreateMsgPort();
	audio_req = audio_port
	          ? (struct IOAudio *)CreateIORequest(audio_port, sizeof(struct IOAudio))
	          : 0;

	if(!audio_req) { ZLOG("Musik: kein Audio-IORequest\n"); return 0; }

	/* EINE Maske der Laenge 1: "genau diese Kanaele". Erst alle vier
	 * (doppelte Lautstaerke je Seite), sonst ch0+ch1 als Notbehelf. */
	static const UBYTE wuensche[] = { 0x0F, 0x03 };
	int gewaehlt = -1;

	for(unsigned w = 0; w < sizeof(wuensche) && gewaehlt < 0; w++)
	{
		audio_alloc[0] = wuensche[w];

		audio_req->ioa_Request.io_Message.mn_Node.ln_Pri = 10;
		audio_req->ioa_AllocKey = 0;
		audio_req->ioa_Data     = audio_alloc;
		audio_req->ioa_Length   = 1;

		if(!OpenDevice((CONST_STRPTR)"audio.device", 0,
		               (struct IORequest *)audio_req, 0))
			gewaehlt = (int)wuensche[w];
	}

	if(gewaehlt < 0)
	{
		ZLOG("Musik: audio.device gibt keine Kanaele her\n");
		return 0;
	}

	/* ADCMD_LOCK: ab jetzt gehoeren uns die Kanaele, und erst damit ist
	 * SetIntVector auf den Audio-Interrupt zulaessig. */
	audio_req->ioa_Request.io_Command = ADCMD_LOCK;
	audio_req->ioa_Request.io_Flags   = IOF_QUICK;
	BeginIO((struct IORequest *)audio_req);

	/* Der Interrupt haengt am NIEDRIGSTEN belegten Kanal. Alle Kanaele
	 * haben dieselbe Laenge, sie werden also gleichzeitig fertig -- einer
	 * genuegt zum Wecken. */
	paula_int_bit = (gewaehlt & 1) ? INTB_AUD0 : INTB_AUD1;
	paula_int_maske = (UWORD)(1u << paula_int_bit);

	custom.intena = 0x0780;                  /* alle vier Audio-Ints aus  */
	custom.intreq = 0x0780;
	custom.adkcon = 0x00FF;                  /* Modulation aus            */
	custom.dmacon = 0x000F;                  /* Audio-DMA aus             */

	paula_handler.is_Node.ln_Type = NT_INTERRUPT;
	paula_handler.is_Node.ln_Pri  = 0;
	paula_handler.is_Node.ln_Name = (char *)"ZodMusik";
	paula_handler.is_Data         = 0;
	paula_handler.is_Code         = (void (*)())paula_server;

	/* SetIntVector, NICHT AddIntServer: AUD0..AUD3 haben keine
	 * Serverkette. Der alte Vektor wird gemerkt und beim Aufraeumen
	 * zurueckgesetzt. */
	paula_alt = SetIntVector(paula_int_bit, &paula_handler);

	return 1;
}

static void paula_anwerfen(void)
{
	UWORD per = (UWORD)(3546895UL / ausgaberate);

	/* Erst Stille auflegen, Lautstaerke 0 -- sonst spielt Paula beim
	 * Einschalten den Inhalt frisch belegten Chip-Speichers. */
	for(int k = 0; k < 4; k++)
	{
		custom.aud[k].ac_ptr = (UWORD *)stille;
		custom.aud[k].ac_len = (UWORD)(puffer_rahmen / 2);
		custom.aud[k].ac_per = per;
		custom.aud[k].ac_vol = 0;
	}

	Disable();

	custom.intena = (UWORD)(INTF_SETCLR | paula_int_maske);
	custom.dmacon = 0x800F;

	/* Den Handler EINMAL von Hand rufen, damit der erste echte Puffer
	 * angemeldet wird. ADoom macht an dieser Stelle "bsr AudioINT2". */
	paula_server();

	Enable();
}

/* ------------------------------------------------------------- Schnittstelle */
int musik_start(int weg, const UBYTE *bank, ULONG banklen,
                ULONG rate, int stimmen)
{
	if(laeuft) musik_stop();

	ausgaberate = rate ? rate : 22050;
	beenden = 0;
	pausiert = 0;
	z_puffer = z_zu_spaet = 0;

	if(!mischer_init(bank, banklen, ausgaberate, stimmen))
	{
		ZLOG("Musik: Instrumentenbank unbrauchbar\n");
		return 0;
	}

	/* Ein Puffer von einem Achtel Sekunde. Kuerzer hiesse mehr Interrupts,
	 * laenger mehr Verzoegerung beim Umschalten des Stueckes. */
	puffer_rahmen = ausgaberate / 8;
	puffer_rahmen &= ~1UL;                   /* Paula zaehlt Worte */

	for(int i = 0; i < PUFFER_ZAHL; i++)
	{
		puffer16[i] = (WORD *)AllocVec(puffer_rahmen * 2 * sizeof(WORD),
		                               MEMF_ANY | MEMF_CLEAR);

		if(!puffer16[i]) { ZLOG("Musik: kein Mischpuffer\n"); musik_stop(); return 0; }

		if(weg == MUSIK_PAULA)
		{
			paula_l[i] = (BYTE *)AllocMem(puffer_rahmen, MEMF_CHIP | MEMF_CLEAR);
			paula_r[i] = (BYTE *)AllocMem(puffer_rahmen, MEMF_CHIP | MEMF_CLEAR);

			if(!paula_l[i] || !paula_r[i])
			{ ZLOG("Musik: kein Chip-Speicher\n"); musik_stop(); return 0; }

			if(!stille)
			{
				stille = (BYTE *)AllocMem(puffer_rahmen, MEMF_CHIP | MEMF_CLEAR);

				if(!stille)
				{ ZLOG("Musik: kein Chip-Speicher fuer die Stille\n"); musik_stop(); return 0; }
			}
		}
	}

	if(weg == MUSIK_PAULA)
	{
		if(!paula_oeffnen()) { musik_stop(); return 0; }
		weg_aktiv = MUSIK_PAULA;
	}
	else
	{
		if(!ahi_oeffnen(stimmen)) { musik_stop(); return 0; }
		weg_aktiv = MUSIK_AHI;
	}

	laeuft = 1;

	misch_proc = CreateNewProcTags(NP_Entry,     (ULONG)misch_schleife,
	                               NP_Name,      (ULONG)"ZodMusik",
	                               NP_Priority,  5,
	                               NP_StackSize, 16384,
	                               TAG_DONE);

	if(!misch_proc)
	{
		ZLOG("Musik: Mischprocess laesst sich nicht anlegen\n");
		musik_stop();
		return 0;
	}

	/* Auf die Vorabfuellung warten -- sonst beginnt die Ausgabe mit
	 * dem Inhalt eines frisch belegten Puffers, und der ist auf dem
	 * Amiga NICHT genullt. */
	for(int warte = 0; warte < 100 && z_puffer < PUFFER_ZAHL; warte++)
		Delay(1);

	if(weg_aktiv == MUSIK_PAULA) paula_anwerfen();
	else if(!ahi_anwerfen())     { musik_stop(); return 0; }

	ZLOG("Musik: %s, %ld Hz, %ld Stimmen, Puffer %ld Rahmen\n",
	     weg_aktiv == MUSIK_PAULA ? "Paula" : "AHI",
	     (long)ausgaberate, (long)stimmen, (long)puffer_rahmen);

	return 1;
}

int musik_spiele(const UBYTE *zmu, ULONG len, int schleife)
{
	if(!laeuft) return 0;

	stueck = zmu;
	stueck_len = len;
	stueck_schleife = schleife;
	stueck_neu = 1;

	wecken();

	return 1;
}

void musik_springen(ULONG schritte)
{
	if(!laeuft) return;

	sprung_ziel = schritte;
	sprung_faellig = 1;

	wecken();
}

void musik_pause(int an) { pausiert = an ? 1 : 0; }

void musik_lautstaerke(int v)
{
	if(v < 0) v = 0;
	if(v > 128) v = 128;

	lautstaerke = v;

	if(weg_aktiv == MUSIK_AHI)
	{
		zod_audio_stream_volume(v);
	}
	else if(weg_aktiv == MUSIK_PAULA)
	{
		UWORD pv = (UWORD)((v * 64) / 128);

		for(int i = 0; i < 4; i++) custom.aud[i].ac_vol = pv;
	}
}

int musik_laeuft(void) { return laeuft; }
int musik_weg(void) { return weg_aktiv; }

void musik_zahlen(ULONG *puffer, ULONG *zu_spaet)
{
	if(puffer)   *puffer = z_puffer;
	if(zu_spaet) *zu_spaet = z_zu_spaet;
}

void musik_stop(void)
{
	/* Gefahrlos, wenn nie etwas lief: alle Zeiger sind dann null und
	 * weg_aktiv ist -1. Gerufen wird das auch aus zod_audio_close, also
	 * aus dem atexit-Weg, und dort darf nichts voraussetzen. */
	beenden = 1;

	if(misch_task) { wecken(); for(int i = 0; i < 100 && misch_task; i++) Delay(1); }

	if(weg_aktiv == MUSIK_PAULA)
	{
		custom.dmacon = 0x000F;

		custom.intena = 0x0780;
		custom.intreq = 0x0780;

		if(paula_alt) { SetIntVector(paula_int_bit, (struct Interrupt *)paula_alt); paula_alt = 0; }

		/* Die Kanaele erst NACH dem Zurueckgeben des Vektors freigeben. */
		audio_req->ioa_Request.io_Command = ADCMD_FREE;
		audio_req->ioa_Request.io_Flags   = IOF_QUICK;
		BeginIO((struct IORequest *)audio_req);

		if(audio_req)
		{
			CloseDevice((struct IORequest *)audio_req);
			DeleteIORequest((struct IORequest *)audio_req);
			audio_req = 0;
		}

		if(audio_port) { DeleteMsgPort(audio_port); audio_port = 0; }
	}

	if(weg_aktiv == MUSIK_AHI) zod_audio_stream_close();

	for(int i = 0; i < PUFFER_ZAHL; i++)
	{
		if(puffer16[i]) { FreeVec(puffer16[i]); puffer16[i] = 0; }
		if(paula_l[i])  { FreeMem(paula_l[i], puffer_rahmen); paula_l[i] = 0; }
		if(paula_r[i])  { FreeMem(paula_r[i], puffer_rahmen); paula_r[i] = 0; }
	}

	if(stille) { FreeMem(stille, puffer_rahmen); stille = 0; }

	{
	}

	misch_proc = 0;
	weg_aktiv = -1;
	laeuft = 0;
	beenden = 0;
}
