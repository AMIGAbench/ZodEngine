/*
 * Prueft SDL_FillRect auf dem echten Bildschirm: kleines Rechteck fuellen,
 * Pixel zurueckholen, gefuellte Spanne je Zeile melden.
 *
 * Anlass war ein Verdacht, der sich NICHT bestaetigt hat: dass der
 * Hardware-Fuellweg (CGX_FillHWRect -> FillPixelArray) die zu breiten
 * Lebensbalken verursacht. Ursache war ein negatives Rechteck aus einer
 * falsch gerundeten double->int-Wandlung (siehe Makefile, -fbbb). Im
 * Emulator fuellt der Hardwareweg nachgewiesen richtig. Die Probe prueft ihn
 * und zum Vergleich den Softwareweg, mit denselben Schaltern wie das Spiel.
 *
 * Aufruf: fill_probe [tiefe]   (Vorgabe 16)
 */
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <SDL/SDL.h>

#include "debug.h"

/* Auf der V2 gibt es keinen seriellen Anschluss: jede Meldung geht deshalb
 * zusaetzlich auf die Konsole (umlenkbar mit > RAM:fill.txt). vsnprintf ist
 * auf dieser Laufzeit korrekt, nur sprintf aus libamiga liest %d als 16 Bit. */
static void report(const char *fmt, ...)
{
	char buf[256];
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);

	dbg_printf("%s", buf);
	fputs(buf, stdout);
	fflush(stdout);
}

struct probe_rect { int x, y, w, h; };

static const probe_rect rects[] = {
	{ 100, 100, 12, 4 },   //Lebensbalken, schwarzer Rahmen
	{ 101, 101,  3, 2 },   //gruener Anteil
	{ 300, 200, 36, 2 },
	{ 400, 300,  1, 5 },   //Ecke eines Auswahlrahmens
	{ 500, 150,  5, 1 },
};
static const int rect_count = sizeof(rects) / sizeof(rects[0]);

static Uint32 read_pixel(SDL_Surface *s, int x, int y)
{
	Uint8 *p = (Uint8 *)s->pixels + y * s->pitch + x * s->format->BytesPerPixel;

	switch(s->format->BytesPerPixel)
	{
	case 1: return *p;
	case 2: return *(Uint16 *)p;
	case 3: return (p[0] << 16) | (p[1] << 8) | p[2];
	default: return *(Uint32 *)p;
	}
}

static void clear_screen(SDL_Surface *s)
{
	if(SDL_MUSTLOCK(s)) SDL_LockSurface(s);

	for(int y = 0; y < s->h; y++)
		memset((Uint8 *)s->pixels + y * s->pitch, 0, s->w * s->format->BytesPerPixel);

	if(SDL_MUSTLOCK(s)) SDL_UnlockSurface(s);
}

static void soft_fill(SDL_Surface *s, const probe_rect &r, Uint32 color)
{
	if(SDL_MUSTLOCK(s)) SDL_LockSurface(s);

	for(int y = r.y; y < r.y + r.h; y++)
		for(int x = r.x; x < r.x + r.w; x++)
		{
			Uint8 *p = (Uint8 *)s->pixels + y * s->pitch + x * s->format->BytesPerPixel;

			if(s->format->BytesPerPixel == 2) *(Uint16 *)p = (Uint16)color;
			else if(s->format->BytesPerPixel == 4) *(Uint32 *)p = color;
			else *p = (Uint8)color;
		}

	if(SDL_MUSTLOCK(s)) SDL_UnlockSurface(s);
}

/* Meldet fuer jede Zeile des Rechtecks (und je eine darueber/darunter) die
 * gefuellte Spanne. Soll: genau x .. x+w-1 in den Zeilen y .. y+h-1. */
static int check(SDL_Surface *s, const char *way, const probe_rect &r)
{
	int bad = 0;

	if(SDL_MUSTLOCK(s)) SDL_LockSurface(s);

	for(int y = r.y - 1; y <= r.y + r.h; y++)
	{
		int first = -1, last = -1;
		long count = 0;

		for(int x = 0; x < s->w; x++)
			if(read_pixel(s, x, y))
			{
				if(first < 0) first = x;
				last = x;
				count++;
			}

		const bool inside = (y >= r.y && y < r.y + r.h);
		const bool ok = inside ? (first == r.x && last == r.x + r.w - 1 && count == r.w)
		                       : (count == 0);

		if(!ok)
		{
			bad++;
			report("FILL %s rect %ld,%ld,%ld,%ld zeile %ld: von %ld bis %ld, %ld Pixel -- FALSCH\n",
			           way, (long)r.x, (long)r.y, (long)r.w, (long)r.h,
			           (long)y, (long)first, (long)last, count);
		}
	}

	if(SDL_MUSTLOCK(s)) SDL_UnlockSurface(s);

	if(!bad)
		report("FILL %s rect %ld,%ld,%ld,%ld: richtig\n",
		           way, (long)r.x, (long)r.y, (long)r.w, (long)r.h);

	return bad;
}

int main(int argc, char **argv)
{
	int depth = argc > 1 ? atoi(argv[1]) : 16;

	dbg_boot();

	if(SDL_Init(SDL_INIT_VIDEO) < 0)
	{
		dbg_fail("fill_probe: SDL_Init");
		return 1;
	}

	//dieselben Schalter wie ZPlayer::InitSDL im Vollbild
	SDL_Surface *s = SDL_SetVideoMode(640, 480, depth,
	                                  SDL_HWSURFACE | SDL_RESIZABLE | SDL_FULLSCREEN);
	if(!s)
	{
		dbg_fail("fill_probe: SDL_SetVideoMode");
		SDL_Quit();
		return 1;
	}

	const SDL_VideoInfo *vi = SDL_GetVideoInfo();

	report("FILL Schirm %ldx%ld, %ld Bit, hw=%ld fill=%ld pitch=%ld\n",
	           (long)s->w, (long)s->h, (long)s->format->BitsPerPixel,
	           (long)((s->flags & SDL_HWSURFACE) ? 1 : 0),
	           (long)(vi ? vi->blit_fill : -1), (long)s->pitch);

	Uint32 color = SDL_MapRGB(s->format, 82, 190, 33);
	int bad_hw = 0, bad_sw = 0;

	for(int i = 0; i < rect_count; i++)
	{
		const probe_rect &r = rects[i];
		SDL_Rect sr;

		clear_screen(s);
		sr.x = r.x; sr.y = r.y; sr.w = r.w; sr.h = r.h;
		SDL_FillRect(s, &sr, color);
		bad_hw += check(s, "SDL_FillRect", r);

		clear_screen(s);
		soft_fill(s, r, color);
		bad_sw += check(s, "Software", r);
	}

	SDL_Quit();

	report("FILL Fehlzeilen: SDL_FillRect %ld, Software %ld\n", (long)bad_hw, (long)bad_sw);

	if(bad_hw || bad_sw)
		dbg_fail("fill_probe: Abweichungen");
	else
		dbg_ok("fill_probe: alle Rechtecke richtig");

	return 0;
}
