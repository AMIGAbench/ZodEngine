/*
 * Pruefung der reinen LOGIK von ZodLaunch auf dem Host.
 *
 * Warum: Der Launcher ist ein MUI-Programm und meldet nichts ueber den
 * seriellen Kanal -- ein Emulatorlauf belegt also nur, dass er startet, nicht
 * WAS er tut. Die Befehlszeile und der Einstellungs-Rundlauf sind aber reine
 * Zeichenkettenarbeit und hier pruefbar.
 *
 * Geprueft wird der ECHTE Quelltext (per #include), nicht ein Nachbau --
 * ein Nachbau wuerde genau die Fehler nicht finden, um die es geht.
 * `ZOD_LAUNCH_TEST` klammert in zodlaunch.c alles aus, was MUI oder AHI
 * braucht.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ---- Amiga-Typen und die vier DOS-Aufrufe, die die Logik benutzt ------- */
typedef int            BOOL;
typedef long           LONG;
typedef unsigned long  ULONG;
typedef FILE          *BPTR;

#define TRUE  1
#define FALSE 0
#define MODE_OLDFILE 1
#define MODE_NEWFILE 2

static BPTR Open(const char *name, long modus)
{
    return fopen(name, modus == MODE_NEWFILE ? "w" : "r");
}
static void  Close(BPTR f)                    { if(f) fclose(f); }
static char *FGets(BPTR f, char *b, size_t n) { return fgets(b, (int)n, f); }
static void  FPuts(BPTR f, const char *s)     { fputs(s, f); }

#define ZOD_LAUNCH_TEST 1
#include "../../launcher/zodlaunch.c"

static int fehler = 0;

static void pruefe(const char *was, const char *ist, const char *soll)
{
    if(strcmp(ist, soll))
    {
        printf("FEHLER %s:\n  ist  '%s'\n  soll '%s'\n", was, ist, soll);
        fehler++;
    }
}

int main(void)
{
    char zeile[MAX_ZEILE];
    int  h;

    /* ---- 1. Die Skalierung landet richtig in der Befehlszeile --------- */
    {
        static const char *soll[] =
        {
            "-S 1.0 ", "-S 1.5 ", "-S 2.0 ", "-S 2.5 ", "-S 3.0 ",
            "-S 3.5 ", "-S 4.0 ", "-S 4.5 ", "-S 5.0 ", "-S 5.5 ", "-S 6.0 "
        };

        for(h = 2; h <= 12; h++)
        {
            char *p;

            cfg_vorgaben();
            cfg.skalierung = h;
            befehlszeile_bauen(zeile, sizeof(zeile));

            p = strstr(zeile, "-S ");
            if(!p) { printf("FEHLER: kein -S bei %d\n", h); fehler++; continue; }

            {
                char stueck[16];

                strncpy(stueck, p, 7);
                stueck[7] = 0;
                pruefe("Befehlszeile", stueck, soll[h-2]);
            }
        }
    }

    /* ---- 1b. Musikausgabe: Cycle, Rundlauf, Befehlszeile ------------- */
    {
        static const struct { LONG weg; BOOL musik; const char *soll; } faelle[] =
        {
            { 0, TRUE,  "-M ahi "   },
            { 1, TRUE,  "-M paula " },
            { 0, FALSE, NULL        },   /* ohne Musik kein -M */
            { 1, FALSE, NULL        }
        };

        for(h = 0; h < (int)(sizeof(faelle) / sizeof(faelle[0])); h++)
        {
            char *p;

            cfg_vorgaben();
            cfg.musik    = faelle[h].musik;
            cfg.musikweg = faelle[h].weg;
            befehlszeile_bauen(zeile, sizeof(zeile));

            p = strstr(zeile, "-M ");

            if(!faelle[h].soll)
            {
                /* Eine Einstellung, die nichts tut, gehoert nicht in die
                 * Vorschau -- sonst sucht man spaeter nach ihrer Wirkung. */
                if(p) { printf("FEHLER: -M trotz abgeschalteter Musik (Fall %d)\n", h); fehler++; }
                continue;
            }

            if(!p) { printf("FEHLER: kein -M (Fall %d)\n", h); fehler++; continue; }

            {
                char stueck[16];
                size_t n = strlen(faelle[h].soll);

                strncpy(stueck, p, n);
                stueck[n] = 0;
                pruefe("Musikausgabe", stueck, faelle[h].soll);
            }
        }

        /* Rundlauf: was gespeichert wird, muss zurueckkommen. */
        for(h = 0; h <= 1; h++)
        {
            cfg_vorgaben();
            cfg.musikweg = h;
            cfg_speichern();

            cfg_vorgaben();
            cfg_laden();

            if(cfg.musikweg != h)
            {
                printf("FEHLER: Musikweg %d kam als %ld zurueck\n", h, (long)cfg.musikweg);
                fehler++;
            }
        }

        /* Eine alte Datei OHNE den Schluessel darf nichts erfinden. */
        {
            BPTR f = Open("ZodLaunch.cfg", MODE_NEWFILE);

            if(f)
            {
                FPuts(f, "spielername=alt\n");
                Close(f);
            }

            cfg_vorgaben();
            cfg.musikweg = 1;        /* absichtlich NICHT die Vorgabe */
            cfg_laden();

            if(cfg.musikweg != 1)
            {
                printf("FEHLER: fehlender Schluessel hat den Musikweg veraendert\n");
                fehler++;
            }
        }

        /* ---- Zwischensequenzen -------------------------------------- *
         *
         * Der Schalter entscheidet allein ueber "-V cuts". Ohne ihn darf
         * KEIN -V in der Zeile stehen: die Engine spielt dann gar keine
         * Filme, und genau das ist die Zusage an den Nutzer, der sie beim
         * Installieren weggelassen hat. Ein -V mit fehlendem Verzeichnis
         * waere nicht dasselbe -- es liefe auf eine Fehlanzeige je Film
         * hinaus statt auf Stille. */
        {
            cfg_vorgaben();
            cfg.videos = TRUE;
            befehlszeile_bauen(zeile, sizeof(zeile));

            if(!strstr(zeile, "-V cuts"))
            {
                printf("FEHLER: kein -V trotz eingeschalteter Videos\n");
                fehler++;
            }

            cfg_vorgaben();
            cfg.videos = FALSE;
            befehlszeile_bauen(zeile, sizeof(zeile));

            if(strstr(zeile, "-V"))
            {
                printf("FEHLER: -V trotz abgeschalteter Videos\n");
                fehler++;
            }
        }

        /* Rundlauf, beide Stellungen. */
        for(h = 0; h <= 1; h++)
        {
            cfg_vorgaben();
            cfg.videos = h ? TRUE : FALSE;
            cfg_speichern();

            cfg_vorgaben();
            cfg_laden();

            if((cfg.videos ? 1 : 0) != h)
            {
                printf("FEHLER: Videos %d kam als %d zurueck\n", h, cfg.videos ? 1 : 0);
                fehler++;
            }
        }

        /* Alte Datei ohne den Schluessel: Vorgabe bleibt stehen. */
        {
            BPTR f = Open("ZodLaunch.cfg", MODE_NEWFILE);

            if(f) { FPuts(f, "spielername=alt\n"); Close(f); }

            cfg_vorgaben();
            cfg.videos = FALSE;      /* absichtlich NICHT die Vorgabe */
            cfg_laden();

            if(cfg.videos)
            {
                printf("FEHLER: fehlender Schluessel hat Videos eingeschaltet\n");
                fehler++;
            }
        }
    }

    /* ---- 2. Rundlauf durch die Einstellungsdatei ---------------------- */
    for(h = 2; h <= 12; h++)
    {
        cfg_vorgaben();
        cfg.skalierung = h;
        cfg_speichern();

        cfg_vorgaben();          /* absichtlich auf die Vorgabe zuruecksetzen */
        cfg_laden();

        if(cfg.skalierung != h)
        {
            printf("FEHLER Rundlauf: %d gespeichert, %ld gelesen\n",
                   h, (long)cfg.skalierung);
            fehler++;
        }
    }

    /* ---- 3. Unsinnige Werte aus einer handbearbeiteten Datei ---------- */
    {
        BPTR f = Open(CFG_DATEI, MODE_NEWFILE);

        FPuts(f, "skalierung=99\n");
        Close(f);

        cfg_vorgaben();
        cfg_laden();
        if(cfg.skalierung != 12) { printf("FEHLER: 99 nicht geklemmt (%ld)\n", (long)cfg.skalierung); fehler++; }

        f = Open(CFG_DATEI, MODE_NEWFILE);
        FPuts(f, "skalierung=-5\n");
        Close(f);

        cfg_vorgaben();
        cfg_laden();
        if(cfg.skalierung != 2) { printf("FEHLER: -5 nicht geklemmt (%ld)\n", (long)cfg.skalierung); fehler++; }
    }

    /* ---- 4. Eine Datei OHNE den Schluessel behaelt die Vorgabe -------- */
    {
        BPTR f = Open(CFG_DATEI, MODE_NEWFILE);

        FPuts(f, "spielername=alt\n");
        Close(f);

        cfg_vorgaben();
        cfg_laden();
        if(cfg.skalierung != 12)
        {
            printf("FEHLER: alte Datei ohne Schluessel gibt %ld statt 12\n",
                   (long)cfg.skalierung);
            fehler++;
        }
    }

    /* ---- 5. Aufloesung: Rundlauf ueber ALLE Eintraege ---------------- */
    {
        int i;

        for(i = 0; aufloesungen[i]; i++)
        {
            char *p;

            cfg_vorgaben();
            cfg.aufloesung = i;
            cfg_speichern();

            cfg_vorgaben();
            cfg_laden();

            if(cfg.aufloesung != i)
            {
                printf("FEHLER Aufloesung-Rundlauf: %s gespeichert, %s gelesen\n",
                       aufloesungen[i], aufloesungen[cfg.aufloesung]);
                fehler++;
            }

            /* und sie muss auch in der Befehlszeile stehen */
            befehlszeile_bauen(zeile, sizeof(zeile));
            p = strstr(zeile, "-r ");
            if(!p || strncmp(p + 3, aufloesungen[i], strlen(aufloesungen[i])))
            {
                printf("FEHLER Befehlszeile -r: '%s' fehlt\n", aufloesungen[i]);
                fehler++;
            }
        }
    }

    /* ---- 5a. Die beiden Spielvarianten ------------------------------- *
     *
     * Story Mode: feste Kampagnenliste, Filme, -G story.
     * ZodEngine Mode: eigene Liste ODER Einzelkarte, KEINE Filme, -G zod.
     *
     * Der Filmschalter darf im ZodEngine-Modus nichts bewirken -- sonst
     * stuende "-V cuts" in der Zeile und die Engine spielte den Vorspann,
     * obwohl der Modus sagt: keine Filme. */
    {
        char z[MAX_ZEILE];

        cfg_vorgaben();

        /* Story: Kampagnenliste, egal was in den Kartenfeldern steht. */
        cfg.variante = 0;
        cfg.videos   = TRUE;
        strcpy(cfg.kartenliste, "eigene.txt");
        strcpy(cfg.einzelkarte, "Data/x.map");
        cfg.nutze_einzelkarte = TRUE;
        befehlszeile_bauen(z, sizeof(z));

        if(!strstr(z, "-l " STORY_LISTE))
        { printf("FEHLER Story: '%s' ohne %s\n", z, STORY_LISTE); fehler++; }
        if(strstr(z, "eigene.txt") || strstr(z, "-m "))
        { printf("FEHLER Story: eigene Kartenwahl wirkt doch: '%s'\n", z); fehler++; }
        if(!strstr(z, "-V cuts"))
        { printf("FEHLER Story: keine Filme: '%s'\n", z); fehler++; }
        if(!strstr(z, "-G story"))
        { printf("FEHLER Story: '-G story' fehlt: '%s'\n", z); fehler++; }

        /* DIE KAMPAGNE IST ROT GEGEN BLAU. Nur die Felder zu sperren
         * genuegt nicht -- in der Einstellung kann noch etwas anderes
         * stehen, und die orig-Karten haben fuer ein drittes Team gar
         * keinen Startplatz. */
        cfg.team = 3;                       /* yellow */
        cfg.bot[2] = TRUE;                  /* green  */
        cfg.bot[1] = FALSE;                 /* blue aus */
        befehlszeile_bauen(z, sizeof(z));

        if(!strstr(z, "-t red") || !strstr(z, "-b blue"))
        { printf("FEHLER Story: nicht rot gegen blau: '%s'\n", z); fehler++; }
        if(strstr(z, "-t yellow") || strstr(z, "-b green"))
        { printf("FEHLER Story: eigene Teamwahl wirkt doch: '%s'\n", z); fehler++; }

        /* Im ZodEngine-Modus gilt wieder, was eingestellt ist. */
        cfg.variante = 1;
        befehlszeile_bauen(z, sizeof(z));

        if(!strstr(z, "-t yellow") || !strstr(z, "-b green"))
        { printf("FEHLER ZodEngine: eigene Teamwahl fehlt: '%s'\n", z); fehler++; }

        cfg.variante = 0;
        cfg.team = 0; cfg.bot[2] = FALSE; cfg.bot[1] = TRUE;

        /* ZodEngine: eigene Wahl, keine Filme -- auch mit gesetztem Haken. */
        cfg.variante = 1;
        cfg.videos   = TRUE;
        befehlszeile_bauen(z, sizeof(z));

        if(!strstr(z, "-m Data/x.map"))
        { printf("FEHLER ZodEngine: Einzelkarte fehlt: '%s'\n", z); fehler++; }
        if(strstr(z, "-V "))
        { printf("FEHLER ZodEngine: Filme trotz Modus: '%s'\n", z); fehler++; }
        if(!strstr(z, "-G zod"))
        { printf("FEHLER ZodEngine: '-G zod' fehlt: '%s'\n", z); fehler++; }

        /* ZodEngine ohne Einzelkarte -> eigene Liste. */
        cfg.nutze_einzelkarte = FALSE;
        befehlszeile_bauen(z, sizeof(z));

        if(!strstr(z, "-l eigene.txt"))
        { printf("FEHLER ZodEngine: eigene Liste fehlt: '%s'\n", z); fehler++; }

        /* Rundlauf durch die Datei. */
        cfg_speichern();
        cfg_vorgaben();
        cfg_laden();
        if(cfg.variante != 1)
        { printf("FEHLER Variante ueberlebt das Speichern nicht\n"); fehler++; }
    }

    /* ---- 5c. FPS-Anzeige ---------------------------------------------- *
     *
     * Der Schalter wird IMMER mitgegeben, auch in der Vorgabestellung --
     * sonst stuende er nicht im Spielprotokoll und waere ein unsichtbarer
     * Sonderfall. Und "on"/"off" muessen wirklich unterschieden werden:
     * beide beginnen mit 'o', eine Pruefung auf den ersten Buchstaben
     * allein liest "off" als "an". */
    {
        char z[MAX_ZEILE];

        cfg_vorgaben();

        if(!cfg.fps)
        { printf("FEHLER Vorgabe der FPS-Anzeige ist nicht an\n"); fehler++; }

        befehlszeile_bauen(z, sizeof(z));
        if(!strstr(z, "-F on"))
        { printf("FEHLER '-F on' fehlt: '%s'\n", z); fehler++; }

        cfg.fps = FALSE;
        befehlszeile_bauen(z, sizeof(z));
        if(!strstr(z, "-F off"))
        { printf("FEHLER '-F off' fehlt: '%s'\n", z); fehler++; }
        if(strstr(z, "-F on"))
        { printf("FEHLER '-F on' steht trotz aus in der Zeile: '%s'\n", z); fehler++; }

        /* Rundlauf durch die Datei. */
        cfg_speichern();
        cfg_vorgaben();
        cfg_laden();
        if(cfg.fps)
        { printf("FEHLER FPS-Einstellung ueberlebt das Speichern nicht\n"); fehler++; }
    }

    /* ---- 5b. PROGDIR: gehoert nicht in die Kartenfelder --------------- *
     *
     * Vom Nutzer gemeldet: nach dem Waehlen einer Einzelkarte stand
     * "PROGDIR:" in der Befehlszeile, und so funktioniert es nicht.
     * PROGDIR: ist PROZESSLOKAL -- im gestarteten Spiel meint es dessen
     * eigenes Programmverzeichnis, nicht das des Launchers. */
    {
        static const struct { const char *roh; const char *soll; } faelle[] =
        {
            { "PROGDIR:karte.map",            "karte.map" },
            { "progdir:Data/x.map",           "Data/x.map" },
            { "PrOgDiR:map_list.txt",         "map_list.txt" },
            { "Data/Campaing/a.map",          "Data/Campaing/a.map" },
            { "PROGDIRX:a.map",               "PROGDIRX:a.map" },
            { "ZOD:Data/a.map",               "ZOD:Data/a.map" }
        };
        size_t k;

        for(k = 0; k < sizeof(faelle)/sizeof(faelle[0]); k++)
        {
            BPTR f = Open(CFG_DATEI, MODE_NEWFILE);
            char z[128];

            snprintf(z, sizeof(z), "einzelkarte=%s\nkartenliste=%s\n",
                     faelle[k].roh, faelle[k].roh);
            FPuts(f, z);
            Close(f);

            cfg_vorgaben();
            cfg_laden();

            pruefe("Einzelkarte ohne PROGDIR:", cfg.einzelkarte, faelle[k].soll);
            pruefe("Kartenliste ohne PROGDIR:", cfg.kartenliste, faelle[k].soll);
        }
    }

    /* ---- 6. ALTE Dateien trugen den Listenplatz ----------------------- *
     *
     * Das ist der eigentliche Grund fuer aufloesung_platz(): die vier neuen
     * Modi stehen MITTEN in der Liste. Ein altes "aufloesung=1" haette
     * danach 512x384 bedeutet statt 800x600 -- eine stillschweigend
     * geaenderte Einstellung, genau die Falle vom Farbtiefen-Eintrag. */
    {
        static const struct { const char *alt; const char *soll; } faelle[] =
        {
            { "0", "640x480"  },
            { "1", "800x600"  },
            { "2", "1024x768" },
            { "7", "640x480"  },   /* ausserhalb der alten Liste -> Vorgabe */
            { "Unsinn", "640x480" },

            /* 320x240 ist am 23.09. aus der Liste ENTFERNT worden (vom
             * Nutzer als unspielbar beurteilt), 512x384 ebenso. Eine bestehende Datei
             * darf davon nicht stolpern -- sie faellt auf die Vorgabe
             * zurueck. Genau deshalb steht die Aufloesung als TEXT in der
             * Datei und nicht als Listenplatz: waere sie ein Platz, haette
             * das Entfernen alles dahinter um eins verschoben. */
            { "320x240", "640x480" },
            { "512x384", "640x480" },
            { "960x540", "640x480" }
        };
        size_t k;

        for(k = 0; k < sizeof(faelle)/sizeof(faelle[0]); k++)
        {
            BPTR f = Open(CFG_DATEI, MODE_NEWFILE);
            char z[64];

            snprintf(z, sizeof(z), "aufloesung=%s\n", faelle[k].alt);
            FPuts(f, z);
            Close(f);

            cfg_vorgaben();
            cfg_laden();

            pruefe("alte Aufloesung", aufloesungen[cfg.aufloesung], faelle[k].soll);
        }
    }

    /* ---- 7. "-N" darf NICHT mehr erzeugt werden ----------------------- */
    {
        cfg_vorgaben();
        befehlszeile_bauen(zeile, sizeof(zeile));

        if(strstr(zeile, "-N"))
        {
            printf("FEHLER: -N steht noch in der Befehlszeile: '%s'\n", zeile);
            fehler++;
        }
    }

    /* ---- 8. Auswahl des Binaers -------------------------------------- */
    {
        char befehl[512];
        char warum[128];

        /* (a) Ohne Auswahl wird NICHT gestartet, und die Meldung nennt die
         *     drei Namen. Das ist der Fall, der jeden Erstnutzer trifft. */
        cfg_vorgaben();

        if(cfg.binaer[0])
        {
            printf("FEHLER: Vorgabe fuer das Binary ist nicht leer: '%s'\n",
                   cfg.binaer);
            fehler++;
        }

        warum[0] = 0;

        if(startbefehl_bauen(befehl, sizeof(befehl), warum, sizeof(warum)) != -1)
        {
            printf("FEHLER: ohne Binary wurde eine Startzeile gebaut: '%s'\n",
                   befehl);
            fehler++;
        }
        else if(!strstr(warum, "zod_040") || !strstr(warum, "zod_080"))
        {
            printf("FEHLER: Meldung nennt die Auswahl nicht: '%s'\n", warum);
            fehler++;
        }

        /* (b) Mit Auswahl steht der Name VORNE, dann die Argumente. */
        cfg_vorgaben();
        strcpy(cfg.binaer, "zod_060");

        if(startbefehl_bauen(befehl, sizeof(befehl), warum, sizeof(warum)))
        {
            printf("FEHLER: mit Binary schlug der Bau fehl: '%s'\n", warum);
            fehler++;
        }
        else if(strncmp(befehl, "zod_060 -", 9))
        {
            printf("FEHLER: Startzeile beginnt falsch: '%s'\n", befehl);
            fehler++;
        }

        /* (c) Ein voller Pfad aus dem ASL-Requester muss unveraendert
         *     durchkommen -- inklusive Doppelpunkt und Leerzeichen im
         *     Verzeichnisnamen ist er auf AmigaOS ueblich. */
        cfg_vorgaben();
        strcpy(cfg.binaer, "ZOD:Spiele/Zod/zod_080");
        startbefehl_bauen(befehl, sizeof(befehl), warum, sizeof(warum));

        if(strncmp(befehl, "ZOD:Spiele/Zod/zod_080 -", 24))
        {
            printf("FEHLER: voller Pfad verstuemmelt: '%s'\n", befehl);
            fehler++;
        }

        /* (d) Rundlauf durch die Einstellungsdatei. */
        cfg_vorgaben();
        strcpy(cfg.binaer, "ZOD:game/zod_040");
        cfg_speichern();

        cfg_vorgaben();
        cfg_laden();

        pruefe("Binary nach Rundlauf", cfg.binaer, "ZOD:game/zod_040");

        /* (d2) Ein altes "PROGDIR:" vor dem Namen wird beim Lesen
         *      abgeschnitten -- so bereinigen sich bestehende Dateien von
         *      selbst, statt dass der Nutzer neu waehlen muss. Gross- und
         *      Kleinschreibung ist auf AmigaDOS gleichgueltig. */
        {
            static const char *alt[] = { "PROGDIR:zod_080", "progdir:zod_080",
                                         "ProgDir:zod_080" };
            size_t v;

            for(v = 0; v < sizeof(alt)/sizeof(alt[0]); v++)
            {
                BPTR f = Open(CFG_DATEI, MODE_NEWFILE);
                char z[96];

                snprintf(z, sizeof(z), "binaer=%s\n", alt[v]);
                FPuts(f, z);
                Close(f);

                cfg_vorgaben();
                cfg_laden();

                pruefe("PROGDIR: abgeschnitten", cfg.binaer, "zod_080");
            }

            /* Gegenprobe: ein Verzeichnis, das nur AEHNLICH heisst, darf
             * NICHT angeschnitten werden. */
            {
                BPTR f = Open(CFG_DATEI, MODE_NEWFILE);
                FPuts(f, "binaer=PROGDIRX:zod_060\n");
                Close(f);

                cfg_vorgaben();
                cfg_laden();

                pruefe("aehnlicher Name bleibt", cfg.binaer, "PROGDIRX:zod_060");
            }
        }

        /* (e) Eine ALTE Datei ohne den Schluessel darf nichts erfinden --
         *     sonst zeigte der Launcher ein Binary an, das niemand gewaehlt
         *     hat, und der Start schluege mit einer irrefuehrenden Meldung
         *     fehl. */
        {
            BPTR f = Open(CFG_DATEI, MODE_NEWFILE);
            FPuts(f, "spielername=alt\n");
            FPuts(f, "aufloesung=640x480\n");
            Close(f);

            cfg_vorgaben();
            cfg_laden();

            pruefe("alte Datei ohne Schluessel", cfg.binaer, "");
        }
    }

    /* ---- 9. Die Farbtiefe ist WEG ------------------------------------- *
     *
     * Sie ist seit dem 24.09. fest 8 Bit; die Auswahl 8/16/32 und die
     * Option -D sind entfallen. Zu pruefen sind ZWEI Dinge, und das zweite
     * ist das wichtigere:
     *
     *   a) in der Befehlszeile steht kein -D mehr,
     *   b) eine BESTEHENDE Datei mit "tiefe=32" stolpert nicht darueber.
     *
     * (b) ist der Fall, der in der Hand des Nutzers liegt: jede
     * ZodLaunch.cfg, die vor heute gespeichert wurde, traegt den
     * Schluessel. Er muss stillschweigend durchfallen -- so wie
     * "fenster" am 23.09. */
    {
        char z[1024];
        char warum[256];
        BPTR f = Open(CFG_DATEI, MODE_NEWFILE);

        FPuts(f, "spielername=alt\n");
        FPuts(f, "aufloesung=800x600\n");
        FPuts(f, "tiefe=32\n");        /* Schluessel aus einer alten Fassung */
        FPuts(f, "binaer=zod_080\n");
        Close(f);

        cfg_vorgaben();
        cfg_laden();

        /* Der alte Schluessel darf die uebrigen nicht verschlucken. */
        pruefe("alter Schluessel tiefe: Aufloesung bleibt",
               aufloesungen[cfg.aufloesung], "800x600");
        pruefe("alter Schluessel tiefe: Binary bleibt", cfg.binaer, "zod_080");

        startbefehl_bauen(z, sizeof(z), warum, sizeof(warum));

        if(strstr(z, "-D"))
        {
            printf("FEHLER: -D steht noch in der Befehlszeile: '%s'\n", z);
            fehler++;
        }

        /* GEGENPROBE, dass hier ueberhaupt eine Befehlszeile geprueft wird:
         * ohne sie saehe ein leeres z genauso aus wie ein bestandener Test.
         * Dieselbe Lehre wie bei den Zaehlern im Messlauf -- eine Null ist
         * erst ein Befund, wenn belegt ist, dass im Gegenfall etwas
         * dastehen wuerde. */
        if(!strstr(z, "-r 800x600"))
        {
            printf("FEHLER: Befehlszeile ohne -r, der Test prueft nichts: '%s'\n", z);
            fehler++;
        }
    }

    remove(CFG_DATEI);

    if(fehler) { printf("NICHT BESTANDEN: %d Fehler\n", fehler); return 1; }

    printf("BESTANDEN: Skalierung 1,0..6,0, Aufloesungen, alte Listenplaetze,\n"
           "           Rundlauf, Klemmung, kein -N mehr, Binary-Auswahl,\n"
           "           Musikausgabe AHI/Paula, Videoschalter,\n"
           "           kein -D mehr und alte Dateien mit 'tiefe=' unbeschadet\n");
    return 0;
}
