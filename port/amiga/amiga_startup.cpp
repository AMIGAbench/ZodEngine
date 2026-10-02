#include "amiga_startup.h"
#include <cstdlib>

#ifdef __amigaos__

#include <dos/dos.h>
#include <devices/timer.h>
#include <proto/dos.h>
#include <proto/exec.h>

#include "zod_log.h"

static BPTR old_dir = 0;
static int have_old_dir = 0;

/* timer.device: GetSysTime braucht TimerBase. Bisher hat SDL diese Basis
 * geoeffnet -- seit der eigene Unterbau SDL ersetzt, machen wir es selbst. */
struct Device *TimerBase = 0;

static struct MsgPort *timer_port = 0;
static struct timerequest *timer_req = 0;

static void timer_open(void)
{
	timer_port = CreateMsgPort();

	if(!timer_port) return;

	timer_req = (struct timerequest*)CreateIORequest(timer_port, sizeof(struct timerequest));

	if(!timer_req)
	{
		DeleteMsgPort(timer_port);
		timer_port = 0;
		return;
	}

	if(OpenDevice((CONST_STRPTR)"timer.device", UNIT_MICROHZ,
	              (struct IORequest*)timer_req, 0) == 0)
		TimerBase = timer_req->tr_node.io_Device;
}

static void timer_close(void)
{
	if(TimerBase)
	{
		CloseDevice((struct IORequest*)timer_req);
		TimerBase = 0;
	}

	if(timer_req) { DeleteIORequest((struct IORequest*)timer_req); timer_req = 0; }
	if(timer_port) { DeleteMsgPort(timer_port); timer_port = 0; }
}

/* libnix: der Startcode (___initlocale) setzt das Dezimalzeichen auf das
 * des System-Locales -- auf deutschen Systemen ','. atof/scanf lesen dann
 * "0.175676" als 0 und printf schreibt "0,175676". Die Einstellungsdateien
 * (und alles, was mit PC-Zod getauscht wird) verwenden aber '.'. */
extern "C" char *__decimalpoint;

/* WIEVIEL STAPEL HABEN WIR WIRKLICH?
 *
 * Auf AmigaOS legt der AUFRUFER den Stapel fest, nicht das Programm:
 *
 *   ZodLaunch          262144 (NP_StackSize, zodlaunch.c)
 *   Shell              was die Shell hat -- Vorgabe 4096
 *   Workbench          was im .info steht
 *
 * libnix kennt KEIN `__stack` (nachgesehen: null Treffer in libnix*.a), der
 * Startcode vergroessert also nichts von sich aus -- anders als SAS/C. Und
 * ohne MMU ist ein Ueberlauf ein STILLER Schreibzugriff irgendwohin; man
 * sieht ihn nicht als Absturz an der Ursache, sondern als Unsinn viel
 * spaeter. Genau diese Klasse hat hier schon zugeschlagen (jvplay mit dem
 * Shell-Vorgabestapel).
 *
 * Deshalb steht die Zahl ab jetzt im Protokoll. Das ist KEINE Absicherung,
 * sondern eine Messung -- aber ohne sie ist "es beendet sich einfach" von
 * einem zu kleinen Stapel nicht zu unterscheiden, und der Fehlerbericht
 * eines Nutzers erlaubt keine Rueckfrage in Echtzeit.
 *
 * Gelesen wird tc_SPUpper/tc_SPLower, nicht pr_StackSize: die beiden
 * beschreiben den Bereich, auf dem wir TATSAECHLICH stehen. pr_StackSize
 * gilt fuer den CLI-Prozess und weicht ab, wenn uns jemand ueber
 * CreateNewProc gestartet hat -- also im Normalfall dieses Spiels. */
static void stapel_melden(void)
{
	struct Task *me = FindTask(0);
	unsigned long spanne, frei;

	if(!me) return;

	spanne = (unsigned long)((char *)me->tc_SPUpper - (char *)me->tc_SPLower);

	/* Abstand vom jetzigen Stapelzeiger bis zur unteren Grenze. Eine
	 * automatische Variable liegt auf dem Stapel, ihre Adresse ist also
	 * eine brauchbare Schaetzung des Zeigers -- ohne Assembler. */
	frei = (unsigned long)((char *)&spanne - (char *)me->tc_SPLower);

	ZLOG("Stapel: %lu Byte, davon %lu noch frei\n", spanne, frei);

	/* Die Schwelle ist NICHT geraten: 262144 laeuft belegt durch (V1200,
	 * 32 Minuten Dauertest unter MuForce, 15 Kartenwechsel), und ein Lauf
	 * mit 1000000 aenderte nichts. Unter 65536 hat es nie jemand
	 * ausprobiert -- deshalb warnt es dort, statt etwas zu behaupten. */
	if(spanne < 65536)
		ZLOG("Stapel: WARNUNG -- unter 65536 Byte ungeprueft."
		     " Ueber ZodLaunch starten (setzt 262144).\n");
}

void zod_amiga_startup(void)
{
	__decimalpoint = (char *)".";

	stapel_melden();

	timer_open();

	/* Liegen die Daten schon im aktuellen Verzeichnis, bleibt es dabei --
	 * sonst wuerde ein vom Aufrufer gesetztes CD (Autostart, Shell) wieder
	 * aufgehoben. Nur wenn hier nichts zu finden ist (typisch beim Start von
	 * der Workbench), wird auf das Verzeichnis der Programmdatei gewechselt. */
	BPTR here = Lock("assets", SHARED_LOCK);

	if(here)
	{
		UnLock(here);
		return;
	}

	BPTR prog_dir = Lock("PROGDIR:", SHARED_LOCK);

	if(!prog_dir)
	{
		ZLOG("PROGDIR: nicht sperrbar -- arbeite im aktuellen Verzeichnis\n");
		return;
	}

	old_dir = CurrentDir(prog_dir);
	have_old_dir = 1;
}

void zod_amiga_shutdown(void)
{
	timer_close();

	if(!have_old_dir) return;

	/* eigenes Lock freigeben und das alte Verzeichnis zuruecksetzen */
	BPTR prog_dir = CurrentDir(old_dir);

	if(prog_dir) UnLock(prog_dir);

	have_old_dir = 0;
	old_dir = 0;
}

int zod_amiga_break(void)
{
	return (SetSignal(0L, SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C) ? 1 : 0;
}

static unsigned long mem_at_start = 0;

void zod_mem_report(const char *when)
{
	unsigned long total = AvailMem(MEMF_ANY);
	unsigned long largest = AvailMem(MEMF_ANY | MEMF_LARGEST);

	if(!mem_at_start) mem_at_start = total;

	ZLOG("Speicher %s: %lu KB frei, groesster Block %lu KB\n",
	     when ? when : "", total / 1024, largest / 1024);
}

void zod_mem_report_exit(void)
{
	unsigned long total = AvailMem(MEMF_ANY);

	ZLOG("Speicher Ende: %lu KB frei (Start: %lu KB, Differenz: %ld KB)\n",
	     total / 1024, mem_at_start / 1024,
	     ((long)total - (long)mem_at_start) / 1024);
}

void *zod_big_alloc(unsigned long size)
{
	return AllocVec(size, MEMF_ANY);
}

void zod_big_free(void *block)
{
	if(block) FreeVec(block);
}

#else /* andere Plattformen */

void zod_amiga_startup(void) {}
void zod_amiga_shutdown(void) {}
int zod_amiga_break(void) { return 0; }
void zod_mem_report(const char *) {}
void zod_mem_report_exit(void) {}
void *zod_big_alloc(unsigned long size) { return malloc(size); }
void zod_big_free(void *block) { free(block); }

#endif

const char *zod_env(const char *name, char *buf, int size)
{
	if(!name || !buf || size <= 0) return 0;

	buf[0] = 0;

#ifdef __amigaos__
	if(GetVar((CONST_STRPTR)name, (STRPTR)buf, size, 0) > 0 && buf[0])
		return buf;

	/* Manche Umgebungen setzen sie doch klassisch -- dann gilt das. */
	{
		const char *e = getenv(name);

		if(e && *e)
		{
			int i = 0;

			while(e[i] && i < size - 1) { buf[i] = e[i]; i++; }
			buf[i] = 0;

			return buf;
		}
	}

	return 0;
#else
	{
		const char *e = getenv(name);

		if(!e || !*e) return 0;

		int i = 0;

		while(e[i] && i < size - 1) { buf[i] = e[i]; i++; }
		buf[i] = 0;

		return buf;
	}
#endif
}
