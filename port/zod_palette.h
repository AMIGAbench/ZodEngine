#ifndef ZOD_PALETTE_H
#define ZOD_PALETTE_H
/*
 * Die gemeinsame 256-Farben-Palette (8-Bit-Pfad).
 *
 * Erzeugt von tools/assets/palette.py aus der ORIGINALPALETTE des Spiels:
 * Grundpalette, je Planet eine Bank fuer die freien Plaetze, je Team eine
 * Umsetztabelle Index -> Index. Damit braucht kein Bild mehr eine eigene
 * Palette, und Teamfarben entstehen beim Zeichnen ueber die Tabelle statt
 * durch acht eingefaerbte Kopien jedes Bildes.
 *
 * Platz 0 ist durchgehend der Farbschluessel (durchsichtig).
 */
#include <SDL/SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

/* palette.zpl aus <dir> laden (wie die Archive). 1 = geladen. */
int zod_palette_load(const char *dir);

/* 1, wenn eine Palette vorliegt -- sonst arbeitet alles wie bisher. */
int zod_palette_ready(void);

/* Die aktuellen 256 Eintraege (Grundpalette plus Bank des Planeten). */
const SDL_Color *zod_palette_colors(void);

/* Bank eines Planeten einschreiben (Reihenfolge wie planet_type).
 * Gibt 1 zurueck, wenn sich etwas geaendert hat. */
int zod_palette_set_planet(int planet);

/* Palette fuer ein Bild: enthaelt der Name einen Planeten, kommt dessen Bank
 * mit zurueck, sonst die Grundpalette. Solange der Schirm nicht selbst 8 Bit
 * hat, traegt jede Flaeche ihre Palette mit sich -- ein Wechsel zur Laufzeit
 * ist dann gar nicht noetig. */
const SDL_Color *zod_palette_colors_for(const char *name);

/* Umsetztabelle eines Teams: 256 Byte, Index -> Index. Team 0 (rot) ist die
 * Gleichheit. Ausserhalb des Bereichs kommt die Gleichheit zurueck. */
const unsigned char *zod_palette_xlat(int team);

int zod_palette_teams(void);

#ifdef __cplusplus
}
#endif

#endif
