/*
 * Kleinteile des SDL-Ersatzes: Sperren, Faeden, Zeit, BMP.
 *
 * Sperren und Faeden sind Huellen: Der Port laeuft seit P2 in einem Task
 * (Pfadsuche als Auftragsschlange im Servertakt, Laden synchron). Erhoben am
 * 18.09.: kein lebender SDL_CreateThread-Aufruf mehr.
 */
#include <SDL/SDL.h>
#include <SDL/SDL_thread.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __amigaos__
#include <proto/exec.h>
#include <proto/dos.h>
#else
/* Derselbe Quelltext laeuft im Pruefstand (PLATFORM=shim) auf dem Host. */
#include <time.h>
#include <unistd.h>
#endif

#include "zod_log.h"

/* stb_image wird in sdl_addons.cpp eingebunden (dort steht die Umsetzung) */
#include "stb_image.h"

extern "C" {

/* ------------------------------------------------------- Sperren, Faeden */

struct SDL_mutex { int dummy; };
struct SDL_Thread { int dummy; };

static SDL_mutex the_mutex = { 0 };

SDL_mutex *SDL_CreateMutex(void)          { return &the_mutex; }
void       SDL_DestroyMutex(SDL_mutex *m) { (void)m; }
int        SDL_mutexP(SDL_mutex *m)       { (void)m; return 0; }
int        SDL_mutexV(SDL_mutex *m)       { (void)m; return 0; }

SDL_Thread *SDL_CreateThread(int (*fn)(void *), void *data)
{
	/* Im Ein-Task-Modell gibt es keine Faeden. Der Aufrufer muss mit 0
	 * umgehen koennen -- die Engine tut es (kein lebender Aufruf mehr). */
	(void)fn; (void)data;
	ZLOG("SDL_CreateThread aufgerufen -- im Ein-Task-Modell nicht vorgesehen\n");
	return 0;
}

void   SDL_WaitThread(SDL_Thread *t, int *status) { (void)t; if(status) *status = 0; }
void   SDL_KillThread(SDL_Thread *t)              { (void)t; }
Uint32 SDL_ThreadID(void)                         { return 1; }

/* ----------------------------------------------------------------- Zeit */

Uint32 SDL_GetTicks(void)
{
#ifndef __amigaos__
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);

	return (Uint32)(ts.tv_sec * 1000UL + ts.tv_nsec / 1000000UL);
#else
	/* Die Engine benutzt eigene Zeitfunktionen (COMMON::current_time); das
	 * hier ist nur fuer Fremdcode da. */
	struct DateStamp ds;

	DateStamp(&ds);

	return (Uint32)(((unsigned long)ds.ds_Minute * 60000UL) +
	                ((unsigned long)ds.ds_Tick * 1000UL / 50UL));
#endif
}

void SDL_Delay(Uint32 ms)
{
	if(!ms) return;

#ifdef __amigaos__
	/* AmigaOS schlaeft in Rastern von 20 ms -- alles
	 * darunter ist ohnehin ein Aufrunden. */
	Delay((ULONG)((ms * 50UL + 999UL) / 1000UL));
#else
	usleep(ms * 1000UL);
#endif
}

/* ------------------------------------------------------------- OpenGL */

/* OpenGL ist global abgeschaltet (-DDISABLE_OPENGL), aber zplayer.cpp ruft
 * SDL_GL_SwapBuffers in einem Zweig, der nie genommen wird. */
void SDL_GL_SwapBuffers(void) { }

/* ------------------------------------------------------------------ BMP */

SDL_Surface *SDL_LoadBMP(const char *file)
{
	int w = 0, h = 0, comp = 0;
	unsigned char *pixels = stbi_load(file, &w, &h, &comp, 4);

	if(!pixels) return 0;

	SDL_Surface *s = SDL_CreateRGBSurface(0, w, h, 8, 0, 0, 0, 0);

	if(!s)
	{
		stbi_image_free(pixels);
		return 0;
	}

	/* Auf die gemeinsame Palette abbilden; Platz 0 bleibt durchsichtig. */
	unsigned char *dst = (unsigned char*)s->pixels;

	for(int y = 0; y < h; y++)
		for(int x = 0; x < w; x++)
		{
			const unsigned char *p = pixels + ((size_t)y * w + x) * 4;

			dst[(size_t)y * s->pitch + x] =
				p[3] < 128 ? 0 : (unsigned char)SDL_MapRGB(s->format, p[0], p[1], p[2]);
		}

	stbi_image_free(pixels);

	SDL_SetColorKey(s, SDL_SRCCOLORKEY, 0);

	return s;
}

static void put_le32(unsigned char *p, unsigned long v)
{
	p[0] = (unsigned char)(v);        p[1] = (unsigned char)(v >> 8);
	p[2] = (unsigned char)(v >> 16);  p[3] = (unsigned char)(v >> 24);
}

int SDL_SaveBMP(SDL_Surface *surface, const char *file)
{
	/* Ein Bild vom Schirm als BMP.
	 *
	 * Klingt nach Luxus, ist aber das fehlende Messmittel: Jeder
	 * automatische Lauf dieses Projekts ist bildschirmlos und belegt
	 * Bildraten und Ladezeilen -- nie, wie es AUSSIEHT. Fehler wie
	 * "Hintergrund der Objekte ist schwarz" lassen sich sonst nur ueber den
	 * Nutzer an der echten Maschine erfahren.
	 *
	 * Geschrieben wird immer 8 Bit mit Palette (alles andere hat der
	 * 8-Bit-Pfad nicht). BMP legt die Zeilen von unten nach oben ab. */
	if(!surface || !surface->pixels || !file) return -1;

	if(surface->format->BytesPerPixel != 1) return -1;

	const int w = surface->w, h = surface->h;
	const int row = (w + 3) & ~3;           /* BMP-Zeilen auf 4 Byte */
	const unsigned long pix = (unsigned long)row * h;
	const unsigned long off = 14 + 40 + 256 * 4;

	FILE *fp = fopen(file, "wb");

	if(!fp) return -1;

	unsigned char head[14 + 40];

	memset(head, 0, sizeof(head));
	head[0] = 'B'; head[1] = 'M';
	put_le32(head + 2, off + pix);
	put_le32(head + 10, off);
	put_le32(head + 14, 40);
	put_le32(head + 18, (unsigned long)w);
	put_le32(head + 22, (unsigned long)h);
	head[26] = 1;                            /* Ebenen   */
	head[28] = 8;                            /* Bit/Pixel */
	put_le32(head + 34, pix);
	put_le32(head + 46, 256);                /* benutzte Farben */

	fwrite(head, 1, sizeof(head), fp);

	const SDL_Color *c = surface->format->palette
	                   ? surface->format->palette->colors : 0;

	for(int i = 0; i < 256; i++)
	{
		unsigned char e[4];

		e[0] = c ? c[i].b : 0;
		e[1] = c ? c[i].g : 0;
		e[2] = c ? c[i].r : 0;
		e[3] = 0;

		fwrite(e, 1, 4, fp);
	}

	static const unsigned char pad[4] = { 0, 0, 0, 0 };

	for(int y = h - 1; y >= 0; y--)
	{
		fwrite((const unsigned char*)surface->pixels + (size_t)y * surface->pitch, 1, w, fp);

		if(row > w) fwrite(pad, 1, row - w, fp);
	}

	fclose(fp);

	return 0;
}

} /* extern "C" */
