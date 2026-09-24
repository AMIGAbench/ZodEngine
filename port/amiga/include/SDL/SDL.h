#ifndef ZOD_AMIGA_SDL_H
#define ZOD_AMIGA_SDL_H
/*
 * Eigener Ersatz fuer SDL 1.2 auf AmigaOS.
 *
 * Warum: SDL bringt auf dem Amiga nur Nachteile mit. Es blaest jede Flaeche auf
 * 32 Bit auf, haengt an jede eine eigene Palette (1 KB) und eine Umsetztabelle
 * (512 Byte), und sein Blitpfad geht ueber Formatpruefungen, die wir nicht
 * brauchen. Gemessen: 6920 Bilder kosten damit 35 MB statt der 7,2 MB, die die
 * Bilddaten gross sind.
 *
 * Dieser Ersatz bildet nur nach, was die Engine tatsaechlich benutzt (erhoben
 * am 18.09.: 171 Aufrufstellen in 15 Dateien). Die Namen und Strukturen
 * entsprechen SDL 1.2, damit der Engine-Code unveraendert bleibt -- der
 * Unterbau ist eigener Code:
 *
 *   Flaechen  : 8 Bit chunky mit GEMEINSAMER Palette, Schluessel ist Index 0
 *   Blitter   : deckend / mit Schluessel / mit Schluessel und Alpha, mit
 *               Beschneidung an beiden Rechtecken (darauf verlaesst sich die
 *               Engine bei HUD und Menues)
 *   Schirm    : eigener RTG-Schirm ueber cybergraphics.library (P96/CGX)
 *   Eingabe   : IDCMP, RAWKEY-Nummern auf die SDL-Keysyms abgebildet
 *
 * Nicht nachgebildet: OpenGL (global abgeschaltet), Zeitfunktionen (die Engine
 * nutzt eigene), Faeden (seit dem Ein-Task-Umbau nicht mehr benutzt).
 *
 * Umsetzung: port/amiga/sdl_video.cpp, sdl_blit.cpp, sdl_screen.cpp,
 *            sdl_input.cpp
 */
/* Echtes SDL zieht diese drei ueber seine eigenen Koepfe mit herein, und der
 * Engine-Code verlaesst sich darauf (memcpy, uint8_t, ...). Ohne sie
 * uebersetzt der Bestand nicht. */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned char  Uint8;
typedef signed char    Sint8;
typedef unsigned short Uint16;
typedef signed short   Sint16;
typedef unsigned int   Uint32;
typedef signed int     Sint32;

/* ---------------------------------------------------------------- Flaechen */

#define SDL_SWSURFACE    0x00000000
#define SDL_HWSURFACE    0x00000001
#define SDL_ASYNCBLIT    0x00000004
#define SDL_ANYFORMAT    0x10000000
#define SDL_HWPALETTE    0x20000000
#define SDL_DOUBLEBUF    0x40000000
#define SDL_FULLSCREEN   0x80000000
#define SDL_OPENGL       0x00000002
#define SDL_RESIZABLE    0x00000010
#define SDL_NOFRAME      0x00000020
#define SDL_HWACCEL      0x00000100
#define SDL_SRCCOLORKEY  0x00001000
#define SDL_RLEACCEL     0x00004000
#define SDL_SRCALPHA     0x00010000
#define SDL_PREALLOC     0x01000000

#define SDL_LOGPAL       0x01
#define SDL_PHYSPAL      0x02

typedef struct SDL_Rect
{
	Sint16 x, y;
	Uint16 w, h;
} SDL_Rect;

typedef struct SDL_Color
{
	Uint8 r, g, b, unused;
} SDL_Color;

typedef struct SDL_Palette
{
	int ncolors;
	SDL_Color *colors;
} SDL_Palette;

typedef struct SDL_PixelFormat
{
	SDL_Palette *palette;
	Uint8 BitsPerPixel;
	Uint8 BytesPerPixel;
	Uint8 Rloss, Gloss, Bloss, Aloss;
	Uint8 Rshift, Gshift, Bshift, Ashift;
	Uint32 Rmask, Gmask, Bmask, Amask;
	Uint32 colorkey;
	Uint8 alpha;
} SDL_PixelFormat;

typedef struct SDL_Surface
{
	Uint32 flags;
	SDL_PixelFormat *format;
	int w, h;
	Uint16 pitch;
	void *pixels;
	int offset;
	SDL_Rect clip_rect;
	int refcount;
} SDL_Surface;

/* Unsere Flaechen liegen immer im Hauptspeicher und brauchen kein Sperren. */
#define SDL_MUSTLOCK(surface) (0)

typedef struct SDL_VideoInfo
{
	Uint32 hw_available;
	Uint32 wm_available;
	Uint32 blit_hw;
	Uint32 blit_hw_CC;
	Uint32 blit_hw_A;
	Uint32 blit_sw;
	Uint32 blit_sw_CC;
	Uint32 blit_sw_A;
	Uint32 blit_fill;
	Uint32 video_mem;
	SDL_PixelFormat *vfmt;
	int current_w, current_h;
} SDL_VideoInfo;

/* ------------------------------------------------------------- Ereignisse */

#define SDL_NOEVENT          0
#define SDL_ACTIVEEVENT      1
#define SDL_KEYDOWN          2
#define SDL_KEYUP            3
#define SDL_MOUSEMOTION      4
#define SDL_MOUSEBUTTONDOWN  5
#define SDL_MOUSEBUTTONUP    6
#define SDL_QUIT             12
#define SDL_VIDEORESIZE      16

#define SDL_PRESSED   1
#define SDL_RELEASED  0

#define SDL_IGNORE    0
#define SDL_ENABLE    1
#define SDL_DISABLE   0
#define SDL_QUERY    -1

#define SDL_BUTTON_LEFT      1
#define SDL_BUTTON_MIDDLE    2
#define SDL_BUTTON_RIGHT     3
#define SDL_BUTTON_WHEELUP   4
#define SDL_BUTTON_WHEELDOWN 5

#define SDL_GRAB_OFF    0
#define SDL_GRAB_ON     1
#define SDL_GRAB_QUERY -1

/* Tastennummern wie in SDL 1.2 -- die Engine prueft sie teilweise als nackte
 * Zahlen (zplayer_events.cpp), sie duerfen sich also nicht verschieben. */
enum
{
	SDLK_UNKNOWN = 0,
	SDLK_BACKSPACE = 8,
	SDLK_TAB = 9,
	SDLK_RETURN = 13,
	SDLK_ESCAPE = 27,
	SDLK_SPACE = 32,
	SDLK_0 = 48, SDLK_1, SDLK_2, SDLK_3, SDLK_4,
	SDLK_5, SDLK_6, SDLK_7, SDLK_8, SDLK_9,
	SDLK_DELETE = 127,
	SDLK_KP0 = 256, SDLK_KP1, SDLK_KP2, SDLK_KP3, SDLK_KP4,
	SDLK_KP5, SDLK_KP6, SDLK_KP7, SDLK_KP8, SDLK_KP9,
	SDLK_KP_PERIOD = 266, SDLK_KP_DIVIDE, SDLK_KP_MULTIPLY,
	SDLK_KP_MINUS, SDLK_KP_PLUS, SDLK_KP_ENTER,
	SDLK_UP = 273, SDLK_DOWN, SDLK_RIGHT, SDLK_LEFT,
	SDLK_INSERT = 277, SDLK_HOME, SDLK_END, SDLK_PAGEUP, SDLK_PAGEDOWN,
	SDLK_F1 = 282, SDLK_F2, SDLK_F3, SDLK_F4, SDLK_F5,
	SDLK_F6, SDLK_F7, SDLK_F8, SDLK_F9, SDLK_F10, SDLK_F11, SDLK_F12,
	SDLK_NUMLOCK = 300, SDLK_CAPSLOCK, SDLK_SCROLLOCK,
	SDLK_RSHIFT = 303, SDLK_LSHIFT, SDLK_RCTRL, SDLK_LCTRL,
	SDLK_RALT = 307, SDLK_LALT
};

#define KMOD_NONE   0x0000
#define KMOD_LSHIFT 0x0001
#define KMOD_RSHIFT 0x0002
#define KMOD_LCTRL  0x0040
#define KMOD_RCTRL  0x0080
#define KMOD_LALT   0x0100
#define KMOD_RALT   0x0200
#define KMOD_SHIFT  (KMOD_LSHIFT | KMOD_RSHIFT)
#define KMOD_CTRL   (KMOD_LCTRL | KMOD_RCTRL)
#define KMOD_ALT    (KMOD_LALT | KMOD_RALT)

typedef struct SDL_keysym
{
	Uint8 scancode;
	Uint32 sym;
	Uint32 mod;
	Uint16 unicode;
} SDL_keysym;

typedef struct SDL_KeyboardEvent
{
	Uint8 type;
	Uint8 which;
	Uint8 state;
	SDL_keysym keysym;
} SDL_KeyboardEvent;

typedef struct SDL_MouseMotionEvent
{
	Uint8 type;
	Uint8 which;
	Uint8 state;
	Uint16 x, y;
	Sint16 xrel, yrel;
} SDL_MouseMotionEvent;

typedef struct SDL_MouseButtonEvent
{
	Uint8 type;
	Uint8 which;
	Uint8 button;
	Uint8 state;
	Uint16 x, y;
} SDL_MouseButtonEvent;

typedef struct SDL_ResizeEvent
{
	Uint8 type;
	int w, h;
} SDL_ResizeEvent;

typedef struct SDL_QuitEvent
{
	Uint8 type;
} SDL_QuitEvent;

typedef struct SDL_ActiveEvent
{
	Uint8 type;
	Uint8 gain;
	Uint8 state;
} SDL_ActiveEvent;

typedef union SDL_Event
{
	Uint8 type;
	SDL_ActiveEvent active;
	SDL_KeyboardEvent key;
	SDL_MouseMotionEvent motion;
	SDL_MouseButtonEvent button;
	SDL_ResizeEvent resize;
	SDL_QuitEvent quit;
} SDL_Event;

/* Klangformate (fuer Mix_OpenAudio; die Ausgabe laeuft ueber AHI) */
#define AUDIO_U8       0x0008
#define AUDIO_S8       0x8008
#define AUDIO_U16LSB   0x0010
#define AUDIO_S16LSB   0x8010
#define AUDIO_U16MSB   0x1010
#define AUDIO_S16MSB   0x9010
#define AUDIO_U16      AUDIO_U16LSB
#define AUDIO_S16      AUDIO_S16LSB

#define SDL_DEFAULT_REPEAT_DELAY    500
#define SDL_DEFAULT_REPEAT_INTERVAL 30

/* ------------------------------------------------------------- Funktionen */

#define SDL_INIT_VIDEO     0x00000020
#define SDL_INIT_AUDIO     0x00000010
#define SDL_INIT_TIMER     0x00000001
#define SDL_INIT_JOYSTICK  0x00000200
#define SDL_INIT_EVERYTHING 0x0000FFFF

int  SDL_Init(Uint32 flags);
int  SDL_InitSubSystem(Uint32 flags);
void SDL_Quit(void);
const char *SDL_GetError(void);

SDL_Surface *SDL_SetVideoMode(int w, int h, int bpp, Uint32 flags);
SDL_Surface *SDL_GetVideoSurface(void);
const SDL_VideoInfo *SDL_GetVideoInfo(void);
int  SDL_Flip(SDL_Surface *screen);
void SDL_UpdateRect(SDL_Surface *screen, Sint32 x, Sint32 y, Uint32 w, Uint32 h);
void SDL_WM_SetCaption(const char *title, const char *icon);
void SDL_WM_SetIcon(SDL_Surface *icon, Uint8 *mask);

SDL_Surface *SDL_CreateRGBSurface(Uint32 flags, int w, int h, int depth,
                                  Uint32 rmask, Uint32 gmask, Uint32 bmask, Uint32 amask);
SDL_Surface *SDL_CreateRGBSurfaceFrom(void *pixels, int w, int h, int depth, int pitch,
                                      Uint32 rmask, Uint32 gmask, Uint32 bmask, Uint32 amask);
void SDL_FreeSurface(SDL_Surface *surface);
int  SDL_LockSurface(SDL_Surface *surface);
void SDL_UnlockSurface(SDL_Surface *surface);

int  SDL_SetColorKey(SDL_Surface *surface, Uint32 flag, Uint32 key);
int  SDL_SetAlpha(SDL_Surface *surface, Uint32 flag, Uint8 alpha);
int  SDL_SetColors(SDL_Surface *surface, SDL_Color *colors, int firstcolor, int ncolors);
int  SDL_SetPalette(SDL_Surface *surface, int flags, SDL_Color *colors,
                    int firstcolor, int ncolors);
int  SDL_SetClipRect(SDL_Surface *surface, const SDL_Rect *rect);

SDL_Surface *SDL_ConvertSurface(SDL_Surface *src, SDL_PixelFormat *fmt, Uint32 flags);
SDL_Surface *SDL_DisplayFormat(SDL_Surface *surface);
SDL_Surface *SDL_DisplayFormatAlpha(SDL_Surface *surface);

int  SDL_UpperBlit(SDL_Surface *src, SDL_Rect *srcrect,
                   SDL_Surface *dst, SDL_Rect *dstrect);
#define SDL_BlitSurface SDL_UpperBlit
int  SDL_FillRect(SDL_Surface *dst, SDL_Rect *dstrect, Uint32 color);

Uint32 SDL_MapRGB(const SDL_PixelFormat *fmt, Uint8 r, Uint8 g, Uint8 b);
Uint32 SDL_MapRGBA(const SDL_PixelFormat *fmt, Uint8 r, Uint8 g, Uint8 b, Uint8 a);
void   SDL_GetRGB(Uint32 pixel, const SDL_PixelFormat *fmt, Uint8 *r, Uint8 *g, Uint8 *b);
void   SDL_GetRGBA(Uint32 pixel, const SDL_PixelFormat *fmt,
                   Uint8 *r, Uint8 *g, Uint8 *b, Uint8 *a);

SDL_Surface *SDL_LoadBMP(const char *file);
int          SDL_SaveBMP(SDL_Surface *surface, const char *file);

int  SDL_PollEvent(SDL_Event *event);
void SDL_PumpEvents(void);
Uint8 SDL_EventState(Uint8 type, int state);
int  SDL_EnableUNICODE(int enable);
int  SDL_EnableKeyRepeat(int delay, int interval);
int  SDL_ShowCursor(int toggle);
void SDL_WarpMouse(Uint16 x, Uint16 y);
int  SDL_WM_GrabInput(int mode);
Uint8 SDL_GetMouseState(int *x, int *y);

void SDL_GL_SwapBuffers(void);

Uint32 SDL_GetTicks(void);
void   SDL_Delay(Uint32 ms);

#ifdef __cplusplus
}
#endif

#endif
