#ifndef ZOD_AMIGA_SDL_TTF_H
#define ZOD_AMIGA_SDL_TTF_H
/*
 * Ersatz fuer SDL_ttf auf dem Amiga.
 *
 * Die Engine benutzt TrueType nur fuer Gruppenziffern an Einheiten und die
 * Nachrichtenzeile; beides laeuft inzwischen ueber die Bitmap-Schriften
 * (ZFontEngine). Die Funktionen bleiben deshalb als Attrappe bestehen und
 * liefern 0 -- Aufrufer pruefen das bereits.
 * Implementierung: port/amiga/sdl_addons.cpp
 */
#include <SDL/SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _TTF_Font TTF_Font;

int TTF_Init(void);
void TTF_Quit(void);
TTF_Font *TTF_OpenFont(const char *file, int ptsize);
void TTF_CloseFont(TTF_Font *font);
SDL_Surface *TTF_RenderText_Solid(TTF_Font *font, const char *text, SDL_Color fg);
const char *TTF_GetError(void);

#ifdef __cplusplus
}
#endif

#endif
