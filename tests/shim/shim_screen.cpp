/*
 * Schirm-Attrappe, damit die GANZE Engine auf dem Host gegen die eigene
 * Grafikschicht (port/amiga/sdl_video.cpp) laufen kann.
 *
 * Warum: Ein Fehler wie "der Farbschluessel fehlt bei jedem Blit" zeigt sich
 * nur im Zusammenspiel der Engine mit dieser Schicht -- eine kleine Sonde
 * findet ihn nicht, und ein Emulatorlauf kostet Minuten und liefert nur
 * Zahlen. Hier laeuft dasselbe in Sekunden, mit AddressSanitizer und mit
 * ZOD_SHOT als Bild.
 *
 * Es wird nichts angezeigt: SDL_Flip tut nichts, Ereignisse kommen keine.
 * Das genuegt -- gezeichnet wird in die Flaeche, und die laesst sich
 * abspeichern.
 *
 * Ersetzt port/amiga/sdl_screen.cpp (Schirm, Ereignisse), NICHT sdl_video.cpp.
 */
#include <SDL/SDL.h>

#include <stdlib.h>
#include <string.h>

#include "zod_palette.h"

extern "C" {

void zod_sdl_set_shared_palette(const SDL_Color *colors);

static SDL_Surface *the_screen = 0;

/* SDL_Init/SDL_InitSubSystem stehen in sdl_video.cpp */
void SDL_Quit(void)                 { }

SDL_Surface *SDL_SetVideoMode(int w, int h, int bpp, Uint32 flags)
{
	(void)flags;

	/* wie auf dem Amiga: die gemeinsame Palette steht im Schirm */
	if(zod_palette_ready()) zod_sdl_set_shared_palette(zod_palette_colors());

	if(the_screen) SDL_FreeSurface(the_screen);

	the_screen = SDL_CreateRGBSurface(0, w, h, bpp == 16 ? 16 : 8, 0, 0, 0, 0);

	return the_screen;
}

SDL_Surface *SDL_GetVideoSurface(void) { return the_screen; }

const SDL_VideoInfo *SDL_GetVideoInfo(void)
{
	static SDL_VideoInfo info;

	memset(&info, 0, sizeof(info));

	return &info;
}

int  SDL_Flip(SDL_Surface *screen) { (void)screen; return 0; }

void SDL_UpdateRect(SDL_Surface *screen, Sint32 x, Sint32 y, Uint32 w, Uint32 h)
{ (void)screen; (void)x; (void)y; (void)w; (void)h; }

int  SDL_PollEvent(SDL_Event *event) { (void)event; return 0; }
void SDL_PumpEvents(void)            { }
Uint8 SDL_EventState(Uint8 type, int state) { (void)type; (void)state; return 0; }

int  SDL_EnableUNICODE(int enable)   { (void)enable; return 0; }
int  SDL_EnableKeyRepeat(int d, int i) { (void)d; (void)i; return 0; }
int  SDL_ShowCursor(int toggle)      { (void)toggle; return 1; }
void SDL_WarpMouse(Uint16 x, Uint16 y) { (void)x; (void)y; }
int  SDL_WM_GrabInput(int mode)      { return mode; }
Uint8 SDL_GetMouseState(int *x, int *y) { if(x) *x = 0; if(y) *y = 0; return 0; }
void SDL_WM_SetCaption(const char *t, const char *i) { (void)t; (void)i; }
void SDL_WM_SetIcon(SDL_Surface *icon, Uint8 *mask)  { (void)icon; (void)mask; }

} /* extern "C" */
