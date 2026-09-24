#include <QCoreApplication>
// ===================================
#include <string>
#include <cstdlib>
using namespace std;

#ifdef _WIN32
//xgetopt stuff
//this is only needed for some compilers
#include "xgetopt.h"
int optind, opterr;
TCHAR *optarg;
//end xgetopt

#else
#include <unistd.h>
#endif

#include <lib_qZod_DnSeparate/common.h>
#include <lib_qZod_DnMap/qzod_map.h>
#include <lib_qZod_DnSoundEngine/qzod_soundengine_old.h>
#include <lib_qZod_DnSettings/qzod_settings_old.h>
#include <lib_qZod_DnGui/zgui_main_menu_base.h>
#include <lib_qZod_DnEffect/zod_effect.hpp>
#include <lib_qZod_DnObjects/zobject.h>
#include <lib_qZod_DnClientServer/zclient.h>
#include <lib_qZod_DnClientServer/zserver.h>
#include <lib_qZod_DnClientServer/zplayer.h>
#include <lib_qZod_DnClientServer/zbot.h>
#include <lib_qZod_DnClientServer/ztray.h>


#include "main_options.h"
#include <zod_wire.h>
#include <zod_log.h>
#include <zod_pack.h>
#include <zod_video.h>
#include <amiga_audio.h>
#include <amiga_startup.h>
#include "fineclock.h"
#ifdef ZOD_ALLOC_PROBE
extern "C" void zod_alloc_report(const char *tag);
#endif

//aus zsdl_opengl.cpp: beziffert beim Beenden das Treffer-Aufblitzen
//(Blits und erzeugte Silhouetten) -- frueher ein SDL_FillRect je Pixel
void zod_hit_report();


//aus zplayer.cpp: Zeitanteile je Bild. Auf dem Amiga ist die Bildrate durch
//die Doppelpufferung an den Strahlruecklauf gekoppelt und taugt deshalb NICHT
//zum Bewerten von Renderarbeit -- die gemessene Zeit schon.
void zod_time_measure_enable();
void zod_time_report(double &render, double &flip, double &process, double &socket, long &frames);
/* zplayer.cpp -- Aufteilung von `zeichnen` auf 14 Abschnitte */
extern "C" void zod_render_sections_report(long frames);
/* zsdl_opengl.cpp -- Spitzenwerte des Skalierers und Groessenhistogramm */
extern "C" void zod_roto_peak_report(unsigned long *calls, unsigned long *made,
                                     unsigned long *bytes, unsigned long *eject,
                                     unsigned long *eject_steps, unsigned long *owners);
extern "C" void zod_size_hist_report(unsigned long *h);
/* zplayer.cpp -- die schlimmsten drei Bilder, einzeln aufgeschluesselt */
extern "C" void zod_worst_report(void);
/* zplayer.cpp -- Effektliste */
extern "C" unsigned long zod_eff_mittel_get(void);
extern "C" unsigned long zod_roto_max_greift_get(void);
extern "C" int zod_roto_max_get(void);
extern "C" void zod_roto_max_set(int hundertstel);
extern "C" void zod_effect_report(unsigned long *jetzt, unsigned long *spitze,
                                  unsigned long *neu, unsigned long *weg,
                                  unsigned long *neu_spitze);
/* zsdl_opengl.cpp -- Sichttests gegen Treffer */
extern "C" void zod_sicht_report(unsigned long *tests, unsigned long *treffer);
extern "C" unsigned long zod_fenster_report(void);
/* zobject.cpp -- Histogramm der Objektarten */
extern "C" void zod_objart_report(void);
extern "C" void zod_lauf_report(void);
extern "C" void zod_pf_report(void);
#ifdef __amigaos__
extern "C" void zod_fueller_report(void);
#endif
/* zserver.cpp -- Aufteilung von `server` auf 13 Abschnitte */
extern "C" void zod_server_sections_report(long frames);
/* qzod_map.cpp -- Stueckzahlen von DoZoneEffects */
extern "C" void zod_zone_report(unsigned long *zonen, unsigned long *sichtbar,
                                unsigned long *kacheln, unsigned long *blits,
                                unsigned long *wasser, unsigned long *bilder,
                                unsigned long *gebacken);
/* zplayer.cpp -- Schlafdauer an die Bildratenanzeige melden */
extern "C" void zod_fps_add_sleep(unsigned long ticks);

/* zplayer.cpp -- Verteilung der Bildzeiten */
extern "C" void zod_frame_report(unsigned long *count, unsigned long *worst,
                                 unsigned long *o20, unsigned long *o50,
                                 unsigned long *o100, unsigned long *freq);

/* zmini_map.cpp -- was der Aufbau der Minikarte kostet */
extern "C" void zod_minimap_report(unsigned long *builds, unsigned long *ticks,
                                   unsigned long *worst, unsigned long *freq,
                                   unsigned long *statisch);

#ifdef __amigaos__
/* port/amiga/sdl_video.cpp -- wie viel die Ausgabe wirklich kopiert */
extern "C" void zod_dirty_report(unsigned long long *bytes, unsigned long *frames);
extern "C" unsigned long zod_dirty_calls_get(void);
extern "C" void zod_dirty_sweep_report(unsigned long frames);
extern "C" void zod_flip_report(void);
/* port/amiga/sdl_video.cpp -- Zaehler des Blitters */
extern "C" void zod_blit_report(unsigned long *kc, unsigned long long *kp,
                                unsigned long *fc, unsigned long long *fp,
                                unsigned long *rows);
#endif
void zod_roto_report(unsigned long &calls, unsigned long &made, unsigned long &pixels,
                     unsigned long &peak_made, unsigned long &peak_pixels);

void display_help(char *shell_command);
void display_version();
int run_server_thread(void *nothing);
int run_bot_thread(void *nothing);
void run_player_thread();
void run_single_player();
void run_tray_app();

static input_options starting_conditions;

//Zielzeit je Bild. Darueber hinaus zu rechnen bringt nichts sichtbares und
//nimmt anderen Tasks nur Rechenzeit weg; darunter wird nicht geschlafen.
#define ZOD_FRAME_TARGET (1.0 / 60.0)

static char bot_bypass_data[MAX_BOT_BYPASS_SIZE];
static int bot_bypass_size;


//int main(int argc, char *argv[])
//{
//    QCoreApplication a(argc, argv);

//    return a.exec();
//}
// =============================================================
// ***** command line for canpaing start
//      -l map_list.txt -n zlover -t red -r 800x484 -o -b blue
// =============================================================
int main(int argc, char **argv)
{
    //AmigaOS: Arbeitsverzeichnis auf PROGDIR:, damit die relativen Pfade der
    //Engine auch beim Start von der Workbench stimmen (sonst wirkungslos)
    zod_amiga_startup();
    atexit(zod_amiga_shutdown);

    //Ziel der Textausgabe (ZOD_LOG: stdout, none, serial oder Dateiname)
    zod_log_open();

    ZLOG("Welcome to the Zod Engine\n");

    //packet conversion table; must be ready before any socket is used
    zod_wire_init();

    //Asset-Archive oeffnen (fehlen sie, laedt die Engine wie bisher Einzeldateien)
    /* ZOD_PACKS waehlt ein anderes Archivverzeichnis -- damit lassen sich der
     * 8-Bit-Pfad (packs8) und der bisherige Stand (packs) vergleichen. */
    {
        const char *packs = getenv("ZOD_PACKS");

        zod_pack_init(packs && *packs ? packs : "packs");
    }
    atexit(zod_pack_shutdown);

    //Beim Beenden Geraete und Dateien schliessen. Ohne das bliebe auf dem
    //Amiga ahi.device offen und eine etwaige Logdatei ungeschlossen.
    atexit(zod_audio_close);
    atexit(zod_log_close);

    //atexit-Behandler laufen in umgekehrter Reihenfolge: hier eingetragen,
    //meldet sich das Treffer-Aufblitzen noch VOR dem Schliessen der Ausgabe
    atexit(zod_hit_report);

    //Freien Speicher festhalten, damit sich nach dem Lauf vergleichen laesst,
    //ob wirklich alles freigegeben wurde (P5-Kriterium).
    zod_mem_report("Start");
    atexit(zod_mem_report_exit);

    if(argc<=1) starting_conditions.setdefaults();

    //read in the arguments
    starting_conditions.getoptions(argc, argv);

    //Logziel aus der Kommandozeile hat Vorrang vor der Umgebung
    if(starting_conditions.read_log_target)
        zod_log_open_name(starting_conditions.log_target.c_str());

    /* FEHLT DIE KARTENLISTE, WIRD HIER ABGEBROCHEN -- und zwar VOR dem
     * Bildschirm.
     *
     * Ohne diese Pruefung laeuft die Engine weiter: ReadMapList scheitert,
     * die Liste bleibt leer, LoadNextMap meldet "no map set to load", und
     * danach steht ein SCHWARZER SCHIRM ohne Karte. Auf dem Host laeuft
     * das durch, auf dem Amiga half dem Nutzer nur noch ein Reboot -- das
     * Spiel liess sich nicht einmal mehr beenden.
     *
     * Die drei Meldungen dazu gab es schon; auf echter Hardware sieht sie
     * nur niemand. Ein sauberer Abbruch ist deshalb besser als eine
     * Protokollzeile: dann ist wenigstens klar, DASS etwas fehlt.
     *
     * Ausgeloest hat es eine Handinstallation, bei der story_list.txt
     * nicht mitkopiert wurde. Die Datei fehlt dem Spiel also erst beim
     * Start, nicht beim Installieren -- genau die Sorte Fehler, die man
     * nicht dort sucht, wo sie entstanden ist. */
    if(starting_conditions.read_map_list)
    {
        FILE *pruef = fopen(starting_conditions.map_list.c_str(), "r");
        int   karten = 0;

        if(pruef)
        {
            char zeile[512];

            while(fgets(zeile, sizeof(zeile), pruef))
                if(strstr(zeile, ".map")) karten++;

            fclose(pruef);
        }

        if(!pruef || !karten)
        {
            ZLOG("ABBRUCH: Kartenliste '%s' %s.\n",
                 starting_conditions.map_list.c_str(),
                 pruef ? "enthaelt keine Karte" : "gibt es nicht");
            ZLOG("Ohne Karten laeuft das Spiel mit schwarzem Schirm weiter,\n");
            ZLOG("deshalb wird hier abgebrochen. Liegt die Datei neben dem\n");
            ZLOG("Programm? story_list.txt und map_list.txt gehoeren beide\n");
            ZLOG("dorthin.\n");

            return 20;   /* AmigaDOS: RETURN_FAIL */
        }
    }

    /* Ausgabeweg der Musik aus -M. Muss VOR dem ersten Mix_LoadMUS stehen:
     * die Musikausgabe wird beim ERSTEN geladenen Stueck aufgesetzt, und
     * danach ist der Weg festgelegt. */
#ifdef __amigaos__
    if(starting_conditions.read_musik_weg)
    {
        extern int zod_musik_weg;

        zod_musik_weg = starting_conditions.musik_weg;

        ZLOG("Musik: Ausgabeweg aus der Befehlszeile: %s\n",
             zod_musik_weg ? "Paula" : "AHI");
    }
#endif

    /* Zwischensequenzen einrichten. Bild- und Tonweg kommen aus DENSELBEN
     * Einstellungen wie das Spiel -- der Launcher stellt sie einmal ein,
     * und der Player folgt. Sonst liefe der Film ueber AHI, waehrend das
     * Spiel Paula benutzt, und der Nutzer saehe zwei verschiedene
     * Tonwege fuer dasselbe Programm. */
    {
        int breit = starting_conditions.read_resolution
                  ? starting_conditions.resolution_width : 640;

        /* Stumm, wenn BEIDES aus ist. Nur -s (Effekte aus) laesst die
         * Filmtonspur laufen: sie ist keine Effektspur. */
        int ton = (starting_conditions.read_sound_off &&
                   starting_conditions.read_music_off)
                ? 0
                : (starting_conditions.musik_weg ? 2 : 1);

        zod_video_setup(starting_conditions.read_video_dir
                            ? starting_conditions.video_dir.c_str() : 0,
                        breit, ton, 0 /* AGA kann die Engine noch nicht */);
    }

    /* VORSPANN. Hier und nicht spaeter, weil zu diesem Zeitpunkt weder
     * Schirm noch Ton der Engine offen sind -- es gibt also nichts
     * freizugeben und nichts wiederherzustellen. Im Original laeuft
     * genau diese Folge: logo, dann intro. */
    /* Die Variante gehoert ins Protokoll. Ein Schalter, der das Verhalten
     * aendert und sich nicht meldet, ist von einem kaputten nicht zu
     * unterscheiden -- dieselbe Regel wie bei ZOD_SHOT und den
     * Messbetrieben. */
    ZLOG("Bildrate: %s\n", starting_conditions.fps_an ? "sichtbar" : "aus");

    ZLOG("Spielvariante: %s\n", starting_conditions.story_modus
         ? "Original Z Story Mode (Filme und Statistik)"
         : "ZodEngine Mode (keine Filme, keine Statistik)");

    /* NUR IM STORY-MODUS. Der ZodEngine-Modus soll sich verhalten wie die
     * Vorlage, und die hat keine Filme -- auch keinen Vorspann. */
    if(zod_video_an() && starting_conditions.story_modus)
    {
        /* ALLE DREI IN EINEM AUFRUF. Je Aufruf oeffnet und schliesst der
         * Player seinen Vollbildschirm -- drei Aufrufe waeren drei
         * Modewechsel mit einem Aufblitzen dazwischen. Vom Nutzer
         * gemeldet: "der Amiga macht zwischen jeden Video ein
         * Screenwechsel oder resync".
         *
         * intro2 ist der EINE Punkt der Folge, der sich aus den Daten
         * nicht beweisen laesst. logo/intro stehen im Desert-Block der
         * Tabelle in ZED.EXE, intro2/creds im Volcanic-Block -- die
         * Paarung sagt nichts ueber die Reihenfolge. Name und Laenge
         * (6,9 MB gegen 26,8 MB bei intro) sprechen fuer einen zweiten
         * Teil des Vorspanns. */
        /* DREI Filme, und e_dplnt gehoert NICHT dazu.
         *
         * e_intro2 IST die Ankunft auf dem Wuestenplaneten: Anflug, roter
         * Planet, Eintritt in die Atmosphaere, Flug ueber Duenen, Landung.
         *
         * e_dplnt dagegen ist der ABSCHIED von der Wueste -- es endet mit
         * der Briefing-Tafel "VOLCANIC PLANET" und gehoert deshalb hinter
         * die Wuestenkampagne, nicht davor. Hier stand es zwischenzeitlich
         * trotzdem; der Nutzer hat es gemeldet. */
        static const char *vorspann[] = { "e_logo", "e_intro", "e_intro2" };

        zod_video_play_folge(vorspann, 3);
    }

    /* AHI-Audiomodus aus -A. Muss VOR dem ersten Mix_OpenAudio stehen, das
     * die Engine spaeter in ZPlayer::Init ausloest. */
    if(starting_conditions.read_ahi_mode)
    {
        ZLOG("AHI: Modus aus der Befehlszeile: 0x%08lx\n",
             starting_conditions.ahi_mode);
        zod_audio_set_mode(starting_conditions.ahi_mode);
    }

    //make sure there is nothing conflicting,
    //like we are trying to make a dedicated server that is supposed to connect to another server
    starting_conditions.checkoptions();

    //init this for the bots
    ZCore::CreateRandomBotBypassData(bot_bypass_data, bot_bypass_size);

    //now see what we have
    if(starting_conditions.read_display_version) display_version();
    if(starting_conditions.read_display_help) display_help(argv[0]);

    //now what do we really run?
    else if(starting_conditions.read_run_tray)
    {
        run_tray_app();
    }
    else if(starting_conditions.read_is_dedicated)
    {
        //run only a server
        //server_thread = SDL_CreateThread(run_server_thread, nullptr);
        run_server_thread(nullptr);
    }
    else if(starting_conditions.read_connect_address)
    {
        //connect to a server
        run_player_thread();
    }
    else
    {
        //Server und Spieler in einem Task
        run_single_player();
    }

    //sauberes Ende meldet 0; das Original gab immer 1 zurueck und liess damit
    //jeden Lauf wie einen Fehlschlag aussehen
    return 0;
}

//Server konfigurieren (gemeinsam fuer dedizierten Server und Singleplayer)
static void configure_server(ZServer &zserver)
{
    int i;

    for(i=0;i<MAX_TEAM_TYPES;i++)
        if(starting_conditions.read_start_bot[i])
        {
            zserver.InitBot(i);

            //int *team = new int;
            //ZLOG("start bot for team %s\n", team_type_string[i].c_str());

            //*team = i;
            //bot_thread.push_back(SDL_CreateThread(run_bot_thread, (void*)team));
        }

    if(starting_conditions.read_map_name)
        zserver.SetMapName(starting_conditions.map_name);
    else if(starting_conditions.read_map_list)
        zserver.SetMapList(starting_conditions.map_list);

    if(starting_conditions.read_settings)
        zserver.SetSettingsFilename(starting_conditions.settings_filename);
    if(starting_conditions.read_p_settings)
        zserver.SetPerpetualSettingsFilename(starting_conditions.p_settings_filename);

    zserver.SetBotBypassData(bot_bypass_data, bot_bypass_size);
}

int run_server_thread(void *nothing)
{
    ZServer zserver;

    configure_server(zserver);
    zserver.Setup();
    zserver.Run();

    return 1;
}

int run_bot_thread(void *nothing)
{
    int *team;
    ZBot zbot;

    team = (int*)nothing;

    zbot.SetDesiredTeam((team_type)*team);
    if(starting_conditions.read_connect_address)
        zbot.SetRemoteAddress(starting_conditions.connect_address);

    zbot.SetBotBypassData(bot_bypass_data, bot_bypass_size);

    zbot.Setup();
    zbot.Run();

    return 1;
}

//Spieler konfigurieren (gemeinsam fuer Client und Singleplayer)
static void configure_player(ZPlayer &zplayer)
{
    zplayer.DisableCursor(starting_conditions.read_disable_zcursor);
    zplayer.SetSoundsOff(starting_conditions.read_sound_off);
    zplayer.SetMusicOff(starting_conditions.read_music_off);
    zplayer.SetWindowed(starting_conditions.read_is_windowed);
    zplayer.SetUseOpenGL(!starting_conditions.read_opengl_off);
    if(starting_conditions.read_player_team)
        zplayer.SetDesiredTeam((team_type)starting_conditions.team);
    if(starting_conditions.read_player_name)
        zplayer.SetPlayerName(starting_conditions.player_name);
    if(starting_conditions.read_loginname)
        zplayer.SetLoginName(starting_conditions.loginname);
    if(starting_conditions.read_password)
        zplayer.SetLoginPassword(starting_conditions.password);
    if(starting_conditions.read_connect_address)
        zplayer.SetRemoteAddress(starting_conditions.connect_address);
    if(starting_conditions.read_resolution)
        zplayer.SetDimensions(starting_conditions.resolution_width, starting_conditions.resolution_height);
    //nur setzen, wenn ausdruecklich verlangt -- sonst bliebe die
    //plattformabhaengige Vorgabe aus dem Konstruktor wirkungslos
    if(starting_conditions.read_single_buffer)
        zplayer.SetSingleBuffer(true);
    if(starting_conditions.read_no_scaler)
        zplayer.SetNoScaler(true);

    /* Vor zplayer.Setup(), damit ZPlayer::InitSDL die Umgebungsvariable nur
     * dann noch liest, wenn die Option NICHT angegeben wurde. */
    if(starting_conditions.read_scale_max)
        zod_roto_max_set(starting_conditions.read_scale_max);
}

void run_player_thread()
{
    ZPlayer zplayer;

    configure_player(zplayer);

    zplayer.Setup();
    zplayer.Run();
}

//Singleplayer: Server, Bots und Spieler laufen abwechselnd in EINEM Task.
//Damit entfallen alle Threads (AmigaOS-Multitasking bleibt erhalten, weil
//uni_pause den Task schlafen legt statt zu pollen).
void run_single_player()
{
    ZServer zserver;
    ZPlayer zplayer;

    configure_server(zserver);
    zserver.Setup();

    //Spieler ohne TCP an den Server im selben Prozess haengen
    zplayer.SetLoopbackServer(&zserver.GetServerSocket());

    configure_player(zplayer);

    /* Welcher Planet? Der Klient weiss es erst, wenn die Karte ueber den
     * Socket da ist -- und das ist nach dem ganzen Laden. Der Server hat
     * sie hier aber schon (zserver.Setup() steht darueber). Nur fuer die
     * Auswahl des Ladebilds; an der Spiellogik haengt daran nichts. */
    zplayer.SetStoryModus(starting_conditions.story_modus);
    zplayer.SetFpsAnzeige(starting_conditions.fps_an);
    zplayer.SetLadePlanet(zserver.GetTerrainType());
    zplayer.SetLadeName(zserver.GetMapFile().c_str());

    zplayer.Setup();

    //Zeitmessung je Bild nur im Messlauf
    const bool measure = starting_conditions.read_bench;

    if(measure) zod_time_measure_enable();

    double start_time = current_time();
    long frames = 0;

    //Servertakt und feste Pause liegen AUSSERHALB von ZPlayer::Tick und fehlten
    //deshalb in der Bilanz: auf dem Amiga waren von 16863 us je Bild nur 1800
    //erklaert. Diese beiden schliessen die Luecke.
    //Beide Posten ueber die E-Clock, nicht ueber current_time(): das ist auf
    //dem Amiga DateStamp() mit 20 ms Aufloesung (siehe zplayer.cpp).
    unsigned long acc_server = 0;
    unsigned long acc_pause = 0;

    //Kleinste Schlafdauer ermitteln, statt sie zu raten: uni_pause() ist
    //usleep(), und die Systeme runden sehr verschieden auf. Gemessen wurden
    //auf dem Amiga 14,8 ms fuer angeforderte 2 ms (88 Prozent jedes Bildes!),
    //auf dem Host rund 2,2 ms.
    double sleep_grain = 0.0;

    for(int i = 0; i < 3; i++)
    {
        double probe_start = current_time();

        uni_pause(1);

        double probe = current_time() - probe_start;

        if(probe > sleep_grain) sleep_grain = probe;
    }

    ZLOG("kleinste Schlafdauer: %ld us\n", (long)(sleep_grain * 1000000.0));

    //Fuer die Restbudget-Rechnung unten. 1, wenn keine feine Uhr da ist --
    //dann ist `left` Unsinn, aber es teilt wenigstens nicht durch null.
    const double clock_freq = zod_fineclock_freq() ? (double)zod_fineclock_freq() : 1.0;

    //Rueckwege fuer den A/B-Vergleich im selben Binary
    bool schlaf_immer = false;
    bool schlaf_nie   = false;
    {
        char buf[16];
        const char *e = zod_env("ZOD_SCHLAF", buf, sizeof(buf));

        if(e && (*e == 'i' || *e == 'I')) schlaf_immer = true;
        if(e && (*e == 'n' || *e == 'N')) schlaf_nie   = true;
    }

    ZLOG("Schlafen: %s\n",
         schlaf_nie   ? "nie (nur Messung)"
       : schlaf_immer ? "immer wenn Budget uebrig (alt)"
                      : "nur wenn das Budget einen ganzen Schlafschritt deckt");

    //Was die Messung selbst kostet. `zeichnen` wird in 14 Abschnitte geteilt,
    //das sind 15 Blicke auf die Uhr je Bild -- bei einem teuren Zeitabruf
    //maesse die Sonde sich selbst. Deshalb beziffert, nicht angenommen.
    //
    //Der Aufruf geht ueber die timer.device (ReadEClock); auf dem Host ist es
    //gettimeofday. 1000 Abrufe, damit die Gesamtdauer weit ueber der
    //Aufloesung liegt.
    {
        const unsigned long cfreq = zod_fineclock_freq();

        //ACHTUNG: Hier stand `cfreq / 1000000UL` als Teiler. Auf dem Host ist
        //die "Frequenz" genau 1000000 (us), das ergibt 1 und geht gut. Die
        //E-Clock des Amiga hat aber nur 709379 Schritte je Sekunde -- der
        //Teiler wird 0, und der Start endete mit **Guru 80000005**
        //(Division durch Null). Deshalb wird durch `cfreq / 1000` geteilt,
        //und die Schranke unten prueft genau das.
        if(cfreq >= 1000)
        {
            const unsigned long t0 = zod_fineclock_ticks();

            for(int i = 0; i < 1000; i++) zod_fineclock_ticks();

            const unsigned long d = zod_fineclock_ticks() - t0;

            //Erst teilen, dann malnehmen -- sonst Ueberlauf auf 32 Bit.
            //Bei genau 1000 Abrufen ist die Gesamtdauer in us zugleich die
            //Dauer eines einzelnen Abrufs in ns.
            ZLOG("Zeitabruf: %ld ns je Aufruf, %ld Schritte je Sekunde\n",
                 (long)((d * 1000UL) / (cfreq / 1000UL)),
                 (long)cfreq);
        }
        else ZLOG("Zeitabruf: KEINE feine Uhr -- Messwerte je Bild ungueltig\n");
    }

#ifdef ZOD_ALLOC_PROBE
    double next_alloc_report = current_time();
#endif

    while(zplayer.Running() && zserver.Running())
    {
        double frame_start = current_time();
        const unsigned long frame_start_ticks = zod_fineclock_ticks();
        unsigned long t_mark = frame_start_ticks;

#ifdef ZOD_ALLOC_PROBE
        if(frame_start >= next_alloc_report)
        {
            zod_alloc_report("takt");
            next_alloc_report = frame_start + 15.0;
        }
#endif

        zserver.Tick();

        if(measure) acc_server += zod_fineclock_ticks() - t_mark;

        zplayer.Tick();
        frames++;

        //AmigaOS: Strg-C beendet sauber (auf anderen Systemen wirkungslos)
        if(zod_amiga_break())
        {
            ZLOG("Strg-C erkannt, beende\n");
            break;
        }

        if(starting_conditions.read_bench)
        {
            double run_time = current_time() - start_time;

            if(run_time >= starting_conditions.bench_seconds)
            {
                //Achtung: der serielle Kanal (RawDoFmt) kann kein %f,
                //deshalb die Bildrate verzehnfacht als Ganzzahl.
                long fps10 = run_time > 0 ? (long)((frames * 10.0) / run_time) : 0;

                //Zeitanteile VOR der [OK]-Zeile: tools/run.sh bricht beim
                //ersten [OK] ab, danach kaeme nichts mehr durch.
                double r, f, p, s;
                long tframes;

                zod_time_report(r, f, p, s, tframes);

                //zeichnen OHNE das Umschalten: SDL_Flip steckt innerhalb der
                //Zeichenfunktion, seine Wartezeit waere sonst doppelt gezaehlt
                if(r > f) r -= f;

                if(tframes > 0)
                {
                    ZLOG("Zeit je Bild in us: zeichnen=%ld umschalten=%ld spiel=%ld netz=%ld (Bilder=%ld)\n",
                         (long)(r * 1000000.0 / tframes),
                         (long)(f * 1000000.0 / tframes),
                         (long)(p * 1000000.0 / tframes),
                         (long)(s * 1000000.0 / tframes),
                         tframes);

                    //Wohin die Zeit im Zeichnen geht. Die Summe dieser 14
                    //Zeilen muss `zeichnen` ergeben -- fehlt etwas, ist es
                    //nicht eingeklammert und die Suche geht dort weiter.
                    zod_render_sections_report(tframes);

                    /* Der Mittelwert kann einen Einbruch nicht zeigen. */
                    zod_worst_report();

                    {
                        unsigned long zz, zs, zk, zb, zw, zf, zg;

                        zod_zone_report(&zz, &zs, &zk, &zb, &zw, &zf, &zg);

                        /* Je Bild, damit es neben den us-Angaben steht.
                         * "gezeichnet" sind jetzt nur noch die Wassermarker;
                         * die uebrigen stecken in der Kartenflaeche und
                         * erscheinen als Einbackvorgaenge im GANZEN Lauf. */
                        if(zf)
                            ZLOG("  Zonenmarker je Bild: %ld von %ld Zonen sichtbar, "
                                 "%ld Kacheln geprueft, %ld Wasser gezeichnet; "
                                 "%ld mal eingebacken im Lauf\n",
                                 (long)(zs / zf), (long)(zz / zf),
                                 (long)(zk / zf), (long)(zw / zf), (long)zg);
                    }
                }

                if(frames > 0)
                {
                    const double cfreq = (double)zod_fineclock_freq();

                    if(cfreq > 0.0)
                    {
                        ZLOG("Zeit je Bild in us: server=%ld pause=%ld (Bildzeit=%ld)\n",
                             (long)(acc_server * 1000000.0 / cfreq / frames),
                             (long)(acc_pause * 1000000.0 / cfreq / frames),
                             (long)(run_time * 1000000.0 / frames));

                        //Wohin die Zeit im Servertakt geht. Wie beim Zeichnen
                        //muss die Summe den Posten oben ergeben -- fehlt etwas,
                        //liegt es ausserhalb jeder Klammer.
                        zod_server_sections_report(frames);

                        {
                            unsigned long st, str;

                            zod_sicht_report(&st, &str);

                            /* Wie viel von der Objektschleife ueberhaupt
                             * sichtbar ist. Klaert, ob ein Sichtriegel lohnt. */
                            if(frames > 0)
                                ZLOG("  Sichttests je Bild: %ld, davon sichtbar %ld (%ld %%)\n",
                                     (long)(st / frames), (long)(str / frames),
                                     (long)(st ? (str * 100UL) / st : 0));

                            /* Gegenprobe, dass der Zuschnitt auf die Karte
                             * gesetzt ist und greift. Bei einer Karte, die
                             * den Ausschnitt fuellt, ist 0 richtig. */
                            ZLOG("  Kartenfenster: %ld mal beschnitten\n",
                                 (long)zod_fenster_report());
                        }

                        zod_objart_report();
                        zod_lauf_report();
                        zod_pf_report();
#ifdef __amigaos__
                        zod_fueller_report();
#endif
                    }
                }

                unsigned long rc, rm, rp, rpm, rpp;

                zod_roto_report(rc, rm, rp, rpm, rpp);

                //Skalierer: Anfragen, davon wirklich gerechnet, und wie viele
                //Bildpunkte das waren. Mit -N muessen "gerechnet" und
                //"Bildpunkte" bei den Truemmern auf 0 fallen -- was bleibt,
                //ist das einmalige Drehen der Geschosse und der Voegel.
                ZLOG("Skalierer: %ld Anfragen, %ld gerechnet, %ld Bildpunkte\n",
                     (long)rc, (long)rm, (long)rp);

#ifdef __amigaos__
                {
                    unsigned long kc, fc, rows;
                    unsigned long long kp, fp;

                    zod_blit_report(&kc, &kp, &fc, &fp, &rows);

                    //Der Kandidat fuer AMMX: 8->8 MIT Farbschluessel, je
                    //Bildpunkt eine Abfrage. Ohne Schluessel ist es memcpy
                    //je Zeile und damit kein Ziel.
                    /* Punkte in Millionen: RawDoFmt kennt nur 32 Bit, und
                     * die Summen sprengen das laengst (frueher stand dort
                     * eine negative Zahl). */
                    ZLOG("Blit 8->8: %ld mit Schluessel (%ld Mio. Punkte), "
                         "%ld ohne (%ld Mio. Punkte), %ld Zeilen gesamt\n",
                         (long)kc, (long)(kp / 1000000ULL),
                         (long)fc, (long)(fp / 1000000ULL), (long)rows);
                }

                {
                    unsigned long long db;
                    unsigned long df;

                    zod_dirty_report(&db, &df);

                    /* Wie viel die Ausgabe wirklich kopiert hat. Ohne
                     * Schmutzspur waeren es w*h Byte je Bild. */
                    if(df)
                    {
                        const unsigned long dc = zod_dirty_calls_get();

                        ZLOG("Ausgabe: %ld Byte je Bild kopiert (von %ld moeglichen)\n",
                             (long)(db / df),
                             (long)(starting_conditions.resolution_width *
                                    starting_conditions.resolution_height));
                        ZLOG("Ausgabe: %ld Kopieraufrufe je Bild, %ld Byte je Aufruf\n",
                             (long)(dc / df), (long)(dc ? db / dc : 0));
                        zod_flip_report();
                        zod_dirty_sweep_report(df);
                    }
                }
#endif

                {
                    unsigned long fc, fw, o20, o50, o100, ff;

                    zod_frame_report(&fc, &fw, &o20, &o50, &o100, &ff);

                    /* ACHTUNG, hier stand "fw * 1000000UL / ff". Das laeuft auf
                     * dem Amiga UEBER: die E-Clock hat 709379 Schritte je
                     * Sekunde, ein Bild von 14,7 ms sind 10427 Ticks, und
                     * 10427 * 1000000 passt nicht in 32 Bit. Gemeldet wurden
                     * dadurch 476 us fuer ein Bild, dessen Mittelwert bei
                     * 14763 us lag -- offensichtlicher Unsinn, der nur
                     * auffiel, weil beide Zahlen nebeneinander standen.
                     * Erst teilen, dann malnehmen: laeuft erst ab 6 Sekunden
                     * je Bild ueber. */
                    if(ff >= 1000 && fc)
                        ZLOG("Bildzeiten: %ld Bilder, laengstes %ld us, "
                             "ueber 20ms: %ld, ueber 50ms: %ld, ueber 100ms: %ld\n",
                             (long)fc, (long)((fw * 1000UL) / (ff / 1000UL)),
                             (long)o20, (long)o50, (long)o100);
                }

                {
                    unsigned long mb, mt, mw, mf, ms;

                    zod_minimap_report(&mb, &mt, &mw, &mf, &ms);

                    /* Gleicher Ueberlauf wie oben -- erst teilen, dann mal. */
                    if(mf >= 1000 && mb)
                        ZLOG("Minikarte: %ld Aufbauten, im Mittel %ld us, "
                             "laengster %ld us; davon %ld mal der unbewegliche Teil\n",
                             (long)mb,
                             (long)(((mt / mb) * 1000UL) / (mf / 1000UL)),
                             (long)((mw * 1000UL) / (mf / 1000UL)),
                             (long)ms);
                }

                //Der Mittelwert taeuscht -- Ruckeln ist die SPITZE. Ein
                //sterbendes Fort erzeugt Dutzende Flaechen in EINEM Bild.
                ZLOG("Skalierer Spitze je Bild: %ld Flaechen, %ld Bildpunkte\n",
                     (long)rpm, (long)rpp);

                {
                    unsigned long pc, pm, pb, pe, pes, po;

                    zod_roto_peak_report(&pc, &pm, &pb, &pe, &pes, &po);

                    /* Die TREFFERQUOTE in der Spitze -- bisher unbekannt.
                     * Ohne sie sagt "68 Flaechen" nicht, ob 70 oder 700
                     * Anfragen dahinterstanden. */
                    ZLOG("Skalierer Spitze: %ld Anfragen, %ld gerechnet (%ld %% Treffer), %ld Byte\n",
                         (long)pc, (long)pm,
                         (long)(pc ? ((pc - pm) * 100UL) / pc : 0), (long)pb);
                    ZLOG("Drehspeicher: %ld Besitzer, %ld Verdraengungen gesamt, "
                         "%ld je Bild (Spitze), %ld Suchschritte gesamt\n",
                         (long)po, (long)pe, (long)pe, (long)pes);
                }

                {
                    unsigned long h[8];

                    zod_size_hist_report(h);

                    /* Wo liegen die verlangten Groessen wirklich? Der Nutzer
                     * sieht den Einbruch, WAEHREND die Truemmer oben sind --
                     * und die Hoehe ist die Groesse. Die Arbeit waechst mit
                     * dem Quadrat. */
                    ZLOG("Groessen verlangt: <1:%ld 1-2:%ld 2-3:%ld 3-4:%ld "
                         "4-5:%ld 5-6:%ld 6-7:%ld 7+:%ld\n",
                         (long)h[0], (long)h[1], (long)h[2], (long)h[3],
                         (long)h[4], (long)h[5], (long)h[6], (long)h[7]);

                if(zod_roto_max_get())
                    /* Die Stueckzahl NEBEN dem Deckel -- sonst ist "Deckel an"
                     * nicht von "Deckel greift nie" zu unterscheiden. */
                    ZLOG("Groessendeckel hat %ld mal geklemmt\n",
                         (long)zod_roto_max_greift_get());
                }

                {
                    unsigned long ej, esp, en, ew, enp;

                    zod_effect_report(&ej, &esp, &en, &ew, &enp);

                    /* ACHTUNG bei "ueber Warteschlange": Es gibt ZWEI Wege in
                     * die Effektliste. Effekte, die Effekte erzeugen, gehen
                     * ueber new_effect_list (ZEffect::effect_list, zplayer.cpp
                     * :268) -- die zaehlt diese Zahl. Objekte, die beim Sterben
                     * Effekte werfen (also das FORT), schieben ueber
                     * ZObject::effect_list DIREKT in die echte Liste
                     * (zplayer_events.cpp:673) und sind hier NICHT enthalten.
                     * Deshalb ist "freigegeben" groesser. Massgeblich ist die
                     * Spitze der Listengroesse -- die stimmt. */
                    /* Der MITTELWERT steht bewusst neben der Spitze: erst
                     * mit ihm laesst sich `zeichnen.effekte` in "je Effekt"
                     * umrechnen, und das ist die Groesse, um die es beim
                     * groessten verbliebenen Posten geht. */
                    ZLOG("Effekte: %ld lebend am Ende, %ld je Bild im Mittel, "
                         "Spitze %ld je Bild; "
                         "%ld ueber Warteschlange erzeugt (ohne die direkten), "
                         "%ld freigegeben, Spitze %ld je Bild\n",
                         (long)ej, (long)zod_eff_mittel_get(), (long)esp,
                         (long)en, (long)ew, (long)enp);
                }

                ZLOG("[OK] bench frames=%ld sek=%ld fps10=%ld\n",
                     frames, (long)run_time, fps10);
                break;
            }
        }

        //Schlafen, WENN im Zeitbudget noch etwas uebrig ist -- aber immer nur um
        //den kleinstmoeglichen Betrag, nie um den ganzen Rest.
        //
        //Grund, gemessen: AmigaOS legt den Task nicht um die angeforderte Dauer
        //schlafen, sondern bis zur naechsten Grenze eines 20-ms-Rasters (die
        //Diagnosezeile "kleinste Schlafdauer" weist 20000 us aus). Ein Wunsch
        //von 2 ms landet damit auf der naechsten Grenze, im Mittel nach 14,8 ms;
        //ein Wunsch von 14 ms ueberspringt diese Grenze und landet auf der
        //uebernaechsten -- gemessen 28,9 ms. Je mehr Budget uebrig ist, desto
        //teurer waere also ein "schlafe um den Rest": ein Versuch damit brach
        //die Bildrate von 59,3 auf 32,5 fps ein.
        //
        //Der eigentliche Gewinn steckt ohnehin nicht in der Dauer, sondern im
        //Weglassen: Vorher wurde mit festem uni_pause(2) IMMER geschlafen, auch
        //wenn die Arbeit das Budget laengst ueberschritten hatte. Auf langsamer
        //Maschine -- also auf echter Hardware -- kamen so 14,8 ms auf JEDES Bild
        //obendrauf. Unter dem MMU-Profil ohne Uebersetzung (91 ms je Bild, dem
        //Verhalten echter Hardware am naechsten) stieg die Bildzahl dadurch von
        //177 auf 220, also um 24 Prozent.
        //ZWEI FEHLER, beide am 19.09. gemessen statt gelesen:
        //
        //1. `left` wurde mit current_time() gerechnet -- auf dem Amiga
        //   DateStamp() mit 20 ms Aufloesung. Die Differenz war damit 0 ODER
        //   20000 us, nie etwas dazwischen. Die Bedingung `left > 0` hing
        //   also allein daran, ob waehrend des Bildes zufaellig ein Tick
        //   umsprang, und hatte mit dem Restbudget nichts zu tun. Bei 16,4 ms
        //   Bildern trifft das rechnerisch 18 % der Bilder; gemessen waren es
        //   11,8 % (pause 2354 us / 20000).
        //
        //2. `sleep_grain` wurde oben gemessen, in der Diagnosezeile gemeldet
        //   -- und dann NIE benutzt. Dabei ist genau das die entscheidende
        //   Groesse: Deckt das Restbudget den kleinstmoeglichen Schlaf nicht,
        //   macht das Schlafen das Bild LANGSAMER, als es ohne war. Auf dem
        //   Amiga wurde aus einem Bild mit 0,2 ms Restbudget ein Bild von
        //   36 ms, also 27 statt 61 fps.
        //
        //Was das kostet, ist belegt (A/B ueber ZOD_ZONEN, V1200, 19.09.): Die
        //Arbeit je Bild sank um 2193 us, angekommen sind 1269 -- weil
        //schnellere Bilder OEFTER Restbudget lassen und jedes Schlafen 20 ms
        //kostet (pause 2354 -> 3278). Vom Gewinn kam die Haelfte an.
        //
        //Die Regel lautet deshalb: schlafen nur, wenn ein ganzer Schlafschritt
        //auch wirklich gedeckt ist.
        //
        //Einzeln betrachtet ist er das auf dem Amiga NIE: Das Raster (20 ms)
        //ist groesser als das ganze Bildbudget (16,7 ms). Ein erster Stand
        //hat daraus "also nie schlafen" gemacht -- und damit den 60-fps-Deckel
        //ganz abgeschafft. Im Emulator lief das Spiel danach mit **436 fps**
        //bei 2,3 ms Arbeit je Bild, also Vollgas ohne ein einziges zusaetzlich
        //sichtbares Bild. Auf der V1200 faellt das nicht auf (16,4 ms Arbeit,
        //ohnehin rund 61 fps), im Menue, beim Laden und auf schnellerer
        //Hardware (PiStorm) sehr wohl.
        //
        //Folge auf dem Amiga: Dort ist das Raster groesser als das ganze
        //Bildbudget, im Spiel wird also nicht mehr geschlafen und die Maschine
        //laeuft mit dem, was sie kann. Das blockiert nichts -- AmigaOS
        //verdraengt Tasks praeemptiv, andere kommen weiterhin dran.
        //
        //HIER STAND EIN ANGESPARTES GUTHABEN, das den 60-fps-Deckel ueber
        //mehrere Bilder hinweg halten sollte. Zurueckgenommen, auf Nachfrage
        //des Nutzers -- und die Frage war berechtigt:
        //
        //  * Auf der Zielhardware greift der Deckel ohnehin nie (16,4 ms
        //    Arbeit je Bild), er kostet dort also nur.
        //  * Gebaut hatte ich ihn gegen 436 fps im EMULATOR -- ein Zustand,
        //    den auf echter Hardware niemand gemeldet hat.
        //  * Und er war schon im ersten Anlauf falsch: Abgebucht wurde ein
        //    ganzer Schritt (20 ms), tatsaechlich geschlafen wurde bis zur
        //    naechsten Rastergrenze, also im Mittel die HALBE Rasterweite
        //    (gemessen 11 977 us). Der Deckel landete dadurch bei 85 statt
        //    60 fps.
        //
        //Merke zu dieser Zeile: "kleinste Schlafdauer" ist ein HOECHSTWERT aus
        //drei Proben, kein Preis je Schlaf. Wer damit Guthaben verrechnet,
        //rechnet mit dem Doppelten des Verbrauchs.
        //
        //`SetEnv ZOD_SCHLAF immer` stellt das alte Verhalten her, `nie`
        //schaltet das Schlafen ganz ab -- beides fuer den A/B im selben Binary.
        const double left = ZOD_FRAME_TARGET
                            - (double)(zod_fineclock_ticks() - frame_start_ticks)
                              / (double)clock_freq;

        const bool schlafen = schlaf_nie   ? false
                            : schlaf_immer ? (left > 0.0)
                                           : (left >= sleep_grain);

        if(schlafen)
        {
            if(measure) t_mark = zod_fineclock_ticks();

            /* Der Bildratenanzeige die Schlafdauer melden: Sie zeigt die
             * ARBEIT je Bild, nicht die Wanduhrzeit. Sonst maesse sie die
             * Taktbremse -- die Zahl kaeme nie ueber 60, und jede
             * Verbesserung verschwaende im Schlaf. */
            const unsigned long s_vor = zod_fineclock_ticks();

            uni_pause(1);

            zod_fps_add_sleep(zod_fineclock_ticks() - s_vor);

            if(measure) acc_pause += zod_fineclock_ticks() - t_mark;
        }
    }
}

void run_tray_app()
{
    ZTray ztray;

    if(starting_conditions.read_connect_address)
        ztray.SetRemoteAddress(starting_conditions.connect_address);

    ztray.Setup();
    ztray.Run();
}

void display_help(char *shell_command)
{
    ZLOG("\n==================================================================\n");
    ZLOG("Command list...\n");
    ZLOG("-c ip_address        - game host address\n");
    ZLOG("-m filename          - map to be used\n");
    ZLOG("-l filename          - map list to be used\n");
    ZLOG("-z filename          - settings file to be used\n");
    ZLOG("-e filename          - main server settings file to be used\n");
    ZLOG("-n player_name       - your player name\n");
    ZLOG("-g login_name        - your login name\n");
    ZLOG("-i login_password    - your login password\n");
    ZLOG("-t team              - your team\n");
    ZLOG("-b team              - connect a bot player\n");
    ZLOG("-w                   - run game in windowed mode\n");
    ZLOG("-r resolution        - resolution to run the game at\n");
    ZLOG("-d                   - run a dedicated server\n");
    ZLOG("-h                   - display command help\n");
    ZLOG("-s                   - no sound\n");
    ZLOG("-u                   - no music\n");
    ZLOG("-o                   - no opengl\n");
    ZLOG("-k                   - use faster and blander cursor\n");
    ZLOG("-v                   - display version and credits\n");
    ZLOG("-a                   - run shell based tray app\n");
    ZLOG("\nZusaetzlich in dieser Portierung:\n");
    ZLOG("-S <wert>            - Deckel auf die GEZEICHNETE Groesse der Effekte,\n");
    ZLOG("                       z. B. \"-S 3.5\". Flugbahn, Flugdauer und\n");
    ZLOG("                       Einschlagzeitpunkt bleiben unveraendert.\n");
    ZLOG("-N                   - kein Skalierer: keine Groessenaenderung, kein\n");
    ZLOG("                       Truemmer-Eigendrall, keine Flughoehe (wie 1996)\n");
    ZLOG("-B sekunden          - Messlauf: nach N Sekunden beenden und Bildrate melden\n");
    ZLOG("-L ziel              - Textausgabe: stdout, none, serial oder Dateiname\n");
    ZLOG("-f                   - einfache Pufferung erzwingen (auf dem Amiga ohnehin Vorgabe)\n");
    ZLOG("-M ahi|paula         - Ausgabeweg der Musik (Amiga)\n");
    ZLOG("-A modus             - AHI-Modus als Zahl, z. B. 0x00020001\n");
    ZLOG("-V verzeichnis       - Zwischensequenzen von dort abspielen (jvplay).\n");
    ZLOG("-F on|off            - Bildrate oben links anzeigen. Vorgabe on.\n");
    ZLOG("-G story|zod         - Spielvariante. story (Vorgabe) = Kampagne des\n");
    ZLOG("                       Originals: Filme und Statistikbildschirm.\n");
    ZLOG("                       zod = wie die ZodEngine-Vorlage: weder noch,\n");
    ZLOG("                       nur der Originalladebildschirm bleibt.\n");
    ZLOG("                       Ohne -V laeuft das Spiel ganz ohne Filme.\n");

    ZLOG("\nExample usage...\n");
    ZLOG("%s -c localhost -r 800x600 -w\n", shell_command);
    ZLOG("%s -m level1.map -b 1 -p 1\n", shell_command);
    ZLOG("==================================================================\n");
}

void display_version()
{
#ifndef DISABLE_OPENGL
    ZLOG("\nZod: A Zed Engine, Version Alpha\n");
#else
    ZLOG("\nZod: A Zed Engine, Version Alpha (OpenGL Disabled)\n");
#endif
    ZLOG("By Michael Bok\n");
    ZLOG("Please visit http://zod.sourceforge.net/ and http://zzone.lewe.com/\n");
}



