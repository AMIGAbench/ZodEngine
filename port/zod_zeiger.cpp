/*
 * Attrappe fuer alles ausser AmigaOS. Die echte Fassung steht in
 * port/amiga/sdl_screen.cpp -- sie braucht den gesperrten Grafikspeicher und
 * gehoert deshalb neben SDL_Flip.
 *
 * Auf dem Host zeichnet echtes SDL, und dort gibt es die Verzoegerung nicht,
 * um die es hier geht: SDL_Flip kopiert sofort. `zod_zeiger_spaet()` liefert
 * deshalb 0, und die Engine blittet den Zeiger wie bisher selbst.
 */
#include "zod_zeiger.h"

#ifndef __amigaos__

extern "C" int  zod_zeiger_spaet(void) { return 0; }
extern "C" void zod_zeiger_setzen(struct SDL_Surface *, int, int) { }
extern "C" void zod_zeiger_keiner(void) { }

#endif
