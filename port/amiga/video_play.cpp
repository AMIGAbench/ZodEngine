/*
 * Zwischensequenzen ueber den externen JVPlayer (jvplay).
 *
 * Die Begruendung fuer den Entwurf steht in port/zod_video.h. Hier stehen
 * die Dinge, die man auf AmigaOS falsch machen kann -- und jede davon ist
 * in diesem Projekt schon einmal falsch gemacht worden:
 *
 *  - STAPEL. jvplay stuerzt mit dem Vorgabestapel einer Shell ab (vom
 *    Nutzer gemeldet). SystemTags reicht unbekannte Tags an CreateNewProc
 *    weiter; NP_StackSize steht ausdruecklich NICHT auf der Liste der
 *    herausgefilterten Tags (dos.doc/SystemTagList). Also wird er hier
 *    gesetzt, gross und ausdruecklich.
 *
 *  - DATEIGRIFFE. "If System() waits for completion, the input and output
 *    filehandles will not be closed by System, you must close them"
 *    (dos.doc). Also selbst schliessen -- sonst leckt jeder Film zwei
 *    Griffe. Und Ein- und Ausgabe duerfen NICHT derselbe Griff sein, das
 *    steht dort ebenfalls.
 *
 *  - NOSER. Der Player schreibt "[OK] jvplay" auf den seriellen Kanal, und
 *    genau auf "[OK]" wartet tools/run.sh als Schlussmarke. Ohne NOSER
 *    wuerde ein Emulatorlauf beim ersten Film beendet, und der Mitschnitt
 *    saehe wie ein erfolgreicher Lauf aus. Was wir wissen muessen,
 *    protokollieren wir selbst.
 *
 *  - DER TON. Vollstaendig freigeben, nicht teilen (siehe zod_video.h).
 *
 *  - DIE TASTE. ESC beendet den Film. Genau dieselbe Taste oeffnet im
 *    Spiel die Ruecktrage "wirklich beenden?". Bliebe sie im Puffer
 *    stehen, saehe der Nutzer nach jedem abgebrochenen Film diese Frage.
 *    Deshalb wird nach der Rueckkehr geleert.
 */

#include "zod_video.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "zod_log.h"

#ifdef __amigaos__
#include <proto/exec.h>
#include <proto/dos.h>
#include <dos/dos.h>
#include <dos/dostags.h>
#endif

/* Reichlich bemessen und BEWUSST eine benannte Konstante: wenn der Player
 * spaeter doch noch abstuerzt, wird genau hier gedreht, und der Wert steht
 * im Protokoll. Die Testumgebung des Players selbst faehrt mit 65536; wir
 * geben das Dreifache, weil ein zu kleiner Stapel auf AmigaOS nicht
 * gemeldet, sondern gefahren wird. */
#define ZOD_VIDEO_STAPEL 200000

namespace
{

char verz[256]   = "";
int  breit_px    = 640;
int  ton_weg     = 1;          /* 0 stumm, 1 AHI, 2 Paula */
int  aga_weg     = 0;
int  gemeldet    = 0;          /* fehlenden Player nur EINMAL melden */

/* Vollen Pfad einer Sequenz bauen. AddPart waere hier richtiger, steht auf
 * dem Host aber nicht zur Verfuegung -- und ein Verzeichnisname endet in
 * diesem Projekt nie auf einem Trenner, weil er aus der Befehlszeile kommt.
 * Deshalb der einfache Weg MIT Behandlung des Volume-Trenners: nach einem
 * ':' gehoert kein '/' dazwischen. */
void pfad_bauen(char *aus, int platz, const char *name)
{
	int n = (int)strlen(verz);
	char trenn = (n > 0 && (verz[n-1] == ':' || verz[n-1] == '/')) ? 0 : '/';

	if(trenn)
		snprintf(aus, platz, "%s/%s.jv", verz, name);
	else
		snprintf(aus, platz, "%s%s.jv", verz, name);
}

}

extern "C" {

void zod_video_setup(const char *verzeichnis, int breit, int ton, int aga)
{
	verz[0] = 0;

	if(verzeichnis && *verzeichnis)
	{
		strncpy(verz, verzeichnis, sizeof(verz) - 1);
		verz[sizeof(verz) - 1] = 0;
	}

	breit_px = breit > 0 ? breit : 640;
	ton_weg  = ton;
	aga_weg  = aga;
	gemeldet = 0;

	if(!verz[0])
	{
		ZLOG("Video: aus (kein -V) -- das Spiel laeuft wie ohne Filme\n");
		return;
	}

	ZLOG("Video: Verzeichnis %s, Bild %s%s, Ton %s\n",
	     verz,
	     aga_weg ? "AGA" : "RTG",
	     (!aga_weg && breit_px >= 640) ? " doppelt" : "",
	     ton_weg == 0 ? "stumm" : (ton_weg == 2 ? "Paula" : "AHI"));
}

int zod_video_an(void)
{
	return verz[0] ? 1 : 0;
}

int zod_video_da(const char *name)
{
	char pfad[320];
	FILE *f;

	if(!verz[0] || !name || !*name) return 0;

	pfad_bauen(pfad, sizeof(pfad), name);

	f = fopen(pfad, "rb");

	if(!f) return 0;

	fclose(f);

	return 1;
}

#ifdef __amigaos__

/* Aus port/amiga/sdl_addons.cpp -- gibt die Tonhardware ganz frei und
 * setzt sie danach genau so wieder auf. */
extern void zod_ton_anhalten(void);
extern void zod_ton_fortsetzen(void);

/* Aus port/amiga/sdl_screen.cpp -- holt den Spielschirm zurueck nach vorn
 * und wirft liegengebliebene Tastendruecke weg. */
extern void zod_schirm_zurueck(void);

int zod_video_play_folge(const char * const *namen, int anzahl)
{
	char befehl[1024];
	int laenge, i, dabei = 0;
	BPTR ein, aus;
	LONG rc;

	if(!verz[0] || !namen || anzahl <= 0) return 0;

	/* Der Player liegt neben dem Spiel, und angegeben wird NUR SEIN NAME.
	 *
	 * Hier stand zuerst "PROGDIR:jvplay". Das war falsch und ist vom
	 * Nutzer gemeldet worden: PROGDIR: ist PROZESSLOKAL (pr_HomeDir), und
	 * die Shell, die SystemTags erzeugt, ist ein anderer Prozess. Das
	 * Spiel laeuft ohnehin mit PROGDIR: als Arbeitsverzeichnis, und die
	 * Autodoc sagt ausdruecklich: "The current directory and path will be
	 * inherited from your process."
	 *
	 * Genau dieselbe Lehre gilt fuer die
	 * Binaerauswahl im Launcher: "Gespeichert wird nur der NAME, kein
	 * Pfad." Ich habe sie hier gebrochen. */
	{
		FILE *f = fopen("jvplay", "rb");

		if(!f)
		{
			if(!gemeldet)
			{
				ZLOG("Video: jvplay fehlt neben dem Spiel -- keine Filme\n");
				gemeldet = 1;
			}

			return 0;
		}

		fclose(f);
	}

	/* EIN Aufruf fuer die ganze Folge. Je Aufruf oeffnet und schliesst der
	 * Player seinen Schirm; drei Aufrufe waeren drei Modewechsel mit dem
	 * Spielschirm dazwischen. */
	laenge = snprintf(befehl, sizeof(befehl), "jvplay");

	for(i = 0; i < anzahl; i++)
	{
		char pfad[320];

		if(!namen[i] || !*namen[i]) continue;

		if(!zod_video_da(namen[i]))
		{
			pfad_bauen(pfad, sizeof(pfad), namen[i]);
			ZLOG("Video: %s fehlt -- uebersprungen\n", pfad);
			continue;
		}

		pfad_bauen(pfad, sizeof(pfad), namen[i]);

		/* Passt der naechste Name nicht mehr, lieber hier abbrechen als
		 * eine halb abgeschnittene Befehlszeile abzuschicken. */
		if(laenge + (int)strlen(pfad) + 4 >= (int)sizeof(befehl))
		{
			ZLOG("Video: Befehlszeile voll, %s und weitere ausgelassen\n", pfad);
			break;
		}

		laenge += snprintf(befehl + laenge, sizeof(befehl) - laenge,
		                   " \"%s\"", pfad);
		dabei++;
	}

	if(!dabei) return 0;            /* kein einziger Film vorhanden */

	snprintf(befehl + laenge, sizeof(befehl) - laenge, " %s %s%s NOSER",
	         aga_weg ? "AGA" : "RTG",
	         ton_weg == 0 ? "NOSOUND" : (ton_weg == 2 ? "PAULA" : "AHI"),
	         (!aga_weg && breit_px >= 640) ? " DOUBLE" : "");

	ZLOG("Video: %s\n", befehl);

	/* Ton VOR dem Start freigeben. Erst danach darf der Player AHI oder
	 * audio.device anfassen. */
	zod_ton_anhalten();

	/* Ein- und Ausgabe muessen VERSCHIEDENE Griffe sein (dos.doc). Beide
	 * nach NIL:, damit die Ausgabe des Players nicht im Spielfenster oder
	 * in der Startshell landet. */
	ein = Open((CONST_STRPTR)"NIL:", MODE_OLDFILE);
	aus = Open((CONST_STRPTR)"NIL:", MODE_NEWFILE);

	/* Auf AmigaOS ist die serielle Ausgabe SYNCHRON. Die letzte Zeile vor
	 * einem Haenger benennt deshalb den Schritt, der ihn ausgeloest hat --
	 * und genau daran ist der erste Abspielversuch gescheitert: der
	 * Mitschnitt endete bei "Ton: freigegeben", und damit war nicht zu
	 * unterscheiden, ob Open oder SystemTags haengt. Jetzt schon. */
	ZLOG("Video: NIL: ein=%ld aus=%ld, starte Player (%ld Filme, Stapel %ld)\n",
	     (long)ein, (long)aus, (long)dabei, (long)ZOD_VIDEO_STAPEL);

	rc = SystemTags((CONST_STRPTR)befehl,
	                SYS_Input,     (ULONG)ein,
	                SYS_Output,    (ULONG)aus,
	                SYS_Asynch,    FALSE,
	                NP_StackSize,  (ULONG)ZOD_VIDEO_STAPEL,
	                TAG_DONE);

	ZLOG("Video: Player zurueck, rc=%ld\n", (long)rc);

	/* Synchron: WIR schliessen sie. */
	if(ein) Close(ein);
	if(aus) Close(aus);

	zod_ton_fortsetzen();

	zod_schirm_zurueck();

	if(rc < 0)
	{
		ZLOG("Video: Player liess sich nicht starten (rc=%ld)\n", (long)rc);
		return 0;
	}

	return 1;
}

#else

int zod_video_play_folge(const char * const *namen, int anzahl)
{
	/* Auf dem Host gibt es keinen Player. Die Meldung steht trotzdem hier,
	 * damit ein Host-Lauf zeigt, WANN welche Folge gekommen waere -- das
	 * ist der billigste Weg, die Ausloeser zu pruefen, ohne einen Amiga. */
	int i;

	if(!verz[0] || !namen) return 0;

	for(i = 0; i < anzahl; i++)
		if(namen[i] && *namen[i])
			ZLOG("Video: hier liefe %s%s\n", namen[i],
			     zod_video_da(namen[i]) ? "" : " (Datei fehlt)");

	return 0;
}

#endif

/* Ein einzelner Film ist eine Folge der Laenge 1. Bewusst KEINE zweite
 * Umsetzung: sonst haetten Einzelfilm und Folge zwei Wege zur Hardware,
 * die auseinanderlaufen koennen. */
int zod_video_play(const char *name)
{
	const char *eins[1];

	eins[0] = name;

	return zod_video_play_folge(eins, 1);
}

}
