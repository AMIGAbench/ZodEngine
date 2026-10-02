#ifndef ZOD_AMIGA_SDL_IMAGE_H
#define ZOD_AMIGA_SDL_IMAGE_H
/*
 * Ersatz fuer SDL_image auf dem Amiga: die Toolchain hat SDL 1.2, aber keine
 * Zusatzbibliotheken. PNG dekodiert stb_image, BMP uebernimmt SDL selbst.
 * Implementierung: port/amiga/sdl_addons.cpp
 */
#include <SDL/SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

SDL_Surface *IMG_Load(const char *file);
const char *IMG_GetError(void);

#ifdef __cplusplus
}
#endif

#endif
