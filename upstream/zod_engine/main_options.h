#ifndef MAIN_OPTIONS_H
#define MAIN_OPTIONS_H


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


#include <string>
#include <cstdlib>
using namespace std;

#include <lib_qZod_DnSeparate/constants.h>

class input_options
{
public:
    input_options();

    int getoptions(int argc, char **argv);
    int checkoptions();
    void setdefaults();

    bool read_connect_address;
    bool read_map_name;
    bool read_map_list;
    bool read_is_windowed;
    bool read_resolution;
    bool read_is_dedicated;
    bool read_display_help;
    bool read_display_version;
    bool read_player_name;
    bool read_player_team;
    bool read_sound_off;
    bool read_music_off;

    /* -A <modus>: AHI-Audiomodus als Zahl, z. B. 0x00020001.
     *
     * Die Engine benutzt die Tiefschnittstelle von AHI (AHI_AllocAudio) und
     * kennt deshalb KEINE Units -- sie fragt einen MODUS an. Ohne -A nimmt sie
     * AHI_DEFAULT_ID, also den in den AHI-Voreinstellungen gespeicherten
     * Standard, und sucht sich sonst selbst einen. Der Launcher setzt diesen
     * Schalter. Auf anderen Plattformen wirkungslos. */
    bool read_ahi_mode;
    unsigned long ahi_mode;

    /* Wohin die Musik geht: 0 = AHI, 1 = Paula. Gesetzt ueber -M, im
     * Launcher als Cycle-Gadget "Musikausgabe". Nur auf dem Amiga
     * wirksam; anderswo liest es niemand. */
    bool read_musik_weg;
    int  musik_weg;
    /* Verzeichnis der Zwischensequenzen (-V). Leer = keine Filme. Der
     * externe Player jvplay bekommt Bild- und Tonweg aus denselben
     * Einstellungen wie das Spiel selbst. */
    bool read_video_dir;
    string video_dir;

    /* Spielvariante.
     *
     * true  = "Original Z Story Mode": Karten in der Reihenfolge des
     *         Originals, alle Zwischensequenzen, Statistikbildschirm.
     * false = "ZodEngine Mode": so wie die ZodEngine-Vorlage -- keine
     *         Filme, kein Statistikbildschirm. Nur der ORIGINALLADEBILD-
     *         SCHIRM bleibt, der der Vorlage sieht schlecht aus.
     *
     * WARUM ES DIESEN SCHALTER BRAUCHT: Die Filmfolge ist eine EIGENE
     * Fortschreibung (Planet 0..4, Gebiet 0..3), die bei jedem Sieg
     * weiterzaehlt und die geladene Karte gar nicht kennt -- so wie im
     * Original, wo die Reihenfolge fest ist. Bei einer eigenen
     * Kartenliste erzaehlen die Filme deshalb eine Kampagne, die mit dem
     * Gespielten nichts zu tun hat.
     *
     * Ladebild und Statistikbild waeren davon NICHT betroffen (die lesen
     * den Terraintyp aus der Karte selbst) -- die Statistik faellt hier
     * trotzdem weg, weil die Vorlage sie nicht hat.
     */
    bool story_modus;

    /* Bildrate oben links anzeigen. Vorgabe true -- bis zum 24.09. wurde
     * sie unbedingt gezeichnet, ein Aufruf ohne -F verhaelt sich also wie
     * bisher. */
    bool fps_an;
    bool read_disable_zcursor;
    bool read_settings;
    bool read_p_settings;
    bool read_opengl_off;
    bool read_start_bot[MAX_TEAM_TYPES];
    bool read_run_tray;
    bool read_loginname;
    bool read_password;
    //Messlauf: nach N Sekunden beenden und Bildrate ueber den Logkanal melden
    bool read_bench;
    //Farbtiefe des Bildschirms (-D), 0 = Vorgabe der Engine

    /* -f: Bildschirm ohne SDL_DOUBLEBUF oeffnen.
     * Auf echtem RTG ist das Umschalten ein echter Pufferwechsel (auf der V2
     * gemessen: 136 us gegen 64846 us im Emulator, wo SDL kopiert). Dann zeigt
     * jedes Bild abwechselnd einen anderen Puffer, und alles, was nicht in
     * JEDEM Bild neu gezeichnet wird, flackert. Der Schalter dient dem
     * Gegentest dieser Vermutung. */
    bool read_single_buffer;
    /* -N: kein Skalierer. Groesse bleibt 1.0, Truemmer werden nicht mehr
     * je Bild gedreht, die Flughoehe der Effekte entfaellt -- wie im
     * Original von 1996, das Einheiten in 8 festen Richtungen und
     * Truemmer als Bildfolge ablegte. Fuer schwaechere Systeme (040/060). */
    bool read_no_scaler;
    /* -S: Deckel auf die GEZEICHNETE Groesse, in Hundertstel.
     * 0 = nicht angegeben (dann entscheidet ZOD_ROTOMAX bzw. die Vorgabe). */
    int  read_scale_max;
    //Ziel der Textausgabe: stdout, none, serial oder ein Dateiname
    bool read_log_target;
    string connect_address;
    string map_name;
    string map_list;
    string resolution;
    string player_name;
    string player_team_str;
    string settings_filename;
    string p_settings_filename;
    string loginname;
    string password;
    string log_target;
    int team;
    int resolution_width;
    int resolution_height;
    int bench_seconds;

};

#endif // MAIN_OPTIONS_H
