#ifndef ZOD_SCHIRM_H
#define ZOD_SCHIRM_H
/*
 * Vollbilder des Originals (Laden, Sieg, Niederlage) anzeigen.
 *
 * Sie liegen in zod_screens.zpk, 320x200 mit je EIGENER 256er-Palette --
 * rund 250 Farben je Bild, ueber 5000 in allen zusammen. In die gemeinsame
 * Palette der Engine passt das nicht; sie bekommen deshalb die Schirmpalette
 * fuer die Dauer ihrer Anzeige, und danach wird sie zurueckgesetzt.
 *
 * WAS DAS BEDEUTET, und es ist keine Kleinigkeit: Solange ein solches Bild
 * steht, ist JEDE andere Grafik auf dem Schirm falschfarbig -- Schrift,
 * HUD, Zeiger. Deshalb zeichnet zod_schirm_zeigen() ein VOLLBILD und sonst
 * nichts, und die Prozentanzeige des Ladebilds entfaellt. Das Original hatte
 * ohnehin keine.
 *
 * Aus demselben Grund gibt es kein Ueberblenden ins Spiel: dafuer muessten
 * Ladebild und Spielgrafik gleichzeitig in EINER Palette liegen.
 */

#ifdef __cplusplus
extern "C" {
#endif

/* Liegt ein Bild unter diesem Namen im Archiv? Der Name ist der im Archiv,
 * also z. B. "assets/screens/load_desert_red". */
int zod_schirm_da(const char *name);

/* Bild ganzflaechig anzeigen: Schirm schwarz, Bild mit ganzzahligem Faktor
 * vergroessert und zentriert, eigene Palette gesetzt, ausgegeben.
 *
 * Die Palette bleibt gesetzt, bis zod_schirm_ende() gerufen wird -- ein
 * Ladebild soll ja stehenbleiben, waehrend die Engine weiterarbeitet.
 *
 * Rueckgabe: 1 = steht, 0 = nichts passiert (kein Archiv, Name unbekannt). */
int zod_schirm_zeigen(const char *name);

/* Schriftsatz waehlen. Es gibt genau drei, und alle drei sind
 * dieselbe Originalschrift (font19) in zwei Groessen:
 *
 *   "gross"  19 Punkt -- Titel, Knoepfe, "LEVEL 01", Levelname
 *   "klein"  12 Punkt -- die Statistikzeilen
 *   "laden"  12 Punkt, eng gesetzt -- die Zeile "LOADING"
 *
 * Darstellbar sind A..Z, 0..9 und . , ? ! * : (klein zusaetzlich -).
 * Kleinbuchstaben werden zu Grossbuchstaben, Unbekanntes wird zum
 * Leerzeichen. Begruendung und Messwerte: zod_schirm.cpp.
 *
 * `skala` vergroessert ganzzahlig; im Original wird sie nicht
 * gebraucht (die beiden Groessen liegen als eigene Sprites vor).
 *
 * Gilt bis zum naechsten Aufruf. */
void zod_schirm_schrift(const char *satz, int skala);

/* Hell (1) oder gedimmt (0) schreiben.
 *
 * Das Original faerbt die Schrift ueber die PALETTE des Bildes:
 * Platz 3 ist weiss, Platz 2 grau. Gedimmt ruecken die Glyphen eine
 * Stufe herunter -- genau so sehen QUIT und RETRY im Original aus.
 * Hier markiert das den Knopf unter dem Mauszeiger. */
void zod_schirm_hell(int an);

/* Text auf das zuletzt gezeigte Bild schreiben, in BILDKOORDINATEN
 * (0..319, 0..199). Skalierung und Zentrierung kommen von
 * zod_schirm_zeigen -- die Anordnung ist damit unabhaengig von der
 * Aufloesung.
 *
 * DIE FARBEN WERDEN UMGESETZT, nicht flach gezeichnet: Die Glyphen sind
 * in die GEMEINSAME Palette indiziert, auf dem Schirm steht aber die des
 * Bildes. Je Glyphenpunkt wird die Farbe aus der Schriftpalette geholt
 * und der naechste Platz in der Bildpalette gesucht. Damit bleibt die
 * Schattierung der Originalschrift erhalten -- eine flache Maske saehe
 * nach Schreibmaschine aus.
 *
 * Erst zod_schirm_ausgeben() bringt es auf den Schirm. */
void zod_schirm_text(int x, int y, const char *text);

/* Breite in Bildpunkten -- fuer zentrierten oder rechtsbuendigen Text. */
int zod_schirm_breite(const char *text);

/* Den Systemzeiger waehrend eines Vollbildes zeigen (1) oder wieder
 * verstecken (0).
 *
 * Das Spiel zeichnet sonst seinen EIGENEN Zeiger und versteckt den des
 * Systems. Auf einem Vollbild zeichnet es aber nichts -- dann ist gar
 * keiner da, und anklickbare Knoepfe sind nicht zu treffen. */
void zod_schirm_zeiger(int an);

/* Einen Ausschnitt des Bildes zurueckholen, in BILDkoordinaten.
 *
 * Gebraucht, um Geschriebenes wieder loszuwerden -- ohne das bliebe
 * jede Schrift stehen, und ein Knopf koennte seine Helligkeit nicht
 * wechseln. */
void zod_schirm_putzen(int x, int y, int b, int h);

/* Schirmkoordinaten (Mausklick) in BILDkoordinaten umrechnen.
 *
 * Ohne das muesste jeder Aufrufer Skalierung und Zentrierung selbst
 * kennen -- und die stehen hier, nicht dort. Rueckgabe 0, wenn der Punkt
 * ausserhalb des Bildes liegt (schwarzer Rand). */
int zod_schirm_maus(int mx, int my, int *bx, int *by);

/* Was seit zod_schirm_zeigen() gezeichnet wurde, ausgeben. */
void zod_schirm_ausgeben(void);

/* Gemeinsame Palette zurueck. Schadet nicht, wenn nichts gezeigt wurde. */
void zod_schirm_ende(void);

/* Namen zusammensetzen: "load"/"win"/"lose", Planet (planet_type) und Team.
 * Teams jenseits rot/blau/gruen/gelb fallen auf rot zurueck -- das Original
 * kennt nur diese vier.
 *
 * `aus` muss mindestens 64 Byte fassen. */
void zod_schirm_name(char *aus, int platz, const char *anlass,
                     int planet, int team);

#ifdef __cplusplus
}
#endif

#endif
