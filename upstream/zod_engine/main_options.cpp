#include <cstdlib>   /* strtoul fuer -A */
#include "main_options.h"

input_options::input_options()
{
    {
        read_connect_address = false;
        read_map_name = false;
        story_modus = true;          /* Vorgabe: die Kampagne des Originals */
        fps_an = true;               /* Vorgabe: Bildrate wie bisher sichtbar */
        read_map_list = false;
        read_is_windowed = false;
        read_resolution = false;
        read_is_dedicated = false;
        read_display_help = false;
        read_display_version = false;
        read_sound_off = false;
        read_music_off = false;
        read_ahi_mode = false;
        ahi_mode = 0;
        read_musik_weg = false;
        musik_weg = 0;
        read_video_dir = false;
        video_dir.clear();
        read_settings = false;
        read_p_settings = false;
        read_opengl_off = false;
        read_run_tray = false;
        read_loginname = false;
        read_password = false;
        read_bench = false;
        read_single_buffer = false;
        read_no_scaler = false;
        read_scale_max = 0;
        bench_seconds = 0;
        read_log_target = false;
        team = 0;

        for(int i=0;i<MAX_TEAM_TYPES;i++)
            read_start_bot[i] = false;
    }
}

int input_options::checkoptions()
{
    //nothing?
    if(!read_connect_address &&
        !read_map_name &&
        !read_is_windowed &&
        !read_resolution &&
        !read_is_dedicated &&
        !read_display_help &&
        !read_display_version &&
        !read_map_list)
    {
        read_display_help = true;
    }

    if(read_connect_address && read_is_dedicated)
    {
        ZLOG("cannot be a dedicated server and have a connect address\n");
        return 0;
    }

    if(read_is_dedicated && read_resolution)
    {
        ZLOG("cannot be a dedicated server and have a screen resolution\n");
        return 0;
    }

    if(read_is_dedicated && read_is_windowed)
    {
        ZLOG("cannot be a dedicated server and be in windowed mode\n");
        return 0;
    }

    if(read_connect_address && (read_map_name || read_map_list))
    {
        ZLOG("cannot have a connect address and set the map\n");
        return 0;
    }

    if(read_map_name && read_map_list)
    {
        ZLOG("cannot a read map list and a specific map file\n");
        return 0;
    }

    if(read_run_tray && !read_connect_address)
    {
        ZLOG("need a connect address to run the tray app\n");
        return 0;
    }

    return 1;
}

void input_options::setdefaults()
{
    //-c hestia.nighsoft.net -n zlover -t red -r 800x600 -w -o
    ZLOG("no arguments set, using defaults of '-c hestia.nighsoft.net -n zlover -t red -r 800x600 -w -o'\n");

    read_connect_address = true;
    connect_address = "hestia.nighsoft.net";

    read_player_name = true;
    player_name = "zlover";

    read_player_team = true;
    player_team_str = "red";
    team = RED_TEAM;

    resolution = "800x600";
    resolution_width = 800;
    resolution_height = 600;
    read_resolution = true;

    read_is_windowed = true;

    read_opengl_off = true;
}


int input_options::getoptions(int argc, char **argv)
{
    int c;
    int i;
    int temp_int;
    extern char *optarg;
    extern int optind;

    while ((c = getopt(argc, argv, "c:m:l:n:t:b:z:e:g:i:wr:dhvksuoafNB:L:D:A:S:M:V:G:F:")) != -1)
    {
        switch(c)
        {
            case 'c':
                if(!optarg) return 0;
                read_connect_address = true;
                connect_address = optarg;
                break;
            case 'm':
                if(!optarg) return 0;
                read_map_name = true;
                map_name = optarg;
                break;

            case 'l':
                if(!optarg) return 0;
                read_map_list = true;
                map_list = optarg;
                break;

            case 'n':
                if(!optarg) return 0;
                read_player_name = true;
                player_name = optarg;
                break;

            case 'z':
                if(!optarg) return 0;
                read_settings = true;
                settings_filename = optarg;
                break;

            case 'e':
                if(!optarg) return 0;
                read_p_settings = true;
                p_settings_filename = optarg;
                break;

            //Ziel der Textausgabe: -L stdout|none|serial|<datei>
            //(nicht ueber die Umgebung, weil libnix unter AmigaOS die per
            //SetEnv gesetzten ENV:-Variablen nicht ueber getenv liefert)
            case 'L':
                if(!optarg) return 0;
                read_log_target = true;
                log_target = optarg;
                break;

            /* -D WIRD ANGENOMMEN UND VERWORFEN.
             *
             * Die Farbtiefe ist seit dem 24.09. fest 8 Bit; die Option ist
             * aus der Hilfe und aus ZodLaunch verschwunden. Hier bleibt sie
             * stehen, und zwar aus einem Grund, der nichts mit der Farbtiefe
             * zu tun hat: WER NUR DAS SPIEL AUSTAUSCHT UND EINEN ALTEN
             * ZodLaunch BEHAELT, bekommt weiterhin "-D 8" uebergeben.
             *
             * Ohne diesen Zweig liefert getopt '?', und die Engine zeigt dann
             * nur die Hilfe statt zu starten -- also "das Spiel startet
             * nicht", mit einer Ursache, die niemand beim Launcher sucht.
             * Genau diese Sorte irrefuehrender Fehlstart gibt es in diesem
             * Projekt schon (falsch gewaehltes Binary), und der Nutzer kopiert
             * Dateien nachweislich auch von Hand herueber.
             *
             * Sie erscheint NICHT in der Hilfe: sie ist keine Einstellung
             * mehr, sondern Rueckwaertsvertraeglichkeit fuer eine Flagge. */
            case 'D':
                ZLOG("-D wird ignoriert: die Farbtiefe ist fest 8 Bit\n");
                break;

            //Messlauf: -B <sekunden>
            case 'B':
                if(!optarg) return 0;
                read_bench = true;
                bench_seconds = atoi(optarg);
                if(bench_seconds <= 0) bench_seconds = 60;
                break;

            case 'g':
                if(!optarg) return 0;
                read_loginname = true;
                loginname = optarg;
                break;

            case 'i':
                if(!optarg) return 0;
                read_password = true;
                password = optarg;
                break;

            case 't':
                if(!optarg) return 0;
                read_player_team = true;
                player_team_str = optarg;

                for(i=0;i<MAX_TEAM_TYPES;i++)
                    if(team_type_string[i] == player_team_str)
                        break;

                if(i!=MAX_TEAM_TYPES)
                    team = i;
                else
                    ZLOG("could not find the team '%s', perhaps try lowercase?\n", player_team_str.c_str());
                break;

            case 'b':
                if(!optarg) return 0;
                //read_bot_count = true;
                //bot_count = atoi(optarg);
                if(!optarg) return 0;

                for(i=0;i<MAX_TEAM_TYPES;i++)
                    if(team_type_string[i] == optarg)
                        break;

                if(i!=MAX_TEAM_TYPES)
                    read_start_bot[i] = true;
                else
                    ZLOG("could not find the team '%s', perhaps try lowercase?\n", optarg);
                break;

            case 'r':
                if(!optarg) return 0;
                resolution = optarg;

                temp_int = resolution.find('x');

                if(temp_int != string::npos)
                {
                    resolution_width = atoi(resolution.substr(0, temp_int).c_str());
                    resolution_height = atoi(resolution.substr(temp_int+1, 10).c_str());
                    read_resolution = true;
                }
                else
                    read_resolution = false;

                break;

            case 'w':
                read_is_windowed = true;
                break;

            case 'd':
                read_is_dedicated = true;
                break;

            case 'h':
                read_display_help = true;
                break;

            case 'v':
                read_display_version = true;
                break;

            case 's':
                read_sound_off = true;
                break;

            case 'u':
                read_music_off = true;
                break;

            case 'A':
                if(!optarg) return 0;
                /* Hexadezimal mit oder ohne 0x, oder dezimal */
                read_ahi_mode = true;
                ahi_mode = strtoul(optarg, 0, 0);
                break;

            case 'M':
                /* Ausgabeweg der Musik. Bewusst als WORT und nicht als
                 * Zahl: "-M paula" ist im Protokoll und in der Vorschau
                 * des Launchers lesbar, "-M 1" waere es nicht. */
                if(!optarg) return 0;
                read_musik_weg = true;
                musik_weg = (optarg[0] == 'p' || optarg[0] == 'P') ? 1 : 0;
                break;

            case 'F':
                /* Bildrate anzeigen: -F on (Vorgabe) oder -F off.
                 *
                 * Als WORT wie -M und -G -- "-F off" ist im Protokoll
                 * lesbar, "-F 0" waere es nicht.
                 *
                 * DER ERSTE BUCHSTABE GENUEGT HIER NICHT: "on" und "off"
                 * beginnen beide mit 'o'. Eine erste Fassung hat genau das
                 * uebersehen und "-F off" als "an" gelesen -- aufgefallen
                 * ist es nur, weil die Gegenprobe alle Schreibweisen
                 * durchgegangen ist. */
                if(!optarg) return 0;
                {
                    const char a = (optarg[0] >= 'A' && optarg[0] <= 'Z')
                                 ? (char)(optarg[0] + 32) : optarg[0];
                    const char b = (optarg[1] >= 'A' && optarg[1] <= 'Z')
                                 ? (char)(optarg[1] + 32) : optarg[1];

                    fps_an = !(a == '0' || a == 'n' || (a == 'o' && b == 'f'));
                }
                break;

            case 'G':
                /* Spielvariante: -G story (Vorgabe) oder -G zod.
                 *
                 * Als WORT, nicht als Zahl -- dieselbe Ueberlegung wie bei
                 * -M paula: "-G zod" ist im Protokoll und in der Vorschau
                 * des Launchers lesbar, "-G 1" waere es nicht. Geprueft
                 * wird der erste Buchstabe: 's' ist die Kampagne, alles
                 * andere der ZodEngine-Modus. */
                if(!optarg) return 0;
                story_modus = (optarg[0] == 's' || optarg[0] == 'S');
                break;

            case 'V':
                /* Verzeichnis der Zwischensequenzen. OHNE -V spielt die
                 * Engine keine Filme und verhaelt sich genau wie bisher --
                 * das ist die Zusage an den Nutzer, der sie beim
                 * Installieren nicht mitkopiert hat. */
                if(!optarg) return 0;
                read_video_dir = true;
                video_dir = optarg;
                break;

            case 'k':
                read_disable_zcursor = true;
                break;

            case 'o':
                read_opengl_off = true;
                break;

            case 'f':
                read_single_buffer = true;
                break;

            //Kein Skalierer: -N
            case 'N':
                read_no_scaler = true;
                break;

            /* -S <wert>: Deckel auf die gezeichnete Groesse der Effekte,
             * z. B. "-S 3.5". Betrifft NUR die Grafik -- Flugbahn, Flugdauer
             * und Einschlagzeitpunkt bleiben unberuehrt (siehe ZOD_ROTOMAX
             * in zsdl_opengl.cpp).
             *
             * BEWUSST OHNE atof: Auf dem Amiga haengt das Dezimalzeichen am
             * System-Locale -- `atof("3.5")` liefert im deutschen Locale 0.
             * Das war die Ursache fuer "Fahrzeuge fahren nicht".
             * Deshalb ganzzahlig geparst, und '.' wie ',' werden akzeptiert. */
            case 'S':
            {
                const char *t = optarg;
                int ganz = 0, bruch = 0, stellen = 0;

                while(*t >= '0' && *t <= '9') { ganz = ganz*10 + (*t - '0'); t++; }

                if(*t == '.' || *t == ',')
                {
                    t++;
                    while(*t >= '0' && *t <= '9' && stellen < 2)
                    {
                        bruch = bruch*10 + (*t - '0');
                        t++;
                        stellen++;
                    }
                }

                while(stellen < 2) { bruch *= 10; stellen++; }

                read_scale_max = ganz * 100 + bruch;
                break;
            }

            case 'a':
                read_run_tray = true;
                break;

            case '?':
                ZLOG("unrecognized option -%c\n", c);
                read_display_help = true;
                return 0;
        }

    }

    return 1;
}
