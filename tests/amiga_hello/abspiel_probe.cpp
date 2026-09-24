/*
 * abspiel_probe -- spielt ein Stueck ueber AHI ODER Paula.
 *
 * Der Hoertest fuer beide Ausgabewege, bevor irgendetwas davon in die
 * Engine kommt. Gleicher Mischer, gleiche Bank, gleiches Stueck -- der
 * einzige Unterschied ist der Weg zur Hardware.
 *
 *   abspiel_probe <bank> <stueck.zmu> <ahi|paula> [sekunden] [rate]
 */
#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <devices/timer.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/timer.h>

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "../../platform/amiga/debug.h"
#include "../../port/amiga/musik_ausgabe.h"
#include "../../port/amiga/musik_mischer.h"
#include "../../port/amiga/amiga_audio.h"
#include "../../port/zod_log.h"

/* AHIBase definiert amiga_audio.cpp -- das bindet die Sonde jetzt mit.
 * TimerBase dagegen kommt in der Engine aus amiga_startup.cpp, und das
 * bindet sie NICHT; zod_audio_play braucht es fuer die Systemzeit. */
struct Device *TimerBase = 0;

static struct MsgPort *timer_port = 0;
static struct timerequest *timer_req = 0;

static int timer_auf(void)
{
	timer_port = CreateMsgPort();
	timer_req = timer_port
	          ? (struct timerequest *)CreateIORequest(timer_port,
	                                                  sizeof(struct timerequest))
	          : 0;

	if(!timer_req) return 0;

	if(OpenDevice((CONST_STRPTR)TIMERNAME, UNIT_MICROHZ,
	              (struct IORequest *)timer_req, 0))
		return 0;

	TimerBase = timer_req->tr_node.io_Device;

	return 1;
}

static void timer_zu(void)
{
	if(timer_req)
	{
		CloseDevice((struct IORequest *)timer_req);
		DeleteIORequest((struct IORequest *)timer_req);
		timer_req = 0;
	}

	if(timer_port) { DeleteMsgPort(timer_port); timer_port = 0; }
}

static BPTR bericht = 0;

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
	if(bericht) Write(bericht, zeile, (LONG)strlen(zeile));
}

static UBYTE *datei_lesen(const char *name, ULONG *laenge)
{
	BPTR f = Open((CONST_STRPTR)name, MODE_OLDFILE);
	if(!f) return 0;

	Seek(f, 0, OFFSET_END);
	LONG n = Seek(f, 0, OFFSET_BEGINNING);
	if(n < 0) { Close(f); return 0; }

	UBYTE *p = (UBYTE *)AllocVec((ULONG)n, MEMF_ANY);
	if(!p) { Close(f); return 0; }

	if(Read(f, p, n) != n) { FreeVec(p); Close(f); return 0; }

	Close(f);
	*laenge = (ULONG)n;

	return p;
}

static LONG zahl(const char *p, LONG vorgabe)
{
	LONG v = 0;
	int ziffern = 0;

	for(; *p >= '0' && *p <= '9'; p++) { v = v * 10 + (*p - '0'); ziffern++; }

	return ziffern ? v : vorgabe;
}

int main(int argc, char **argv)
{
	dbg_boot();

	if(argc < 4)
	{
		dbg_fail("abspiel Aufruf: abspiel_probe <bank> <stueck.zmu> <ahi|paula> [sek] [rate]");
		return 1;
	}

	int weg = (argv[3][0] == 'p' || argv[3][0] == 'P') ? MUSIK_PAULA : MUSIK_AHI;
	LONG sekunden = (argc > 4) ? zahl(argv[4], 20) : 20;
	LONG rate     = (argc > 5) ? zahl(argv[5], 22050) : 22050;
	/* Wie oft der Ton abgegeben und zurueckgeholt wird. Das ist der
	 * Ablauf, den ein eigener Videoabspieler braucht: vor der Sequenz
	 * gibt das Spiel die Hardware frei, danach nimmt es sie zurueck.
	 * Bisher war dieser Weg NUR beim Beenden erprobt. */
	LONG runden   = (argc > 6) ? zahl(argv[6], 1) : 1;

	bericht = Open((CONST_STRPTR)"abspiel_probe.txt", MODE_NEWFILE);

	if(!timer_auf())
	{
		dbg_fail("abspiel timer.device nicht zu oeffnen");
		return 1;
	}

	/* Das Protokoll auf den seriellen Kanal legen.
	 *
	 * Ohne das erscheint KEINE der ZLOG-Zeilen aus musik_ausgabe.c, und
	 * ein Fehlschlag in musik_start ist von jedem anderen nicht zu
	 * unterscheiden: im Mitschnitt stand nur "Ausgabe liess sich nicht
	 * oeffnen", ohne Grund. Eine Sonde, die den Grund verschweigt, ist
	 * eine halbe Sonde. */
	zod_log_set(ZOD_LOG_SERIAL, 0);

	ULONG bl = 0, zl = 0;
	UBYTE *bank = datei_lesen(argv[1], &bl);
	UBYTE *zmu  = datei_lesen(argv[2], &zl);

	if(!bank || !zmu)
	{
		melde("Datei nicht lesbar\n");
		dbg_fail("abspiel Datei fehlt");
		return 1;
	}

	melde("abspiel_probe -- %s, %ld Hz, %ld s\n",
	      weg == MUSIK_PAULA ? "Paula" : "AHI", (long)rate, (long)sekunden);

	int faelle = mischer_selbsttest();

	melde("Assemblerkern: %s\n",
	      faelle > 0 ? "aktiv" : (faelle < 0 ? "DURCHGEFALLEN" : "nicht gebaut"));

	ULONG puffer = 0, zu_spaet = 0, noten = 0, spitze = 0;
	ULONG verdraengt = 0, ohne = 0, geklemmt = 0;
	LONG  runde;
	int   alle_runden = 1;

	for(runde = 1; runde <= runden; runde++)
	{
		/* Der AHI-Weg laeuft ueber den Kontext der KLANGEFFEKTE -- in
		 * der Engine oeffnet den Mix_OpenAudio. Die Sonde bindet die
		 * Engine nicht, also muss sie ihn selbst aufmachen. Fehlte er,
		 * schlug zod_audio_stream_open fehl, und der Mitschnitt meldete
		 * nur "Ausgabe liess sich nicht oeffnen" -- was wie ein Fehler
		 * des Wiederaufbaus aussah und keiner war. */
		if(weg != MUSIK_PAULA && !zod_audio_open(1, 22050))
		{
			melde("Runde %ld: AHI-Kontext liess sich nicht oeffnen\n",
			      (long)runde);
			alle_runden = 0;
			break;
		}

		if(!musik_start(weg, bank, bl, (ULONG)rate, 24))
		{
			melde("Runde %ld: Ausgabe liess sich nicht oeffnen\n",
			      (long)runde);
			alle_runden = 0;
			break;
		}

		musik_spiele(zmu, zl, 1);

		melde("Runde %ld: laeuft ueber %s -- %ld s\n", (long)runde,
		      musik_weg() == MUSIK_PAULA ? "Paula" : "AHI", (long)sekunden);

		Delay(sekunden * 50);

		musik_zahlen(&puffer, &zu_spaet);
		mischer_zahlen(&noten, &spitze, &verdraengt, &ohne, &geklemmt);

		melde("Runde %ld: Puffer %ld (erwartet rund %ld), Noten %ld,"
		      " Spitze %ld\n",
		      (long)runde, (long)puffer, (long)(sekunden * 8),
		      (long)noten, (long)spitze);

		/* VOLLSTAENDIG abgeben: Process beenden, bei Paula den
		 * Interrupt-Vektor zurueck und die Kanaele freigeben. Genau
		 * das muss ein Videoabspieler vorfinden. */
		musik_stop();

		if(weg != MUSIK_PAULA) zod_audio_close();

		/* Zu wenig Nachschub heisst Stottern -- als Zahl sichtbar. */
		if(puffer * 2 < (ULONG)(sekunden * 8))
		{
			melde("Runde %ld: ZU WENIG PUFFER\n", (long)runde);
			alle_runden = 0;
		}
	}

	/* Ein Puffer je Achtelsekunde. Kommen deutlich weniger an, hat der
	 * Nachschub nicht getragen -- das waere als Stottern hoerbar. */
	int gut = alle_runden && (ohne == 0);

	{
		const char *marke = gut ? "[OK] abspiel\n" : "[FAIL] abspiel siehe Meldungen\n";
		BPTR aus = Output();
		if(aus) Write(aus, (APTR)marke, (LONG)strlen(marke));
		if(bericht) Write(bericht, (APTR)marke, (LONG)strlen(marke));
	}

	if(bericht) Close(bericht);

	FreeVec(zmu);
	FreeVec(bank);

	timer_zu();

	if(gut) dbg_ok("abspiel");
	else    dbg_fail("abspiel siehe Meldungen");

	return gut ? 0 : 1;
}
