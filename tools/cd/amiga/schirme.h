#ifndef ZX_SCHIRME_H
#define ZX_SCHIRME_H
/*
 * Die Vollbilder aus z/main.pac: Ladebild, Siegbild, Niederlagenbild --
 * je Planet und Teamfarbe.
 *
 * WARUM EIN EIGENER ARCHIVSCHREIBER und nicht archiv_schreiben() aus
 * zextract.c: Der dort kennt genau zwei Quellen (Saetze aus SPRITES.RSC und
 * Klaenge aus z.pac) und schreibt jeden Eintrag OHNE eigene Palette --
 * `pal_colors` steht fest auf 0, weil alle Spielbilder die gemeinsame
 * Palette benutzen. Diese Vollbilder koennen das nicht: jedes hat rund 250
 * EIGENE Farben, alle 60 zusammen ueber 5000. Sie brauchen also Format 1
 * mit Palette je Eintrag.
 *
 * Den bewaehrten Schreiber dafuer umzubauen hiesse, eine byteweise
 * abgenommene Stelle anzufassen, um einen Sonderfall unterzubringen, der
 * mit ihr nichts gemein hat. Hier ist es ein Dutzend Zeilen, weil alle
 * Eintraege gleich gross sind.
 */

#include "zextract.h"

/* Aus <cd>/z/main.pac die 60 Vollbilder holen und als
 * <ziel>/zod_screens.zpk ablegen -- dazu den Originalzeichensatz.
 *
 * Rueckgabe: Zahl der geschriebenen Bilder, 0 wenn main.pac fehlt
 * (das ist KEIN Fehler -- das Spiel laeuft dann mit dem bisherigen
 * Ladebild weiter), -1 bei einem echten Fehler. */
/* `bytes` bekommt die Zahl der geschriebenen Byte, wenn es nicht 0 ist.
 * Das ist kein Beiwerk: ZExtracts Schlusszeile summiert sonst nur, was
 * ueber den ALTEN Archivschreiber lief, und meldet damit rund die Haelfte
 * des tatsaechlich Geschriebenen -- ein Zaehler, der luegt, fuehrt spaeter
 * jemanden in die Irre. */
LONG schirme_extrahieren(const char *cd, const char *ziel, ULONG *bytes);

/* Die Levelbezeichnungen aus <cd>/z/levels.dat nach <ziel>/levelnames.dat.
 *
 * WARUM SIE GEBRAUCHT WERDEN: Im Original steht auf dem Ladebildschirm
 * "Level 01" und darunter der Name des Levels -- "Virgin Soldiers",
 * "Death Valley", "Molten Kombat". Der hat mit dem Dateinamen der Karte
 * nichts zu tun, und der Name IN der Kartendatei ist unbrauchbar (dort
 * steht durchweg "clone_map").
 *
 * levels.dat ist 240 Byte je Eintrag, der Name steht bei Versatz 0 und
 * ist hoechstens 20 Byte lang; Eintrag k ist Level k (Eintrag 0 ist
 * "Test" und gehoert nicht dazu).
 *
 * Geschrieben wird ein FESTES Satzformat: 40 Saetze zu 20 Byte, Level N
 * bei (N-1)*20, mit Nullbytes aufgefuellt. Kein Parser noetig -- die
 * Engine liest den Satz und hat den Namen.
 *
 * Rueckgabe: Zahl der Level mit Namen, 0 wenn levels.dat fehlt (KEIN
 * Fehler -- dann steht auf dem Ladebild nur "Level NN"), -1 bei Fehler. */
LONG levelnamen_extrahieren(const char *cd, const char *ziel);

#endif
