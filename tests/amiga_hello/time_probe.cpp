/*
 * time_probe -- stimmt die Zeitquelle der Engine auf dieser Maschine?
 *
 * Warum es diese Sonde gibt: Bis zum 18.09. hat SDL auf dem Amiga
 * timer.device geoeffnet. Seit die eigene Grafikschicht SDL ersetzt, macht das
 * port/amiga/amiga_startup.cpp selbst. Die Engine misst ALLES mit
 * COMMON::current_time(), und das ist gettimeofday() -- also genau die
 * Funktion, die davon abhaengt.
 *
 * Steht die Spielzeit, bewegt sich nichts, waehrend Zeichnen und Auswahl
 * weiterlaufen. Genau so sah es auf der V1200 aus, und im Emulator nicht.
 *
 * Die Sonde misst vier Quellen nebeneinander:
 *
 *   gettimeofday()   das, was die Engine benutzt (libnix)
 *   GetSysTime()     timer.device unmittelbar, ueber unsere eigene Basis
 *   DateStamp()      dos.library, 1/50 s, braucht kein Device
 *   Delay()          als bekannte Referenzdauer
 *
 * Erwartet wird: Alle drei Uhren laufen um dieselbe Spanne weiter, und die
 * Aufloesung von gettimeofday ist besser als eine Sekunde.
 *
 *   [OK] time   alles in Ordnung
 *   [FAIL] time <was nicht stimmt>
 */
#include <exec/types.h>
#include <devices/timer.h>
#include <dos/dos.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/timer.h>

#include <sys/time.h>
#include <stdio.h>

#include "debug.h"

struct Device *TimerBase = 0;

static struct MsgPort *timer_port = 0;
static struct timerequest *timer_req = 0;

/* Genau derselbe Weg wie in port/amiga/amiga_startup.cpp -- wenn er dort
 * scheitert, scheitert er hier auch, und das ist die Aussage. */
static int timer_open(void)
{
	timer_port = CreateMsgPort();

	if(!timer_port) return 0;

	timer_req = (struct timerequest*)CreateIORequest(timer_port,
	                                                 sizeof(struct timerequest));

	if(!timer_req) return 0;

	if(OpenDevice((CONST_STRPTR)"timer.device", UNIT_MICROHZ,
	              (struct IORequest*)timer_req, 0) != 0)
		return 0;

	TimerBase = timer_req->tr_node.io_Device;

	return 1;
}

static void timer_close(void)
{
	if(TimerBase) { CloseDevice((struct IORequest*)timer_req); TimerBase = 0; }
	if(timer_req) DeleteIORequest((struct IORequest*)timer_req);
	if(timer_port) DeleteMsgPort(timer_port);
}

/* Wortgleich mit COMMON::current_time() (common.cpp), damit hier wirklich das
 * gemessen wird, was die Engine tut -- und nicht etwas Aehnliches. */
static double current_time(void)
{
	static long first_sec = 0;
	static long first_usec = 0;
	struct timeval t;

	gettimeofday(&t, 0);

	if(!first_sec)
	{
		first_sec = t.tv_sec;
		first_usec = t.tv_usec;
	}

	return (t.tv_sec - first_sec) + ((t.tv_usec - first_usec) * 0.000001);
}

static long ds_ms(void)
{
	struct DateStamp ds;

	DateStamp(&ds);

	return (long)ds.ds_Minute * 60000L + (long)ds.ds_Tick * 1000L / 50L;
}

int main(void)
{
	const int haben_timer = timer_open();
	struct timeval sys0, sys1;
	double c0, c1;
	long d0, d1;
	int i;
	int schritte = 0;
	double letzte;
	int fehler = 0;

	dbg_boot();
	printf("time_probe -- Zeitquelle pruefen\n\n");

	printf("timer.device: %s\n", haben_timer ? "geoeffnet" : "NICHT ZU OEFFNEN");

	if(!haben_timer) fehler++;

	/* --- 1. Aufloesung: wie oft aendert sich current_time in 200 Schritten? */
	letzte = current_time();

	for(i = 0; i < 200; i++)
	{
		double jetzt = current_time();

		if(jetzt != letzte) { schritte++; letzte = jetzt; }
	}

	printf("Aufloesung: %d von 200 Abfragen lieferten einen neuen Wert\n",
	       schritte);

	/* --- 2. Laeuft die Uhr ueberhaupt? Eine Sekunde nach Delay(50). ------ */
	c0 = current_time();
	d0 = ds_ms();

	if(haben_timer) GetSysTime(&sys0);

	Delay(50);                      /* 50 Ticks = 1 Sekunde */

	c1 = current_time();
	d1 = ds_ms();

	if(haben_timer) GetSysTime(&sys1);

	printf("\nnach Delay(50), also einer Sekunde:\n");
	printf("  current_time (gettimeofday): %ld ms\n",
	       (long)((c1 - c0) * 1000.0));
	printf("  DateStamp:                   %ld ms\n", d1 - d0);

	if(haben_timer)
	{
		long us = (long)(sys1.tv_secs - sys0.tv_secs) * 1000000L
		        + (long)(sys1.tv_micro - sys0.tv_micro);

		printf("  GetSysTime (timer.device):   %ld ms\n", us / 1000L);

		if(us < 800000L || us > 1300000L) fehler++;
	}

	/* Die Engine braucht gettimeofday. Bleibt es stehen, steht das Spiel. */
	if((c1 - c0) < 0.8 || (c1 - c0) > 1.3)
	{
		printf("\n  >>> gettimeofday laeuft NICHT richtig weiter.\n");
		printf("  >>> Damit steht die Spielzeit: nichts bewegt sich,\n");
		printf("  >>> waehrend Zeichnen und Auswahl weiterlaufen.\n");
		fehler++;
	}

	if(d1 - d0 < 800 || d1 - d0 > 1300)
	{
		printf("\n  >>> Auch DateStamp ist auffaellig.\n");
		fehler++;
	}

	if(schritte < 2)
	{
		printf("\n  >>> current_time aendert sich fast nie -- zu grob\n");
		printf("  >>> oder eingefroren.\n");
		fehler++;
	}

	timer_close();

	printf("\n");

	if(fehler)
	{
		dbg_fail("time");
		printf("[FAIL] time: %d Auffaelligkeiten\n", fehler);

		return 10;
	}

	dbg_ok("time");
	printf("[OK] time\n");

	return 0;
}
