/*
 * ZodLaunch -- MUI-3.8-Startprogramm fuer die Zod Engine auf AmigaOS.
 *
 * Zweck: die Befehlszeile des Spiels zusammensetzen, ohne dass sie jemand
 * abtippen muss, und das Spiel asynchron im richtigen Verzeichnis starten.
 *
 * Bewusst NICHT enthalten:
 *  - Bearbeiten von default_settings.txt. Das sind 256 Zeilen Spielbalance
 *    (Schaden, Geschwindigkeit, Bauzeit je Einheit) mit Werten wie 0.001105.
 *    Ein Umweg ueber Fliesskomma und zurueck aendert die Schreibweise und
 *    verschoebe stillschweigend das Spielgleichgewicht. Der Launcher fasst
 *    die Datei nicht an; damit ist der geforderte bytegleiche Round-Trip
 *    trivial erfuellt.
 *  - Map-Editor-Start (P9, noch nicht portiert).
 *
 * Eigene Einstellungen liegen in ZodLaunch.cfg neben dem Programm -- ein
 * schlichtes schluessel=wert-Format, das der Launcher selbst schreibt.
 */

/* ZOD_LAUNCH_TEST klammert alles aus, was MUI, AHI oder exec braucht. Uebrig
 * bleibt die reine Logik (Einstellungen lesen/schreiben, Befehlszeile bauen),
 * und die prueft tests/launcher/launch_logic_test.c auf dem HOST -- der
 * Launcher meldet nichts ueber den seriellen Kanal, ein Emulatorlauf belegt
 * also nur, dass er startet, nicht was er tut. */
#ifndef ZOD_LAUNCH_TEST

#include <exec/types.h>
#include <exec/libraries.h>
#include <dos/dos.h>
#include <dos/dostags.h>
#include <workbench/startup.h>

/* MAKE_ID fuer die Fenster-Kennung (MUIA_Window_ID). Ohne diesen Header
 * erzeugt der Uebersetzer eine implizite Deklaration und damit eine
 * willkuerliche Kennung -- das Fenster merkt sich seine Lage dann falsch. */
#include <libraries/iffparse.h>
#include <libraries/mui.h>

/* DoMethod nimmt veraenderliche Argumente und steht in alib_protos.h. Fehlt
 * die Deklaration, nimmt der Uebersetzer int-Rueckgabe und ungepruefte
 * Argumente an -- auf m68k ein sicherer Absturz beim ersten Knopfdruck. */
#include <clib/alib_protos.h>

#include <devices/ahi.h>
#include <libraries/asl.h>

#include <proto/exec.h>
#include <proto/ahi.h>
#include <proto/asl.h>
#include <proto/dos.h>
#include <proto/intuition.h>

/* muimaster BEWUSST ueber proto/muimaster.h und NICHT ueber
 * <inline/muimaster_lib.h>.
 *
 * Grund (die Ursache des frueheren Uebersetzungsfehlers): Die Inline-Datei
 * macht MUI_NewObject zu einem variadischen MAKRO. Der Praeprozessor sammelt
 * die Argumente eines Makroaufrufs aber VOR jeder weiteren Ersetzung und
 * zaehlt dabei ausschliesslich rohe Klammern. "ApplicationObject" liefert die
 * oeffnende Klammer (MUI_NewObject(MUIC_Application), die schliessende steckt
 * jedoch im Makro "End" (= TAG_DONE) -- und "End" ist beim Einsammeln noch ein
 * unersetzter Bezeichner. Die Argumentliste wird deshalb nie geschlossen; der
 * Uebersetzer laeuft bis zum Dateiende und meldet dort
 * "unterminated argument list invoking macro MUI_NewObject".
 * Die Klammerbilanz der Datei ist dabei in Ordnung -- sie kommt nur nie zum
 * Tragen.
 *
 * proto/muimaster.h setzt NO_INLINE_STDARG/NO_INLINE_VARARGS und laesst damit
 * die echten Funktionsprototypen aus clib/muimaster_protos.h stehen. Die
 * zugehoerigen Stubs liegen in libmui.a der Toolchain (-lmui im Makefile);
 * die frueher gesehene Meldung "undefined reference to MUI_NewObject" kam
 * allein daher, dass diese Bibliothek nicht gebunden wurde. Die Stubs
 * erwarten die Basis unter dem Namen MUIMasterBase, den wir unten genau so
 * fuehren. */
#include <proto/muimaster.h>

#endif /* !ZOD_LAUNCH_TEST */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ------------------------------------------------------------------ */
/* Bibliotheksbasen                                                     */
/* ------------------------------------------------------------------ */
/* utility.library wird hier nicht gebraucht; proto/utility.h deklariert
 * UtilityBase ohnehin selbst, eine eigene Definition waere ein Konflikt. */
struct Library *MUIMasterBase = NULL;

/* ------------------------------------------------------------------ */
/* Auswahllisten                                                        */
/* ------------------------------------------------------------------ */

/* Genau die Namen, die die Engine erwartet: main_options.cpp vergleicht das
 * Argument von -t und -b als Zeichenkette gegen team_type_string[] und meldet
 * sonst "could not find the team '...', perhaps try lowercase?". */
static const char *team_namen[] =
{
    "red", "blue", "green", "yellow", "purple", "teal", "white", "black", NULL
};

static const char *aufloesungen[] =
{
    "640x480", "800x600",
    "1024x768", "1280x720", NULL
};

/* 320x240, 512x384 und 960x540 sind ENTFERNT worden -- vom Nutzer als "viel zu klein
 * und unspielbar" beurteilt. Gefahrlos, weil die Aufloesung als TEXT
 * gespeichert wird: ein altes "aufloesung=320x240" faellt unten auf die
 * Vorgabe 640x480 zurueck. Waere es ein Listenplatz, haette das Entfernen
 * jede bestehende Einstellung stillschweigend umgedeutet. */

/* Die Aufloesung wird als TEXT gespeichert, nicht als Listenplatz.
 *
 * Ein Listenplatz ist als Einstellung eine Falle: sobald die Liste waechst,
 * deutet er stillschweigend etwas anderes. Genau das ist beim
 * Farbtiefen-Eintrag schon einmal passiert (der ist inzwischen ganz weg).
 * Hier waere es schlimmer gewesen -- die vier neuen Modi stehen mitten in der
 * Liste, ein altes "aufloesung=1" haette danach 512x384 bedeutet statt
 * 800x600.
 *
 * Alte Dateien trugen eine EINZELNE Ziffer 0..2 fuer die damalige Liste
 * {640x480, 800x600, 1024x768}. Das ist von den neuen Werten unterscheidbar:
 * die enthalten immer ein 'x'. */
static LONG aufloesung_platz(const char *wert)
{
    int i;

    for(i = 0; aufloesungen[i]; i++)
        if(!strcmp(aufloesungen[i], wert)) return (LONG)i;

    if(wert[0] >= '0' && wert[0] <= '2' && !wert[1])
    {
        static const char *alt[] = { "640x480", "800x600", "1024x768" };

        return aufloesung_platz(alt[wert[0] - '0']);
    }

    /* Unbekannt (handbearbeitet oder aus einer neueren Fassung): Vorgabe. */
    for(i = 0; aufloesungen[i]; i++)
        if(!strcmp(aufloesungen[i], "640x480")) return (LONG)i;

    return 0;
}

/* DIE FARBTIEFE IST KEINE EINSTELLUNG MEHR -- sie ist 8 Bit, fest.
 *
 * Die Zeichenflaeche der Engine ist seit der gemeinsamen Palette (18.09.)
 * 8 Bit. Ein 16-Bit-Schirm bringt dann keine einzige zusaetzliche Farbe,
 * zwingt die Ausgabe aber, jedes Bild Bildpunkt fuer Bildpunkt durch eine
 * Tabelle zu setzen und die doppelte Menge in den Schirm zu schreiben --
 * im Emulator gemessen 868 gegen 192 us je Bild, bei ziffergleichem
 * Zeichnen. 32 Bit ist das Vierfache.
 *
 * Eine Einstellung, deren beide anderen Stellungen nur langsamer sind und
 * nichts besser machen, ist keine Wahl, sondern eine Falle. Sie ist
 * deshalb weg; `tiefen[]` und `tiefe_platz()` sind mit ihr entfallen.
 *
 * Findet der RTG-Treiber keinen LUT8-Modus, faellt port/amiga/sdl_screen.cpp
 * von sich aus auf 16 Bit zurueck -- das ist ein Rueckfall im Treiber, keine
 * Einstellung, und es bleibt.
 *
 * ALTE DATEIEN: der Schluessel "tiefe" wird nicht mehr gelesen und faellt
 * beim naechsten Speichern heraus, genau wie "fenster" am 23.09. */

/* Wohin die Musik geht. AHI zuerst, weil es auf jeder Soundkarte und auf
 * SAGA laeuft; Paula ist der Weg fuer Rechner ohne AHI-Treiber und fuer
 * alle, die den Originalklang der Hardware wollen. Der Mischer ist in
 * beiden Faellen derselbe -- nur der Weg zur Hardware unterscheidet sich.
 * Gespeichert wird der LISTENPLATZ; es gibt hier keine Zahl mit eigener
 * Bedeutung, die man verwechseln koennte. */
static const char *musikwege[] = { "AHI", "Paula", NULL };

/* Die beiden Spielvarianten. Gespeichert wird der LISTENPLATZ -- es gibt
 * hier keine Zahl mit eigener Bedeutung, die man verwechseln koennte, und
 * die Liste wird nicht waermer als zwei Eintraege. */
static const char *varianten[] =
{
    "Original Z Story Mode",
    "ZodEngine Mode",
    NULL
};

/* Die Kampagne hat ihre EIGENE Kartenliste, und das ist kein Beiwerk:
 * map_list.txt enthaelt seit dem 23.09. auch die 22 Mehrspielerkarten, ist
 * als Kampagne also falsch. story_list.txt sind genau die 35 Originalkarten
 * in der Reihenfolge des Originals; tools/dist_bundle.sh prueft das beim
 * Packen und bricht sonst ab. */
#define STORY_LISTE "story_list.txt"

/* ------------------------------------------------------------------ */
/* AHI-Audiomodi                                                        */
/*                                                                      */
/* Die Engine benutzt AHIs TIEFSCHNITTSTELLE (AHI_AllocAudio) und kennt */
/* deshalb keine Units -- sie waehlt einen MODUS. Die Units 0 bis 3 der */
/* Voreinstellungen gehoeren zur Geraeteschnittstelle, die hier niemand */
/* benutzt. Darum steht hier "Audiomodus" und nicht "Unit".             */
/*                                                                      */
/* Eintrag 0 ist immer die Vorgabe: dann uebergibt der Launcher gar     */
/* nichts, und die Engine nimmt AHI_DEFAULT_ID -- also den in den        */
/* AHI-Voreinstellungen gespeicherten Standard.                          */
/* ------------------------------------------------------------------ */
struct Library *AHIBase = NULL;

#ifndef ZOD_LAUNCH_TEST
static struct MsgPort    *ahi_port = NULL;
static struct AHIRequest *ahi_req  = NULL;
static int    ahi_offen = 0;

/* ahi.device oeffnen -- gebraucht wird es fuer den Modus-Requester und fuer
 * den Namen des eingestellten Modus. Schlaegt es fehl, laeuft der Launcher
 * weiter; das Feld zeigt dann nur die Nummer. */
static void ahi_oeffnen(void)
{
    ahi_port = CreateMsgPort();
    if(!ahi_port) return;

    ahi_req = (struct AHIRequest *)CreateIORequest(ahi_port, sizeof(struct AHIRequest));
    if(!ahi_req) { DeleteMsgPort(ahi_port); ahi_port = NULL; return; }

    ahi_req->ahir_Version = 4;

    if(OpenDevice((CONST_STRPTR)AHINAME, AHI_NO_UNIT, (struct IORequest *)ahi_req, 0))
    {
        DeleteIORequest((struct IORequest *)ahi_req);
        DeleteMsgPort(ahi_port);
        ahi_req = NULL; ahi_port = NULL;
        return;
    }

    ahi_offen = 1;
    AHIBase = (struct Library *)ahi_req->ahir_Std.io_Device;
}

/* Den Modus benennen, wie ihn auch die AHI-Voreinstellungen zeigen. Die
 * Nummer steht dabei, weil sie im Protokoll der Engine auftaucht
 * ("AHI: Modus 0x...") und die Zuordnung eindeutig macht. */
static void ahi_name_von_id(ULONG id, char *aus, int platz)
{
    char roh[48];

    if(!id)
    {
        strncpy(aus, "Default (AHI preferences)", platz - 1);
        aus[platz - 1] = 0;

        return;
    }

    roh[0] = 0;

    if(ahi_offen)
        AHI_GetAudioAttrs(id, NULL,
                          AHIDB_BufferLen, (ULONG)sizeof(roh),
                          AHIDB_Name,      (ULONG)roh,
                          TAG_DONE);

    if(!roh[0]) strcpy(roh, "unnamed");

    snprintf(aus, platz, "%s (0x%08lx)", roh, (unsigned long)id);
}

/* DER MODUS WIRD JETZT UEBER AHIs EIGENEN REQUESTER GEWAEHLT, nicht mehr
 * ueber ein Auswahlfeld (Wunsch des Nutzers, 23.09.).
 *
 * asl.library hat dafuer NICHTS -- sie kennt Datei-, Schrift- und
 * Bildschirmmodus-Requester, aber keinen fuer Audio. AHI bringt seinen
 * eigenen mit (AHI_AllocAudioRequestA / AHI_AudioRequestA), und der ist
 * auch der richtige: er zeigt Namen, Frequenzen und die Infoseite genau so
 * wie die AHI-Voreinstellungen, und er kennt Modi, die erst zur Laufzeit
 * dazukommen.
 *
 * Die A-Fassungen mit TagItem-Feld, nicht die varargs-Fassungen: die
 * inline-Kopfdateien dieser Werkzeugkette machen aus varargs Makros, und
 * genau daran ist hier schon einmal der MUI-Aufbau zerbrochen
 * (MUI_NewObject). */
static ULONG ahi_modus_waehlen(ULONG start, struct Window *eltern)
{
    struct AHIAudioModeRequester *req;
    ULONG neu = start;

    struct TagItem auf[] = {
        { AHIR_TitleText,      (ULONG)"Select audio mode" },
        { AHIR_InitialAudioID, start ? start : (ULONG)AHI_DEFAULT_ID },
        { AHIR_Window,         (ULONG)eltern },
        { TAG_DONE,            0 }
    };
    struct TagItem zeig[] = { { TAG_DONE, 0 } };

    if(!ahi_offen) return start;

    /* Ohne Elternfenster den Tag gar nicht erst setzen -- AHI deutet einen
     * Nullzeiger sonst als gueltiges Fenster. */
    if(!eltern) auf[2].ti_Tag = TAG_IGNORE;

    req = AHI_AllocAudioRequestA(auf);

    if(!req) return start;

    if(AHI_AudioRequestA(req, zeig))
        neu = req->ahiam_AudioID;

    AHI_FreeAudioRequest(req);

    return neu;
}

static void ahi_schliessen(void)
{
    if(ahi_offen) { CloseDevice((struct IORequest *)ahi_req); ahi_offen = 0; }
    if(ahi_req)   { DeleteIORequest((struct IORequest *)ahi_req); ahi_req = NULL; }
    if(ahi_port)  { DeleteMsgPort(ahi_port); ahi_port = NULL; }
    AHIBase = NULL;
}

#endif /* !ZOD_LAUNCH_TEST */

/* ------------------------------------------------------------------ */
/* Einstellungen des Launchers                                          */
/* ------------------------------------------------------------------ */
#define CFG_DATEI   "ZodLaunch.cfg"
#define MAX_ZEILE   256
#define MAX_TEXT     64
#define MAX_PFAD    128

struct einstellungen
{
    char  spielername[MAX_TEXT];
    LONG  team;             /* Index in team_namen                      */
    BOOL  bot[8];           /* je Team ein Bot                          */
    LONG  aufloesung;       /* Index in aufloesungen                    */
    BOOL  ton;
    BOOL  musik;
    /* Deckel auf die GEZEICHNETE Groesse der Effekte, in HALBEN Schritten:
     * 2 = 1,0 ... 12 = 6,0. Halbe statt Zehntel, weil der Schieberegler von
     * MUI ganzzahlig arbeitet und der Nutzer 0,5er Schritte wollte.
     * 12 (= 6,0) heisst "kein Deckel": mehr als 5,98 kann die Engine
     * rechnerisch gar nicht erzeugen. */
    LONG  skalierung;
    char  kartenliste[MAX_TEXT];
    char  einzelkarte[MAX_TEXT];
    BOOL  nutze_einzelkarte;
    char  extra[MAX_TEXT];
    ULONG ahi_modus;        /* 0 = Vorgabe, sonst AHI-Modusnummer       */
    LONG  variante;         /* 0 = Story Mode, 1 = ZodEngine Mode       */
    BOOL  fps;              /* Bildrate oben links anzeigen             */
    LONG  musikweg;         /* 0 = AHI, 1 = Paula                       */
    /* Zwischensequenzen. Wirkt nur, wenn beim Installieren auch
     * welche kopiert wurden -- ohne das Verzeichnis passiert nichts,
     * und das Spiel verhaelt sich wie ganz ohne Filme. */
    BOOL  videos;
    /* Welches Spielprogramm gestartet wird. Das Paket enthaelt drei:
     * zod_040, zod_060, zod_080 -- sie unterscheiden sich im erzeugten
     * Maschinencode, nicht im Inhalt.
     *
     * BEWUSST OHNE VORGABE (leer). Eine geratene Vorgabe waere schlimmer als
     * keine: Stuende hier zod_080 und der Rechner ist eine 060, meldete der
     * Start nur "liess sich nicht starten" -- eine Fehlersuche an der
     * falschen Stelle. Leer heisst: der Launcher sagt klar, was zu tun ist.
     *
     * Eigenes Laengenmass, weil der ASL-Requester einen vollen Pfad
     * liefert (MAX_TEXT = 64 reicht dafuer nicht verlaesslich). */
    char  binaer[MAX_PFAD];
};

static struct einstellungen cfg;

static void cfg_vorgaben(void)
{
    int i;

    memset(&cfg, 0, sizeof(cfg));

    strcpy(cfg.spielername, "spieler");
    cfg.team        = 0;                 /* red                          */
    cfg.aufloesung  = aufloesung_platz("640x480");
    cfg.ton         = TRUE;
    cfg.musik       = FALSE;             /* Musik ist nicht mitgeliefert  */
    cfg.musikweg    = 0;                 /* AHI                           */
    cfg.videos      = TRUE;              /* wirkt nur, wenn cuts/ da ist  */
    cfg.skalierung  = 12;                /* 6,0 = kein Deckel             */
    cfg.variante    = 0;                 /* Original Z Story Mode         */
    cfg.fps         = TRUE;              /* wie bisher: Bildrate sichtbar */
    strcpy(cfg.kartenliste, "map_list.txt");

    for(i = 0; i < 8; i++) cfg.bot[i] = FALSE;
    cfg.bot[1] = TRUE;                   /* ein Bot (blue) als Vorgabe    */
}

/* Den Programmnamen uebernehmen -- ohne ein fuehrendes "PROGDIR:".
 *
 * Ein frueherer Stand schrieb die Auswahl des ASL-Requesters mitsamt Praefix
 * in die Datei ("PROGDIR:zod_080"). Das ist nicht falsch, aber Ballast: Das
 * Spiel startet ohnehin mit PROGDIR: als Arbeitsverzeichnis. Beim Lesen wird
 * es deshalb abgeschnitten, damit bestehende Dateien sich von selbst
 * bereinigen und niemand neu waehlen muss.
 *
 * AmigaDOS unterscheidet keine Gross- und Kleinschreibung, der Vergleich also
 * auch nicht. Von Hand statt mit strncasecmp -- das gibt es nicht auf jeder
 * Amiga-Laufzeit. */
static const char *ohne_progdir(const char *wert)
{
    static const char praefix[] = "progdir:";
    int i;

    for(i = 0; praefix[i]; i++)
    {
        char c = wert[i];

        if(c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        if(c != praefix[i]) break;
    }

    return praefix[i] ? wert : wert + i; /* Praefix vollstaendig getroffen */
}

static void binaer_setzen(const char *wert)
{
    strncpy(cfg.binaer, ohne_progdir(wert), MAX_PFAD - 1);
    cfg.binaer[MAX_PFAD - 1] = 0;
}

/* Dasselbe fuer die beiden Kartenfelder.
 *
 * DAS IST NICHT NUR KOSMETIK: PROGDIR: ist prozesslokal (pr_HomeDir). Ein
 * "PROGDIR:karte.map" in der Befehlszeile meint im gestarteten Spiel dessen
 * EIGENES Programmverzeichnis, nicht das des Launchers -- und der Weg
 * darueber hat hier schon einmal einen Aufruf haengen lassen. Der Requester schneidet das Praefix inzwischen selbst ab;
 * das hier heilt Dateien, die es schon enthalten. */
static void karte_setzen(char *ziel, const char *wert)
{
    strncpy(ziel, ohne_progdir(wert), MAX_TEXT - 1);
    ziel[MAX_TEXT - 1] = 0;
}

/* schluessel=wert lesen; unbekannte Zeilen werden ueberlesen */
static void cfg_laden(void)
{
    BPTR  f;
    char  zeile[MAX_ZEILE];

    f = Open(CFG_DATEI, MODE_OLDFILE);
    if(!f) return;

    while(FGets(f, zeile, sizeof(zeile)))
    {
        char *gleich = strchr(zeile, '=');
        char *wert;
        int   n;

        /* Zeilenende abschneiden */
        n = (int)strlen(zeile);
        while(n > 0 && (zeile[n-1] == '\n' || zeile[n-1] == '\r')) zeile[--n] = 0;

        if(!gleich) continue;
        *gleich = 0;
        wert = gleich + 1;

        if(!strcmp(zeile, "spielername"))  { strncpy(cfg.spielername, wert, MAX_TEXT-1); cfg.spielername[MAX_TEXT-1] = 0; }
        else if(!strcmp(zeile, "team"))        cfg.team       = atoi(wert);
        else if(!strcmp(zeile, "aufloesung"))  cfg.aufloesung = aufloesung_platz(wert);
        else if(!strcmp(zeile, "ton"))         cfg.ton        = (BOOL)atoi(wert);
        else if(!strcmp(zeile, "musik"))       cfg.musik      = (BOOL)atoi(wert);
        /* "fenster" gab es bis zum 23.09. Der Schluessel wird jetzt
         * einfach nicht mehr erkannt und faellt beim naechsten Speichern
         * heraus -- eine alte Datei darf davon nicht stolpern. */
        else if(!strcmp(zeile, "videos"))      cfg.videos     = (BOOL)atoi(wert);
        else if(!strcmp(zeile, "skalierung"))
        {
            cfg.skalierung = atoi(wert);
            /* Aeltere Dateien kennen den Schluessel nicht; ein unsinniger
             * Wert aus einer von Hand bearbeiteten Datei darf den Regler
             * nicht aus dem Bereich schieben. */
            if(cfg.skalierung < 2)  cfg.skalierung = 2;
            if(cfg.skalierung > 12) cfg.skalierung = 12;
        }
        else if(!strcmp(zeile, "einzelkarte_an")) cfg.nutze_einzelkarte = (BOOL)atoi(wert);
        else if(!strcmp(zeile, "kartenliste")) karte_setzen(cfg.kartenliste, wert);
        else if(!strcmp(zeile, "einzelkarte")) karte_setzen(cfg.einzelkarte, wert);
        else if(!strcmp(zeile, "extra"))       { strncpy(cfg.extra, wert, MAX_TEXT-1); cfg.extra[MAX_TEXT-1] = 0; }
        else if(!strcmp(zeile, "ahimodus"))    cfg.ahi_modus  = strtoul(wert, NULL, 0);
        else if(!strcmp(zeile, "variante"))    cfg.variante   = (atoi(wert) == 1) ? 1 : 0;
        else if(!strcmp(zeile, "fps"))        cfg.fps        = (BOOL)atoi(wert);
        else if(!strcmp(zeile, "musikweg"))    cfg.musikweg   = (atol(wert) == 1) ? 1 : 0;
        /* Aeltere Dateien kennen den Schluessel nicht -- dann bleibt er leer,
         * und der Launcher verlangt beim Starten eine Auswahl. */
        else if(!strcmp(zeile, "binaer"))      { binaer_setzen(wert); }
        else if(!strncmp(zeile, "bot", 3))
        {
            int idx = atoi(zeile + 3);
            if(idx >= 0 && idx < 8) cfg.bot[idx] = (BOOL)atoi(wert);
        }
    }

    Close(f);
}

static void cfg_speichern(void)
{
    BPTR f;
    char zeile[MAX_ZEILE];
    int  i;

    f = Open(CFG_DATEI, MODE_NEWFILE);
    if(!f) return;

    snprintf(zeile, sizeof(zeile), "spielername=%s\n", cfg.spielername);   FPuts(f, zeile);
    snprintf(zeile, sizeof(zeile), "team=%ld\n", (long)cfg.team);          FPuts(f, zeile);
    snprintf(zeile, sizeof(zeile), "aufloesung=%s\n", aufloesungen[cfg.aufloesung]); FPuts(f, zeile);
    snprintf(zeile, sizeof(zeile), "ton=%d\n", cfg.ton ? 1 : 0);           FPuts(f, zeile);
    snprintf(zeile, sizeof(zeile), "musik=%d\n", cfg.musik ? 1 : 0);       FPuts(f, zeile);
    snprintf(zeile, sizeof(zeile), "videos=%d\n", cfg.videos ? 1 : 0);     FPuts(f, zeile);
    snprintf(zeile, sizeof(zeile), "skalierung=%ld\n", (long)cfg.skalierung); FPuts(f, zeile);
    snprintf(zeile, sizeof(zeile), "kartenliste=%s\n", cfg.kartenliste);   FPuts(f, zeile);
    snprintf(zeile, sizeof(zeile), "einzelkarte=%s\n", cfg.einzelkarte);   FPuts(f, zeile);
    snprintf(zeile, sizeof(zeile), "einzelkarte_an=%d\n", cfg.nutze_einzelkarte ? 1 : 0); FPuts(f, zeile);
    snprintf(zeile, sizeof(zeile), "extra=%s\n", cfg.extra);               FPuts(f, zeile);
    snprintf(zeile, sizeof(zeile), "ahimodus=0x%08lx\n", (unsigned long)cfg.ahi_modus); FPuts(f, zeile);
    snprintf(zeile, sizeof(zeile), "variante=%ld\n", (long)cfg.variante);  FPuts(f, zeile);
    snprintf(zeile, sizeof(zeile), "fps=%d\n", cfg.fps ? 1 : 0);           FPuts(f, zeile);
    snprintf(zeile, sizeof(zeile), "musikweg=%ld\n", (long)cfg.musikweg); FPuts(f, zeile);
    snprintf(zeile, sizeof(zeile), "binaer=%s\n", cfg.binaer);             FPuts(f, zeile);

    for(i = 0; i < 8; i++)
    {
        snprintf(zeile, sizeof(zeile), "bot%d=%d\n", i, cfg.bot[i] ? 1 : 0);
        FPuts(f, zeile);
    }

    Close(f);
}

/* ------------------------------------------------------------------ */
/* Befehlszeile bauen                                                   */
/* ------------------------------------------------------------------ */

/* Setzt nur Argumente, die von der Vorgabe abweichen oder noetig sind.
 * Reihenfolge und Schreibweise entsprechen der Hilfeausgabe des Spiels. */
static void befehlszeile_bauen(char *aus, size_t platz)
{
    char teil[MAX_ZEILE];
    int  i;

    aus[0] = 0;

    /* IM STORY MODE ENTSCHEIDET NICHT DER NUTZER, WELCHE KARTEN LAUFEN.
     * Die Kampagne ist eine feste Folge -- und die Filme haengen an einer
     * eigenen Zaehlung, nicht an der Karte. Eine abweichende Liste liesse
     * die Filme eine andere Geschichte erzaehlen als das Gespielte. */
    if(cfg.variante == 0)
        snprintf(teil, sizeof(teil), "-l %s ", STORY_LISTE);
    else if(cfg.nutze_einzelkarte && cfg.einzelkarte[0])
        snprintf(teil, sizeof(teil), "-m %s ", cfg.einzelkarte);
    else
        snprintf(teil, sizeof(teil), "-l %s ", cfg.kartenliste);
    strncat(aus, teil, platz - strlen(aus) - 1);

    if(cfg.spielername[0])
    {
        snprintf(teil, sizeof(teil), "-n %s ", cfg.spielername);
        strncat(aus, teil, platz - strlen(aus) - 1);
    }

    /* DIE KAMPAGNE IST ROT GEGEN BLAU.
     *
     * Die orig-Karten sind Zwei-Spieler-Karten, und im Original fuehrt man
     * die roten Roboter. Team und Bots sind im Story Mode deshalb nicht
     * frei -- die Felder sind in der Oberflaeche gesperrt, und hier stehen
     * die festen Werte. Nur zu sperren genuegte NICHT: in der Einstellung
     * koennte noch "green" stehen, und dann liefe die Kampagne mit einem
     * Team, fuer das die Karten gar keinen Startplatz haben. */
    if(cfg.variante == 0)
    {
        strncat(aus, "-t red -b blue ", platz - strlen(aus) - 1);
    }
    else
    {
        snprintf(teil, sizeof(teil), "-t %s ", team_namen[cfg.team]);
        strncat(aus, teil, platz - strlen(aus) - 1);

        for(i = 0; i < 8; i++)
            if(cfg.bot[i] && i != cfg.team)
            {
                snprintf(teil, sizeof(teil), "-b %s ", team_namen[i]);
                strncat(aus, teil, platz - strlen(aus) - 1);
            }
    }

    snprintf(teil, sizeof(teil), "-r %s ", aufloesungen[cfg.aufloesung]);
    strncat(aus, teil, platz - strlen(aus) - 1);

    /* Nur uebergeben, wenn ausdruecklich gewaehlt -- sonst soll die Engine
     * ihren eigenen Weg gehen (AHI_DEFAULT_ID, dann Suche). */
    if(cfg.ahi_modus)
    {
        snprintf(teil, sizeof(teil), "-A 0x%08lx ", (unsigned long)cfg.ahi_modus);
        strncat(aus, teil, platz - strlen(aus) - 1);
    }

    if(!cfg.ton)   strncat(aus, "-s ", platz - strlen(aus) - 1);
    if(!cfg.musik) strncat(aus, "-u ", platz - strlen(aus) - 1);

    /* Der Ausgabeweg wird nur mitgegeben, wenn Musik ueberhaupt laeuft --
     * sonst stuende in der Vorschau eine Einstellung, die nichts tut. */
    if(cfg.musik)
    {
        snprintf(teil, sizeof(teil), "-M %s ", cfg.musikweg == 1 ? "paula" : "ahi");
        strncat(aus, teil, platz - strlen(aus) - 1);
    }
    /* Zwischensequenzen. Der Pfad steht hier fest und NICHT als
     * Einstellung: der Installer legt sie neben das Spiel, und ein zweiter
     * Ort waere eine Einstellung, die niemand braucht und die man falsch
     * setzen kann. Ohne -V spielt die Engine ueberhaupt keine Filme --
     * genau das ist der Fall "beim Installieren nicht mitkopiert". */
    if(cfg.videos && cfg.variante == 0)
        strncat(aus, "-V cuts ", platz - strlen(aus) - 1);

    /* Die Variante immer mitgeben, auch die Vorgabe: dann steht sie in der
     * Vorschau und im Spielprotokoll, statt ein unsichtbarer Sonderfall zu
     * sein. */
    snprintf(teil, sizeof(teil), "-G %s ", cfg.variante == 1 ? "zod" : "story");
    strncat(aus, teil, platz - strlen(aus) - 1);

    /* Auch die Vorgabe mitgeben: dann steht sie im Spielprotokoll, statt
     * ein unsichtbarer Sonderfall zu sein. */
    snprintf(teil, sizeof(teil), "-F %s ", cfg.fps ? "on" : "off");
    strncat(aus, teil, platz - strlen(aus) - 1);


    /* Immer mitgeben, auch bei 6,0 -- dann steht die Einstellung in der
     * Vorschau und im Spielprotokoll, statt ein unsichtbarer Sonderfall zu
     * sein. Bei 6,0 klemmt der Deckel nie (die Engine erreicht hoechstens
     * 5,98), er kostet dort also nichts.
     * Punkt als Dezimalzeichen: die Engine nimmt beides, aber das Protokoll
     * und die Hilfe schreiben den Punkt. */
    snprintf(teil, sizeof(teil), "-S %ld.%ld ",
             (long)(cfg.skalierung / 2), (long)((cfg.skalierung & 1) ? 5 : 0));
    strncat(aus, teil, platz - strlen(aus) - 1);

    /* OpenGL ist in dieser Fassung abgeschaltet; -o schadet nicht und haelt
     * die Zeile mit den dokumentierten Beispielen deckungsgleich. */
    strncat(aus, "-o ", platz - strlen(aus) - 1);

    if(cfg.extra[0])
    {
        strncat(aus, cfg.extra, platz - strlen(aus) - 1);
        strncat(aus, " ", platz - strlen(aus) - 1);
    }
}

/* Die vollstaendige Startzeile: Programm + Argumente.
 *
 * Bewusst AUSSERHALB des MUI-Teils, damit der Host-Test sie prueft. Der
 * Launcher meldet nichts ueber den seriellen Kanal; ein Emulatorlauf belegt
 * nur, dass er startet. Die Regel "ohne gewaehltes Binary wird nicht
 * gestartet" waere sonst ungeprueft -- und sie ist genau die, die den
 * Erstnutzer trifft.
 *
 * Rueckgabe 0 = in Ordnung, -1 = `fehler` ist gesetzt. */
static int startbefehl_bauen(char *aus, size_t platz,
                             char *fehler, size_t fehler_platz)
{
    char argumente[MAX_ZEILE];

    if(!cfg.binaer[0])
    {
        snprintf(fehler, fehler_platz,
                 "Please choose a binary first "
                 "(zod_040, zod_060 or zod_080)");
        aus[0] = 0;
        return -1;
    }

    befehlszeile_bauen(argumente, sizeof(argumente));
    snprintf(aus, platz, "%s %s", cfg.binaer, argumente);

    return 0;
}

/* ------------------------------------------------------------------ */
/* Spiel starten                                                        */
#ifndef ZOD_LAUNCH_TEST

/* ------------------------------------------------------------------ */

/* ------------------------------------------------------------------ */
/* Binary waehlen -- ASL-Dateirequester                                 */
/*                                                                      */
/* Von Hand statt ueber MUIs Popasl-Klasse: So laesst sich der Requester */
/* auf PROGDIR: und auf das Muster "zod_#?" vorbelegen. Der Nutzer sieht */
/* damit genau die drei Programme, um die es geht, statt des ganzen      */
/* Verzeichnisses -- und die Oberflaeche ist ohnehin nur am Bildschirm   */
/* pruefbar, also zaehlt hier jede Stelle, die Fehlbedienung ausschliesst.*/
/* ------------------------------------------------------------------ */
struct Library *AslBase = NULL;

/* Zeigen beide Angaben auf dasselbe Verzeichnis? */
static int gleiches_verzeichnis(const char *a, const char *b)
{
    BPTR la, lb;
    int  gleich = 0;

    la = Lock((CONST_STRPTR)a, ACCESS_READ);
    lb = Lock((CONST_STRPTR)b, ACCESS_READ);

    if(la && lb) gleich = (SameLock(la, lb) == LOCK_SAME);

    if(la) UnLock(la);
    if(lb) UnLock(lb);

    return gleich;
}

/* Eine Datei waehlen und RELATIV zum Programmverzeichnis ablegen, wenn sie
 * darunter liegt.
 *
 * Warum relativ: Das Spiel startet mit PROGDIR: als Arbeitsverzeichnis
 * (NP_CurrentDir weiter unten), und die Eintraege in map_list.txt sind
 * ebenfalls relativ. Ein absoluter Pfad wuerde zwar auch gehen, haengt aber
 * am Namen des Volumes -- wird die Platte umbenannt oder das Spiel woanders
 * hin kopiert, zeigt die Einstellung ins Leere.
 *
 * Verglichen wird ueber NameFromLock, nicht ueber die Zeichenkette aus dem
 * Requester: "PROGDIR:", "ZOD:" und "ZOD:/" koennen dasselbe Verzeichnis
 * meinen (siehe gleiches_verzeichnis). NameFromLock liefert fuer beide
 * Seiten die aufgeloeste Schreibweise. */
static int datei_waehlen(const char *titel, const char *muster,
                         char *ziel, int platz)
{
    struct FileRequester *fr;
    char voll[MAX_PFAD];
    char heim[MAX_PFAD];
    int  gewaehlt = 0;

    if(!AslBase) return 0;

    fr = (struct FileRequester *)
         AllocAslRequestTags(ASL_FileRequest,
             ASLFR_TitleText,      (ULONG)titel,
             ASLFR_InitialDrawer,  (ULONG)"PROGDIR:",
             ASLFR_InitialPattern, (ULONG)muster,
             ASLFR_DoPatterns,     TRUE,
             TAG_DONE);

    if(!fr) return 0;

    if(AslRequestTags(fr, TAG_DONE) && fr->fr_File && fr->fr_File[0])
    {
        voll[0] = 0;

        /* DAS VERZEICHNIS ERST AUFLOESEN.
         *
         * ASL gibt zurueck, was es bekommen hat: hat der Nutzer nicht
         * navigiert, steht in fr_Drawer woertlich "PROGDIR:" -- der Wert aus
         * ASLFR_InitialDrawer. Daraus wurde "PROGDIR:karte.map", und der
         * Vergleich weiter unten lief gegen den AUFGELOESTEN Pfad ("ZOD:"),
         * passte also nie. Das Praefix blieb stehen und landete in der
         * Befehlszeile -- vom Nutzer gemeldet.
         *
         * PROGDIR: ist zudem PROZESSLOKAL (pr_HomeDir): das Spiel wird von
         * ZodLaunch aus gestartet und hat ein eigenes. Ein weitergereichtes
         * "PROGDIR:" meint dort also etwas anderes als hier -- dieselbe
         * Falle wie beim Aufruf von jvplay.
         *
         * Lock + NameFromLock liefert die echte Schreibweise, und zwar fuer
         * jedes Assign, nicht nur fuer PROGDIR:. */
        if(fr->fr_Drawer && fr->fr_Drawer[0])
        {
            BPTR dl = Lock((CONST_STRPTR)fr->fr_Drawer, ACCESS_READ);

            if(dl)
            {
                if(!NameFromLock(dl, (STRPTR)voll, sizeof(voll))) voll[0] = 0;

                UnLock(dl);
            }

            /* Laesst es sich nicht sperren, bleibt die Angabe des
             * Requesters -- besser als gar nichts. */
            if(!voll[0])
            {
                strncpy(voll, (char *)fr->fr_Drawer, sizeof(voll) - 1);
                voll[sizeof(voll) - 1] = 0;
            }
        }

        if(AddPart((STRPTR)voll, (CONST_STRPTR)fr->fr_File, sizeof(voll)))
        {
            const char *nimm = voll;
            BPTR lock = Lock((CONST_STRPTR)"PROGDIR:", ACCESS_READ);

            if(lock)
            {
                heim[0] = 0;

                if(NameFromLock(lock, (STRPTR)heim, sizeof(heim)) && heim[0])
                {
                    int n = (int)strlen(heim);
                    int i;

                    /* AmigaDOS ist schreibungsunabhaengig. */
                    for(i = 0; i < n; i++)
                    {
                        char a = voll[i], b = heim[i];

                        if(a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
                        if(b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
                        if(a != b) break;
                    }

                    if(i == n)
                    {
                        const char *r = voll + n;

                        /* Nach einem Volume-Namen steht schon der
                         * Doppelpunkt, nach einem Verzeichnis ein
                         * Schraegstrich -- beides gehoert weg. */
                        if(*r == '/' || *r == ':') r++;

                        if(*r) nimm = r;
                    }
                }

                UnLock(lock);
            }

            strncpy(ziel, nimm, platz - 1);
            ziel[platz - 1] = 0;
            gewaehlt = 1;
        }
    }

    FreeAslRequest(fr);

    return gewaehlt;
}

/* Liefert 1, wenn der Nutzer etwas gewaehlt hat. */
static int binaer_waehlen(void)
{
    struct FileRequester *fr;
    char  pfad[MAX_PFAD];
    int   gewaehlt = 0;

    if(!AslBase) return 0;

    fr = (struct FileRequester *)
         AllocAslRequestTags(ASL_FileRequest,
             ASLFR_TitleText,      (ULONG)"Select Zod program",
             ASLFR_InitialDrawer,  (ULONG)"PROGDIR:",
             ASLFR_InitialPattern, (ULONG)"zod_#?",
             ASLFR_DoPatterns,     TRUE,
             TAG_DONE);

    if(!fr) return 0;

    if(AslRequestTags(fr, TAG_DONE) && fr->fr_File && fr->fr_File[0])
    {
        /* Liegt die Auswahl im Programmverzeichnis -- der Normalfall, denn
         * alle drei Fassungen liegen neben dem Launcher --, wird NUR der Name
         * gespeichert. Das Spiel startet ohnehin mit PROGDIR: als
         * Arbeitsverzeichnis (NP_CurrentDir unten), ein Praefix waere also
         * nur Ballast in der Befehlszeile.
         *
         * Verglichen wird ueber LOCKS, nicht ueber Zeichenketten: "PROGDIR:",
         * "ZOD:" und "ZOD:/" koennen dasselbe Verzeichnis meinen, und ein
         * Zeichenkettenvergleich saehe drei verschiedene. */
        if(!fr->fr_Drawer || !fr->fr_Drawer[0] ||
           gleiches_verzeichnis((char *)fr->fr_Drawer, "PROGDIR:"))
        {
            strncpy(cfg.binaer, (char *)fr->fr_File, MAX_PFAD - 1);
            cfg.binaer[MAX_PFAD - 1] = 0;
            gewaehlt = 1;
        }
        else
        {
            /* Woanders her: voller Pfad. AddPart setzt den Trenner richtig --
             * nach einem Volume-Namen ("ZOD:") gehoert KEINER dazwischen,
             * nach einem Verzeichnis schon. Von Hand mit "/" zusammenzukleben
             * ist genau die Stelle, an der auf AmigaOS Pfade kaputtgehen. */
            strncpy(pfad, (char *)fr->fr_Drawer, sizeof(pfad) - 1);
            pfad[sizeof(pfad) - 1] = 0;

            if(AddPart((STRPTR)pfad, (CONST_STRPTR)fr->fr_File, sizeof(pfad)))
            {
                strncpy(cfg.binaer, pfad, MAX_PFAD - 1);
                cfg.binaer[MAX_PFAD - 1] = 0;
                gewaehlt = 1;
            }
        }
    }

    FreeAslRequest(fr);

    return gewaehlt;
}

/* Asynchron ueber SystemTags mit NP_CurrentDir: das Spiel MUSS in seinem
 * eigenen Verzeichnis laufen, weil es die Assets ueber relative Pfade sucht.
 * SYS_Asynch gibt den Launcher sofort wieder frei; das Verzeichnis-Lock geht
 * dabei in den Besitz des neuen Prozesses ueber und darf nicht selbst
 * freigegeben werden. */
static LONG spiel_starten(char *fehler, size_t fehler_platz)
{
    char  befehl[512];
    BPTR  verzeichnis;
    LONG  rc;

    if(startbefehl_bauen(befehl, sizeof(befehl), fehler, fehler_platz))
        return -1;

    verzeichnis = Lock("PROGDIR:", ACCESS_READ);
    if(!verzeichnis)
    {
        snprintf(fehler, fehler_platz, "Cannot lock PROGDIR:");
        return -1;
    }

    rc = SystemTags(befehl,
                    SYS_Input,      (ULONG)NULL,
                    SYS_Output,     (ULONG)NULL,
                    SYS_Asynch,     TRUE,
                    NP_CurrentDir,  (ULONG)verzeichnis,
                    NP_StackSize,   262144,
                    NP_Name,        (ULONG)"Zod Engine",
                    TAG_DONE);

    if(rc == -1)
    {
        /* Bei Misserfolg gehoert das Lock noch uns. */
        UnLock(verzeichnis);
        snprintf(fehler, fehler_platz, "%s would not start -- is it in the program directory?",
                 cfg.binaer);
        return -1;
    }

    /* Erfolg: das Lock gehoert jetzt dem neuen Prozess. */
    return 0;
}

/* ------------------------------------------------------------------ */
/* Oberflaeche                                                          */
/* ------------------------------------------------------------------ */
enum
{
    ID_START = 1, ID_BEENDEN, ID_BINAER,
    ID_AHI, ID_AHI_VORGABE, ID_LISTE, ID_KARTE
};

int main(int argc, char **argv)
{
    Object *app, *fenster;
    Object *o_name, *o_team, *o_aufl;
    Object *o_skalierung, *o_skalierung_text;
    Object *o_ton, *o_musik, *o_videos, *o_musikweg;
    Object *o_ahi, *o_ahi_label, *o_ahi_gruppe, *o_ahi_waehlen, *o_ahi_vorgabe;
    Object *o_liste, *o_karte, *o_karte_an, *o_extra;
    Object *o_liste_waehlen, *o_karte_waehlen;
    Object *o_variante, *o_karten_gruppe, *o_videos_gruppe, *o_videos_label;
    Object *o_fps;
    Object *o_bots_gruppe, *o_team_label;
    LONG    karten_aus = -1;
    Object *o_start;
    Object *o_binaer, *o_binaer_waehlen;
    Object *o_bot[8];
    ULONG   signale = 0;
    BOOL    laeuft = TRUE;
    int     i;
    char    ahi_anzeige[80];
    LONG    ahi_aus = -1;        /* zuletzt gesetzter Ausgrauzustand */

    (void)argc; (void)argv;

    cfg_vorgaben();
    cfg_laden();

    /* Vor dem Aufbau der Oberflaeche: ahi.device muss offen sein, sonst
     * kann das Feld den Namen des eingestellten Modus nicht zeigen. */
    ahi_oeffnen();

    MUIMasterBase = OpenLibrary(MUIMASTER_NAME, MUIMASTER_VMIN);
    if(!MUIMasterBase)
    {
        Printf("ZodLaunch: muimaster.library %ld or newer is required.\n",
               (LONG)MUIMASTER_VMIN);
        return RETURN_FAIL;
    }

    /* asl.library ist seit OS 2.0 dabei. Fehlt sie wider Erwarten, laeuft der
     * Launcher weiter -- der Pfad laesst sich dann von Hand eintippen, statt
     * dass gar nichts geht. */
    AslBase = OpenLibrary((CONST_STRPTR)"asl.library", 38L);

    ahi_name_von_id(cfg.ahi_modus, ahi_anzeige, sizeof(ahi_anzeige));

    app = ApplicationObject,
        MUIA_Application_Title,       (ULONG)"ZodLaunch",
        MUIA_Application_Version,     (ULONG)"$VER: ZodLaunch 1.2 (24.09.2026)",
        MUIA_Application_Copyright,   (ULONG)"GPLv3",
        MUIA_Application_Author,      (ULONG)"Zod Engine port",
        MUIA_Application_Description, (ULONG)"Launcher for the Zod Engine",
        MUIA_Application_Base,        (ULONG)"ZODLAUNCH",

        SubWindow, fenster = WindowObject,
            MUIA_Window_Title, (ULONG)"Start Zod Engine",
            MUIA_Window_ID,    MAKE_ID('Z','O','D','L'),
            WindowContents, VGroup,

                Child, ColGroup(2), GroupFrameT("Game mode"),
                    Child, Label2("Mode"),
                    Child, o_variante = CycleObject,
                        MUIA_Cycle_Entries, (ULONG)varianten,
                        MUIA_Cycle_Active,  cfg.variante,
                        MUIA_ShortHelp, (ULONG)"Original Z Story Mode: the 35 "
                            "original maps in the original order, with all "
                            "cutscenes and the statistics screen. The campaign "
                            "is red against blue, so team and bots are fixed.\n"
                            "ZodEngine Mode: like the ZodEngine original -- no "
                            "cutscenes, no statistics screen, your own maps, "
                            "teams and bots. Only the original loading screen "
                            "is kept.",
                    End,
                End,

                Child, ColGroup(2), GroupFrameT("Player"),
                    Child, Label2("Name"),
                    Child, o_name = StringObject,
                        StringFrame,
                        MUIA_String_Contents, (ULONG)cfg.spielername,
                        MUIA_String_MaxLen,   MAX_TEXT,
                    End,
                    /* Der NAME bleibt auch im Story Mode editierbar -- er
                     * gehoert dir, nicht der Kampagne. Nur das Team ist
                     * festgelegt. */
                    Child, o_team_label = Label2("Team"),
                    Child, o_team = CycleObject,
                        MUIA_Cycle_Entries, (ULONG)team_namen,
                        MUIA_Cycle_Active,  cfg.team,
                    End,
                End,

                /* ColGroup(8) statt zweier HGroups: nur so stehen die
                 * Ankreuzfelder wirklich untereinander. In einer HGroup
                 * bestimmt jede Zeile ihre Breiten selbst, und weil
                 * "yellow" laenger ist als "red", verrutschte die zweite
                 * Reihe gegen die erste. Eine Spaltengruppe misst ueber
                 * ALLE Zeilen.
                 *
                 * Das HVSpace am Ende jeder Zeile zieht die Gruppe auf die
                 * Fensterbreite: Ankreuzfelder und Beschriftungen sind
                 * fest breit, ohne einen dehnbaren Fueller bliebe die
                 * Gruppe schmal und MUI zentrierte sie. */
                Child, o_bots_gruppe = ColGroup(9), GroupFrameT("Bots"),
                    Child, o_bot[0] = MUI_MakeObject(MUIO_Checkmark, (ULONG)"red"),
                    Child, Label1("red"),
                    Child, o_bot[1] = MUI_MakeObject(MUIO_Checkmark, (ULONG)"blue"),
                    Child, Label1("blue"),
                    Child, o_bot[2] = MUI_MakeObject(MUIO_Checkmark, (ULONG)"green"),
                    Child, Label1("green"),
                    Child, o_bot[3] = MUI_MakeObject(MUIO_Checkmark, (ULONG)"yellow"),
                    Child, Label1("yellow"),
                    Child, HVSpace,
                    Child, o_bot[4] = MUI_MakeObject(MUIO_Checkmark, (ULONG)"purple"),
                    Child, Label1("purple"),
                    Child, o_bot[5] = MUI_MakeObject(MUIO_Checkmark, (ULONG)"teal"),
                    Child, Label1("teal"),
                    Child, o_bot[6] = MUI_MakeObject(MUIO_Checkmark, (ULONG)"white"),
                    Child, Label1("white"),
                    Child, o_bot[7] = MUI_MakeObject(MUIO_Checkmark, (ULONG)"black"),
                    Child, Label1("black"),
                    Child, HVSpace,
                End,

                Child, o_karten_gruppe = VGroup, GroupFrameT("Maps"),
                    Child, ColGroup(2),
                        Child, Label2("Map list"),
                        Child, HGroup,
                            Child, o_liste = StringObject,
                                StringFrame,
                                MUIA_String_Contents, (ULONG)cfg.kartenliste,
                                MUIA_String_MaxLen,   MAX_TEXT,
                                MUIA_ShortHelp, (ULONG)"The rotation the server "
                                    "cycles through, and the list the in-game "
                                    "\"Select Map\" menu offers.",
                            End,
                            Child, o_liste_waehlen = SimpleButton("Choose..."),
                        End,
                        Child, Label2("Single map"),
                        Child, HGroup,
                            Child, o_karte = StringObject,
                                StringFrame,
                                MUIA_String_Contents, (ULONG)cfg.einzelkarte,
                                MUIA_String_MaxLen,   MAX_TEXT,
                                MUIA_ShortHelp, (ULONG)"One map file. Only used "
                                    "when the box below is ticked; it then "
                                    "bypasses the list entirely.",
                            End,
                            Child, o_karte_waehlen = SimpleButton("Choose..."),
                        End,
                    End,
                    Child, HGroup,
                        Child, o_karte_an = MUI_MakeObject(MUIO_Checkmark, (ULONG)"Single map"),
                        Child, Label1("Start the single map instead of the list"),
                        Child, HVSpace,
                    End,
                End,

                Child, ColGroup(2), GroupFrameT("Display"),
                    Child, Label2("Resolution"),
                    Child, o_aufl = CycleObject,
                        MUIA_Cycle_Entries, (ULONG)aufloesungen,
                        MUIA_Cycle_Active,  cfg.aufloesung,
                    End,
                    Child, Label2("Scaling"),
                    Child, HGroup,
                        /* Der Regler laeuft in HALBEN Schritten (2..12).
                         * MUIA_Numeric_Format kann daraus kein "3.5"
                         * machen -- es bekommt genau eine ganze Zahl.
                         * Deshalb bleibt die Beschriftung daneben; sie
                         * steht jetzt aber IN derselben Zeile und rechts
                         * am Regler, so dass beides ein Bedienelement
                         * ergibt. */
                        Child, o_skalierung = SliderObject,
                            MUIA_Numeric_Min,    2,
                            MUIA_Numeric_Max,    12,
                            MUIA_Numeric_Value,  cfg.skalierung,
                            MUIA_Numeric_Format, (ULONG)"",
                            MUIA_ShortHelp, (ULONG)"Cap on the DRAWN size of "
                                "explosion debris (1.0 to 6.0 in half steps).\n"
                                "Graphics only: flight path, flight time and "
                                "impact moment are unchanged.\n"
                                "6.0 = no cap. Lower values are meant for "
                                "68040/68060 and AGA.",
                        End,
                        Child, o_skalierung_text = TextObject,
                            MUIA_Text_PreParse, (ULONG)"\33r",
                            MUIA_Text_SetMax,   TRUE,
                            MUIA_Text_Contents, (ULONG)"6.0",
                        End,
                    End,
                    Child, Label2("Frame rate"),
                    Child, HGroup,
                        Child, o_fps = MUI_MakeObject(MUIO_Checkmark, (ULONG)"FPS"),
                        Child, Label1("Show frames per second, top left"),
                        Child, HVSpace,
                    End,
                    Child, o_videos_label = Label2("Cutscenes"),
                    Child, o_videos_gruppe = HGroup,
                        Child, o_videos = MUI_MakeObject(MUIO_Checkmark, (ULONG)"Cutscenes"),
                        Child, Label1("Play the films between missions"),
                        Child, HVSpace,
                    End,
                End,

                Child, VGroup, GroupFrameT("Audio"),
                    Child, HGroup,
                        Child, o_ton = MUI_MakeObject(MUIO_Checkmark, (ULONG)"Sound"),
                        Child, Label1("Sound effects"),
                        Child, o_musik = MUI_MakeObject(MUIO_Checkmark, (ULONG)"Music"),
                        Child, Label1("Music"),
                        Child, HVSpace,
                    End,
                    Child, ColGroup(2),
                        Child, Label2("Music output"),
                        Child, o_musikweg = CycleObject,
                            MUIA_Cycle_Entries, (ULONG)musikwege,
                            MUIA_Cycle_Active,  cfg.musikweg,
                            MUIA_ShortHelp, (ULONG)"Where the music goes. The "
                                "mixer is the same either way -- only the path "
                                "to the hardware differs.\n"
                                "AHI: one stereo stream to the sound card or "
                                "SAGA, 16 bit.\n"
                                "Paula: audio.device claims the channels and "
                                "writes the registers directly, 8 bit -- the "
                                "sound of the original hardware.",
                        End,

                        /* Ausgegraut, sobald Paula gewaehlt ist: der Modus
                         * gehoert zu AHI und hat auf dem Paula-Weg keine
                         * Bedeutung. */
                        Child, o_ahi_label = Label2("Audio mode"),
                        Child, o_ahi_gruppe = HGroup,
                            Child, o_ahi = TextObject,
                                TextFrame,
                                MUIA_Background,    MUII_TextBack,
                                MUIA_Text_Contents, (ULONG)ahi_anzeige,
                                MUIA_ShortHelp, (ULONG)"The engine uses AHI's "
                                    "low level interface and picks a MODE, not "
                                    "a unit -- units belong to the device "
                                    "interface, which it does not use.",
                            End,
                            Child, o_ahi_waehlen = SimpleButton("Choose..."),
                            Child, o_ahi_vorgabe = SimpleButton("Default"),
                        End,
                    End,
                End,

                Child, ColGroup(2), GroupFrameT("Program"),
                    Child, Label2("Binary"),
                    Child, HGroup,
                        Child, o_binaer = StringObject,
                            StringFrame,
                            MUIA_String_Contents, (ULONG)cfg.binaer,
                            MUIA_String_MaxLen,   MAX_PFAD,
                            MUIA_ShortHelp, (ULONG)"Which of the three builds "
                                "to start: zod_040, zod_060 or zod_080. They "
                                "differ only in the generated machine code. "
                                "There is deliberately NO default -- a wrongly "
                                "guessed build only reports that it will not "
                                "start.",
                        End,
                        Child, o_binaer_waehlen = SimpleButton("Choose..."),
                    End,
                    Child, Label2("Arguments"),
                    Child, o_extra = StringObject,
                        StringFrame,
                        MUIA_String_Contents, (ULONG)cfg.extra,
                        MUIA_String_MaxLen,   MAX_TEXT,
                    End,
                End,

                /* NUR NOCH "Start". Der Sicherungsknopf ist weg -- die
                 * Einstellungen werden beim Starten UND beim Schliessen
                 * geschrieben. Ein Knopf, den man vergessen kann, ist eine
                 * Falle: ohne ihn waere die Einstellung beim naechsten
                 * Start wieder die alte gewesen. */
                Child, o_start = SimpleButton("Start"),
            End,
        End,
    End;

    if(!app)
    {
        Printf("ZodLaunch: could not build the user interface.\n");
        if(AslBase) { CloseLibrary(AslBase); AslBase = NULL; }
        CloseLibrary(MUIMasterBase);
        return RETURN_FAIL;
    }

    /* Ankreuzfelder auf die geladenen Werte setzen */
    for(i = 0; i < 8; i++)
        set(o_bot[i], MUIA_Selected, cfg.bot[i]);
    set(o_ton,          MUIA_Selected, cfg.ton);
    set(o_musik,        MUIA_Selected, cfg.musik);
    set(o_videos,       MUIA_Selected, cfg.videos);
    set(o_fps,          MUIA_Selected, cfg.fps);
    set(o_skalierung,   MUIA_Numeric_Value, cfg.skalierung);
    set(o_karte_an,     MUIA_Selected, cfg.nutze_einzelkarte);

    DoMethod(fenster, MUIM_Notify, MUIA_Window_CloseRequest, TRUE,
             (ULONG)app, 2, MUIM_Application_ReturnID, ID_BEENDEN);
    DoMethod(o_start, MUIM_Notify, MUIA_Pressed, FALSE,
             (ULONG)app, 2, MUIM_Application_ReturnID, ID_START);
    DoMethod(o_binaer_waehlen, MUIM_Notify, MUIA_Pressed, FALSE,
             (ULONG)app, 2, MUIM_Application_ReturnID, ID_BINAER);
    DoMethod(o_ahi_waehlen, MUIM_Notify, MUIA_Pressed, FALSE,
             (ULONG)app, 2, MUIM_Application_ReturnID, ID_AHI);
    DoMethod(o_ahi_vorgabe, MUIM_Notify, MUIA_Pressed, FALSE,
             (ULONG)app, 2, MUIM_Application_ReturnID, ID_AHI_VORGABE);
    DoMethod(o_liste_waehlen, MUIM_Notify, MUIA_Pressed, FALSE,
             (ULONG)app, 2, MUIM_Application_ReturnID, ID_LISTE);
    DoMethod(o_karte_waehlen, MUIM_Notify, MUIA_Pressed, FALSE,
             (ULONG)app, 2, MUIM_Application_ReturnID, ID_KARTE);

    set(fenster, MUIA_Window_Open, TRUE);

    while(laeuft)
    {
        ULONG id = DoMethod(app, MUIM_Application_NewInput, (ULONG)&signale);
        /* vorschau entfaellt: die Vorschau laeuft jetzt ueber startbefehl_bauen */
        char  fehler[128];

        /* Oberflaeche in die Einstellungen zurueckschreiben */
        {
            STRPTR s = NULL;
            LONG   l = 0;

            get(o_name, MUIA_String_Contents, &s);
            if(s) { strncpy(cfg.spielername, (char*)s, MAX_TEXT-1); cfg.spielername[MAX_TEXT-1] = 0; }

            get(o_liste, MUIA_String_Contents, &s);
            if(s) karte_setzen(cfg.kartenliste, (char*)s);

            get(o_karte, MUIA_String_Contents, &s);
            if(s) karte_setzen(cfg.einzelkarte, (char*)s);

            get(o_extra, MUIA_String_Contents, &s);
            if(s) { strncpy(cfg.extra, (char*)s, MAX_TEXT-1); cfg.extra[MAX_TEXT-1] = 0; }

            get(o_binaer, MUIA_String_Contents, &s);
            if(s) { strncpy(cfg.binaer, (char*)s, MAX_PFAD-1); cfg.binaer[MAX_PFAD-1] = 0; }

            get(o_team,  MUIA_Cycle_Active, &l); cfg.team       = l;
            get(o_musikweg, MUIA_Cycle_Active, &l); cfg.musikweg = l;
            get(o_variante, MUIA_Cycle_Active, &l); cfg.variante = (l == 1) ? 1 : 0;
            get(o_aufl,  MUIA_Cycle_Active, &l); cfg.aufloesung = l;

            for(i = 0; i < 8; i++) { get(o_bot[i], MUIA_Selected, &l); cfg.bot[i] = (BOOL)l; }
            get(o_ton,          MUIA_Selected, &l); cfg.ton               = (BOOL)l;
            get(o_musik,        MUIA_Selected, &l); cfg.musik             = (BOOL)l;
            get(o_videos,       MUIA_Selected, &l); cfg.videos            = (BOOL)l;
            get(o_fps,          MUIA_Selected, &l); cfg.fps               = (BOOL)l;

            get(o_skalierung, MUIA_Numeric_Value, &l);
            cfg.skalierung = l;
            {
                static char sk[16];

                snprintf(sk, sizeof(sk), "%ld,%ld",
                         (long)(cfg.skalierung / 2),
                         (long)((cfg.skalierung & 1) ? 5 : 0));
                set(o_skalierung_text, MUIA_Text_Contents, (ULONG)sk);
            }
            get(o_karte_an,     MUIA_Selected, &l); cfg.nutze_einzelkarte = (BOOL)l;

            /* PAULA BRAUCHT KEINEN AHI-MODUS -- das Feld wird ausgegraut.
             * Nur bei Aenderung setzen: MUIA_Disabled bei jedem Durchlauf
             * neu zu schreiben laesst MUI in jedem Takt neu zeichnen. */
            {
                LONG aus = (cfg.musikweg == 1) ? TRUE : FALSE;

                if(aus != ahi_aus)
                {
                    ahi_aus = aus;
                    set(o_ahi_label,  MUIA_Disabled, aus);
                    set(o_ahi_gruppe, MUIA_Disabled, aus);
                }
            }

            /* IM STORY MODE SIND KARTENWAHL UND FILMSCHALTER GESPERRT.
             * Die Kampagne ist eine feste Folge mit festen Filmen -- ein
             * Feld, das man setzen kann und das nichts tut, ist schlimmer
             * als keines. Im ZodEngine-Modus umgekehrt: dort gibt es
             * ueberhaupt keine Filme, also auch nichts anzukreuzen. */
            {
                LONG aus = (cfg.variante == 0) ? TRUE : FALSE;

                if(aus != karten_aus)
                {
                    karten_aus = aus;

                    /* Story Mode: Karten, Team und Bots liegen fest --
                     * die Kampagne ist eine feste Folge, rot gegen blau.
                     * ZodEngine-Modus umgekehrt: dort gibt es ueberhaupt
                     * keine Filme, also auch nichts anzukreuzen. */
                    set(o_karten_gruppe, MUIA_Disabled, aus);
                    set(o_team_label,    MUIA_Disabled, aus);
                    set(o_team,          MUIA_Disabled, aus);
                    set(o_bots_gruppe,   MUIA_Disabled, aus);

                    set(o_videos_label,  MUIA_Disabled, !aus);
                    set(o_videos_gruppe, MUIA_Disabled, !aus);
                }
            }
        }

        /* Die Vorschau der Befehlszeile ist entfernt (24.09.). Sie zeigte
         * den Shell-Aufruf, und danach hat nie jemand gefragt -- wohl aber
         * stand sie im Weg. Die eine Meldung, die sie auch trug ("Please
         * choose a binary first"), kommt weiterhin als Requester, wenn das
         * Starten scheitert; sie laeuft ueber dieselbe Funktion
         * (startbefehl_bauen), kann also nicht davon abweichen. */

        switch(id)
        {
        case ID_BEENDEN:
            /* Ohne Sicherungsknopf MUSS hier gesichert werden -- sonst
             * waere jede Aenderung beim naechsten Start wieder weg. */
            cfg_speichern();
            laeuft = FALSE;
            break;

        case ID_BINAER:
            if(binaer_waehlen())
                set(o_binaer, MUIA_String_Contents, (ULONG)cfg.binaer);
            break;

        case ID_AHI:
        {
            struct Window *iw = NULL;
            ULONG neu;

            get(fenster, MUIA_Window_Window, &iw);

            neu = ahi_modus_waehlen(cfg.ahi_modus, iw);

            if(neu != cfg.ahi_modus)
            {
                cfg.ahi_modus = neu;
                ahi_name_von_id(cfg.ahi_modus, ahi_anzeige, sizeof(ahi_anzeige));
                set(o_ahi, MUIA_Text_Contents, (ULONG)ahi_anzeige);
            }
            break;
        }

        case ID_AHI_VORGABE:
            cfg.ahi_modus = 0;
            ahi_name_von_id(0, ahi_anzeige, sizeof(ahi_anzeige));
            set(o_ahi, MUIA_Text_Contents, (ULONG)ahi_anzeige);
            break;

        case ID_LISTE:
            if(datei_waehlen("Select map list", "#?.txt",
                             cfg.kartenliste, MAX_TEXT))
                set(o_liste, MUIA_String_Contents, (ULONG)cfg.kartenliste);
            break;

        case ID_KARTE:
            if(datei_waehlen("Select single map", "#?.map",
                             cfg.einzelkarte, MAX_TEXT))
                set(o_karte, MUIA_String_Contents, (ULONG)cfg.einzelkarte);
            break;

        case ID_START:
            cfg_speichern();
            if(spiel_starten(fehler, sizeof(fehler)) == 0)
                laeuft = FALSE;          /* Launcher macht dem Spiel Platz */
            else
                MUI_Request(app, fenster, 0, (char*)"ZodLaunch", (char*)"Too bad",
                            (char*)fehler);
            break;

        default:
            break;
        }

        if(laeuft && signale)
        {
            signale = Wait(signale | SIGBREAKF_CTRL_C);
            if(signale & SIGBREAKF_CTRL_C) { cfg_speichern(); laeuft = FALSE; }
        }
    }

    set(fenster, MUIA_Window_Open, FALSE);
    MUI_DisposeObject(app);

    ahi_schliessen();
    if(AslBase) { CloseLibrary(AslBase); AslBase = NULL; }
    CloseLibrary(MUIMasterBase);

    return RETURN_OK;
}

#endif /* !ZOD_LAUNCH_TEST */
