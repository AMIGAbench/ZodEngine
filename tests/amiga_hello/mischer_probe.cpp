/*
 * mischer_probe -- was kostet der eigene Musikmischer auf DIESER Maschine?
 *
 * Kein AHI, kein Paula, keine Ausgabe. Die Sonde mischt eine bekannte Menge
 * Musik in einen Puffer und misst, wie lange sie dafuer braucht. Das
 * Verhaeltnis ist die Antwort:
 *
 *     10 s Musik in 1,2 s gemischt  ->  120 Promille Rechenlast
 *
 * WARUM SO UND NICHT ALS ZAEHLSCHLEIFE: Die erste Sonde (music_probe) hat
 * die Last als Differenz zweier Zaehlschleifen gemessen. Das misst bei
 * hoher Last vor allem Rauschen -- die Stufen kamen nicht monoton heraus
 * (16 Kanaele angeblich teurer als 32), und der Zaehler lief ueber 2^32.
 * Hier gibt es nichts zu vergleichen: gemessen wird unmittelbar die Zeit
 * fuer eine feste Arbeit.
 *
 * Aufruf:
 *   mischer_probe <bank> <stueck.zmu> [sekunden] [berichtdatei]
 */
#include <exec/types.h>
#include <exec/memory.h>
#include <devices/timer.h>
#include <dos/dos.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/timer.h>

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "../../platform/amiga/debug.h"
#include "../../port/amiga/musik_mischer.h"

struct Device *TimerBase = 0;

static struct MsgPort *timer_port = 0;
static struct timerequest *timer_req = 0;
static BPTR bericht_datei = 0;

/* Formatiert mit vsnprintf und gibt als reines %s weiter -- sprintf kommt
 * auf dieser Werkzeugkette aus libamiga.a und liest %d als 16 Bit. */
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

static ULONG jetzt_us(void)
{
	struct timeval tv;

	GetSysTime(&tv);

	return (ULONG)(tv.tv_secs * 1000000UL + tv.tv_micro);
}

int main(int argc, char **argv)
{
	dbg_boot();

	if(argc < 3)
	{
		dbg_fail("mischer Aufruf: mischer_probe <bank> <stueck.zmu> [sekunden]");
		return 1;
	}

	LONG sekunden = 10;

	if(argc > 3)
	{
		sekunden = 0;
		for(const char *p = argv[3]; *p >= '0' && *p <= '9'; p++)
			sekunden = sekunden * 10 + (*p - '0');
		if(sekunden < 1) sekunden = 10;
	}

	bericht_datei = Open((CONST_STRPTR)(argc > 4 ? argv[4] : "mischer_probe.txt"),
	                     MODE_NEWFILE);

	timer_port = CreateMsgPort();
	timer_req = timer_port
	          ? (struct timerequest *)CreateIORequest(timer_port, sizeof(struct timerequest))
	          : 0;

	if(!timer_req || OpenDevice((CONST_STRPTR)TIMERNAME, UNIT_MICROHZ,
	                            (struct IORequest *)timer_req, 0))
	{
		dbg_fail("mischer timer.device nicht zu oeffnen");
		return 1;
	}

	TimerBase = timer_req->tr_node.io_Device;

	int gut_asm = 1;
	ULONG bl = 0, zl = 0;
	UBYTE *bank = datei_lesen(argv[1], &bl);
	UBYTE *zmu  = datei_lesen(argv[2], &zl);

	if(!bank || !zmu)
	{
		melde("Datei nicht lesbar: %s bzw. %s\n", argv[1], argv[2]);
		dbg_fail("mischer Datei fehlt");
		return 1;
	}

	melde("mischer_probe -- Bank %s (%ld Byte), Stueck %s, %ld s je Stufe\n",
	      argv[1], (long)bl, argv[2], (long)sekunden);

	/* Der Assemblerkern laeuft erst nach bestandenem Selbsttest. Ohne
	 * diese Zeile waere "ASM gebaut" von "ASM benutzt" nicht zu
	 * unterscheiden -- der Bau hat den Kern schon einmal stillschweigend
	 * weggelassen und trotzdem BUILD OK gemeldet. */
	{
		int faelle = mischer_selbsttest();

		if(faelle > 0)
			melde("Assemblerkern: Selbsttest bestanden, %ld Faelle,"
			      " Kern ist AKTIV\n", (long)faelle);
		else if(faelle < 0)
		{
			melde("Assemblerkern: Selbsttest DURCHGEFALLEN bei Fall %ld"
			      " -- es laeuft die C-Fassung\n", (long)(-faelle));
			gut_asm = 0;
		}
		else
			melde("Assemblerkern: nicht gebaut, es laeuft die C-Fassung\n");
	}

	/* Ausgabepuffer: ein halbe Sekunde reicht, er wird wiederverwendet.
	 * Es geht um die Rechenzeit, nicht um den Inhalt. */
	const ULONG block = 512;
	WORD *puffer = (WORD *)AllocVec(block * 2 * sizeof(WORD), MEMF_ANY);

	if(!puffer)
	{
		dbg_fail("mischer kein Speicher fuer den Mischpuffer");
		return 1;
	}

	struct Stufe { ULONG rate; int stimmen; };

	static const Stufe plan[] = {
		{ 11025, 24 },     /* Vorgabe */
		{ 11025, 16 },
		{ 22050, 24 },
		{ 28867, 24 },
		{ 11025, 24 }      /* KONTROLLE: noch einmal Stufe 1 */
	};

	const int stufen = (int)(sizeof(plan) / sizeof(plan[0]));
	long ergebnis[8];
	int gut = 1;

	for(int i = 0; i < stufen; i++)
	{
		if(!mischer_init(bank, bl, plan[i].rate, plan[i].stimmen))
		{
			melde("Bank unbrauchbar\n");
			dbg_fail("mischer Bank unbrauchbar");
			return 1;
		}

		if(!mischer_stueck(zmu, zl))
		{
			melde("Stueck unbrauchbar\n");
			dbg_fail("mischer Stueck unbrauchbar");
			return 1;
		}

		ULONG rahmen_gesamt = plan[i].rate * (ULONG)sekunden;
		ULONG getan = 0;

		ULONG t0 = jetzt_us();

		while(getan < rahmen_gesamt)
		{
			ULONG r = rahmen_gesamt - getan;

			if(r > block) r = block;

			mischer_fuellen(puffer, r);
			getan += r;
		}

		ULONG t1 = jetzt_us();
		ULONG dauer = t1 - t0;

		ULONG noten, spitze, verdraengt, ohne, geklemmt;
		mischer_zahlen(&noten, &spitze, &verdraengt, &ohne, &geklemmt);

		/* Promille der Echtzeit: gebrauchte Zeit / gemischte Spielzeit */
		long promille = (long)((unsigned long long)dauer * 1000
		                       / ((unsigned long long)sekunden * 1000000UL));

		ergebnis[i] = promille;

		melde("%ld Hz, %ld Stimmen: %ld Promille (%ld us fuer %ld s Musik), "
		      "%ld Noten, Spitze %ld, %ld verdraengt, %ld ohne Instrument\n",
		      (long)plan[i].rate, (long)plan[i].stimmen, promille,
		      (long)dauer, (long)sekunden,
		      (long)noten, (long)spitze, (long)verdraengt, (long)ohne);

		if(ohne) gut = 0;
		if(!gut_asm) gut = 0;
	}

	/* Ohne Kontrolle ist kein Unterschied deutbar -- dieselbe Regel wie bei
	 * den Host-Laeufen: die Streuung der Wiederholung ist das Kriterium. */
	long d = ergebnis[0] > ergebnis[stufen - 1]
	       ? ergebnis[0] - ergebnis[stufen - 1]
	       : ergebnis[stufen - 1] - ergebnis[0];

	melde("KONTROLLE: Stufe 1 %ld gegen Wiederholung %ld Promille, Streuung %ld\n",
	      ergebnis[0], ergebnis[stufen - 1], d);

	/* Unter 20 Promille ist die Streuung die Aufloesungsgrenze der Messung,
	 * kein Befund. Im Emulator meldete die Sonde sonst "Streuung ueber
	 * 20 %", weil 2 gegen 1 Promille standen -- eine Warnung, die nur
	 * Rauschen anzeigt, macht die echte Warnung wertlos. */
	if(ergebnis[0] < 20)
		melde("Hinweis: unter 20 Promille -- das ist die Aufloesungsgrenze"
		      " dieser Messung, nicht der Preis des Mischers.\n");
	else if(d * 5 > ergebnis[0])
		melde("WARNUNG: Streuung ueber 20 %% -- Unterschiede zwischen den"
		      " Stufen sind nicht deutbar.\n");

	{
		const char *marke = gut ? "[OK] mischer\n" : "[FAIL] mischer siehe Meldungen\n";
		BPTR aus = Output();

		if(aus) Write(aus, (APTR)marke, (LONG)strlen(marke));
		if(bericht_datei) Write(bericht_datei, (APTR)marke, (LONG)strlen(marke));
	}

	FreeVec(puffer);
	FreeVec(zmu);
	FreeVec(bank);

	if(bericht_datei) Close(bericht_datei);

	CloseDevice((struct IORequest *)timer_req);
	DeleteIORequest((struct IORequest *)timer_req);
	DeleteMsgPort(timer_port);

	if(gut) dbg_ok("mischer");
	else    dbg_fail("mischer siehe Meldungen");

	return gut ? 0 : 1;
}
