/*
 * Wohin die Zeit eines Skalier-Aufrufs geht.
 *
 * Anlass: Der Nutzer meldet, dass grosse Explosionen die Bildrate einbrechen
 * lassen -- auch nach dem AMMX-Blitter, der dort naemlich nicht greift. Die
 * Frage vor jeder weiteren Optimierung lautet: Wie viel eines
 * rotozoomSurface()-Aufrufs ist ueberhaupt Pixelarbeit?
 *
 * Gemessen wird mit den echten Groessen des Falls: ein 16x16-Truemmerteil
 * (assets/planets/rock_effects/debri_*), gedreht und auf 1.0-2.0 skaliert.
 *
 * Bauen und laufen:
 *   tools/build.sh host rotobench
 */
#include <SDL/SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "SDL_rotozoom.h"

#include <math.h>

/* Nicht im Header, aber mit externer Bindung (SDL_rotozoom.cpp:628). */
extern void transformSurfaceY(SDL_Surface *src, SDL_Surface *dst, int cx, int cy,
                              int isin, int icos, int flipx, int flipy);


/* --- Die URFASSUNG der Schleife, zum Vergleich ---------------------------
 * Wortgleich aus SDL_rotozoom.cpp vor dem Umbau. Sie ist der Maßstab: Die
 * neue Fassung darf Byte fuer Byte nichts anderes liefern. */
static void transform_ref(SDL_Surface *src, SDL_Surface *dst, int cx, int cy,
                          int isin, int icos, int flipx, int flipy)
{
	int x, y, dx, dy, xd, yd, sdx, sdy, ax, ay;
	unsigned char *pc, *sp;
	int gap;

	xd = ((src->w - dst->w) << 15);
	yd = ((src->h - dst->h) << 15);
	ax = (cx << 16) - (icos * cx);
	ay = (cy << 16) - (isin * cx);
	pc = (unsigned char*)dst->pixels;
	gap = dst->pitch - dst->w;

	memset(pc, (unsigned char)(src->format->colorkey & 0xff), dst->pitch * dst->h);

	for (y = 0; y < dst->h; y++) {
		dy = cy - y;
		sdx = (ax + (isin * dy)) + xd;
		sdy = (ay - (icos * dy)) + yd;
		for (x = 0; x < dst->w; x++) {
			dx = (short) (sdx >> 16);
			dy = (short) (sdy >> 16);
			if (flipx) dx = (src->w-1)-dx;
			if (flipy) dy = (src->h-1)-dy;
			if ((dx >= 0) && (dy >= 0) && (dx < src->w) && (dy < src->h)) {
				sp = (unsigned char *) (src->pixels);
				sp += (src->pitch * dy + dx);
				*pc = *sp;
			}
			sdx += icos;
			sdy += isin;
			pc++;
		}
		pc += gap;
	}
}

static double jetzt(void)
{
	struct timeval t;

	gettimeofday(&t, nullptr);

	return t.tv_sec + t.tv_usec / 1000000.0;
}

/* Eine 8-Bit-Quellflaeche wie die Truemmerbilder: Palette, Farbschluessel 0. */
static SDL_Surface *quelle_bauen(int w, int h)
{
	SDL_Surface *s = SDL_CreateRGBSurface(SDL_SWSURFACE, w, h, 8, 0, 0, 0, 0);

	if(!s) return nullptr;

	SDL_Color farben[256];

	for(int i = 0; i < 256; i++)
	{
		farben[i].r = (Uint8)i;
		farben[i].g = (Uint8)(255 - i);
		farben[i].b = (Uint8)(i * 3);
	}

	SDL_SetColors(s, farben, 0, 256);
	SDL_SetColorKey(s, SDL_SRCCOLORKEY, 0);

	unsigned char *p = (unsigned char*)s->pixels;

	for(int y = 0; y < h; y++)
		for(int x = 0; x < w; x++)
			p[y * s->pitch + x] = (unsigned char)(((x + y) % 7) ? (x * 16 + y) : 0);

	return s;
}

int main(void)
{
	if(SDL_Init(SDL_INIT_VIDEO) < 0)
	{
		printf("SDL_Init: %s\n", SDL_GetError());

		return 1;
	}

	const int DURCHGAENGE = 20000;

	/* Die Groessen, die im Spiel wirklich vorkommen. */
	struct { int w, h; const char *was; } faelle[] = {
		{ 16, 16, "Truemmer klein (debri_*)" },
		{ 32, 32, "Wrackteil / Feuerball" },
		{ 64, 64, "grosses Einzelteil" },
	};

	/* --- Gegenprobe: neue Schleife gegen die Urfassung, Byte fuer Byte --- */
	{
		int gepruefte = 0, abweichungen = 0;

		for(int w = 7; w <= 40; w += 11)
			for(int h = 5; h <= 40; h += 13)
			{
				SDL_Surface *src = quelle_bauen(w, h);

				if(!src) continue;

				for(int wi = 0; wi < 32; wi++)
					for(int gi = 0; gi <= 20; gi++)
						for(int fl = 0; fl < 4; fl++)
						{
							const double winkel = wi * 11.25;
							const double groesse = 1.0 + gi * 0.05;
							const double rad = winkel * 3.14159265358979 / 180.0;
							const int sa = (int)((sin(rad) / groesse) * 65536.0);
							const int ca = (int)((cos(rad) / groesse) * 65536.0);

							SDL_Surface *a = rotozoomSurface(src, winkel, groesse, 0);

							if(!a) continue;

							SDL_Surface *b = SDL_CreateRGBSurface(SDL_SWSURFACE,
							                                     a->w, a->h, 8, 0, 0, 0, 0);
							if(!b) { SDL_FreeSurface(a); continue; }

							b->format->colorkey = src->format->colorkey;

							transformSurfaceY(src, a, a->w / 2, a->h / 2, sa, ca, fl & 1, fl & 2);
							transform_ref(src, b, a->w / 2, a->h / 2, sa, ca, fl & 1, fl & 2);

							gepruefte++;

							for(int yy = 0; yy < a->h; yy++)
								if(memcmp((unsigned char*)a->pixels + yy * a->pitch,
								          (unsigned char*)b->pixels + yy * b->pitch,
								          a->w) != 0)
								{
									if(abweichungen < 3)
										printf("ABWEICHUNG: %dx%d Winkel %.2f Groesse %.2f flip %d Zeile %d\n",
										       w, h, winkel, groesse, fl, yy);
									abweichungen++;
									break;
								}

							SDL_FreeSurface(a);
							SDL_FreeSurface(b);
						}

				SDL_FreeSurface(src);
			}

		printf("Gegenprobe: %d Faelle geprueft, %d Abweichungen -> %s\n\n",
		       gepruefte, abweichungen, abweichungen ? "FEHLGESCHLAGEN" : "BITGLEICH");

		if(abweichungen) { SDL_Quit(); return 1; }
	}

	printf("Skalier-Aufrufe: %d je Fall, Winkel und Groesse wechselnd\n\n",
	       DURCHGAENGE);
	printf("%-28s %10s %10s %10s %8s\n",
	       "Fall", "gesamt/us", "Pixel/us", "Rest/us", "Rest %");

	for(unsigned f = 0; f < sizeof(faelle) / sizeof(faelle[0]); f++)
	{
		SDL_Surface *src = quelle_bauen(faelle[f].w, faelle[f].h);

		if(!src) { printf("Quelle %d nicht zu belegen\n", faelle[f].w); continue; }

		/* --- 1) der ganze Aufruf ------------------------------------- */
		double t0 = jetzt();
		long pixel_summe = 0;

		for(int i = 0; i < DURCHGAENGE; i++)
		{
			const double winkel = (i % 32) * 11.25;
			const double groesse = 1.0 + ((i % 20) * 0.05);
			SDL_Surface *d = rotozoomSurface(src, winkel, groesse, 0);

			if(d) { pixel_summe += d->w * d->h; SDL_FreeSurface(d); }
		}

		const double ganz = jetzt() - t0;

		/* --- 2) nur die Pixelschleife --------------------------------
		 * Dieselbe Anzahl Aufrufe von transformSurfaceY, auf einer EINMAL
		 * belegten Zielflaeche -- also ohne Belegung, ohne Loeschen, ohne
		 * Palettenkopie. Die Differenz ist der feste Aufwand je Aufruf. */
		SDL_Surface *probe = rotozoomSurface(src, 45.0, 1.5, 0);

		if(!probe) { SDL_FreeSurface(src); continue; }

		t0 = jetzt();

		for(int i = 0; i < DURCHGAENGE; i++)
		{
			const double winkel = (i % 32) * 11.25;
			const double groesse = 1.0 + ((i % 20) * 0.05);
			const double rad = winkel * 3.14159265358979 / 180.0;
			const int sa = (int)((sin(rad) / groesse) * 65536.0);
			const int ca = (int)((cos(rad) / groesse) * 65536.0);

			transformSurfaceY(src, probe, probe->w / 2, probe->h / 2, sa, ca, 0, 0);
		}

		const double nur_pixel = jetzt() - t0;
		const double rest = ganz - nur_pixel;

		printf("%-28s %10.2f %10.2f %10.2f %7.1f%%\n",
		       faelle[f].was,
		       ganz * 1000000.0 / DURCHGAENGE,
		       nur_pixel * 1000000.0 / DURCHGAENGE,
		       rest * 1000000.0 / DURCHGAENGE,
		       ganz > 0 ? rest * 100.0 / ganz : 0.0);

		SDL_FreeSurface(probe);
		SDL_FreeSurface(src);
	}

	/* --- Pruefsumme ueber einen Parameterdurchlauf -------------------------
	 * Erfasst BEIDE Wege (Winkel 0 -> zoomSurfaceY, sonst transformSurfaceY).
	 * Damit laesst sich eine Aenderung am Zoomweg gegenpruefen, fuer den es
	 * keine Urfassung im Programm gibt: Zahl vorher notieren, nachher
	 * vergleichen. */
	{
		unsigned long summe = 0, punkte = 0;

		for(int w = 7; w <= 40; w += 11)
			for(int h = 5; h <= 40; h += 13)
			{
				SDL_Surface *src = quelle_bauen(w, h);

				if(!src) continue;

				for(int wi = 0; wi < 32; wi++)
					for(int gi = 0; gi <= 20; gi++)
					{
						SDL_Surface *d = rotozoomSurface(src, wi * 11.25,
						                                 1.0 + gi * 0.05, 0);
						if(!d) continue;

						for(int yy = 0; yy < d->h; yy++)
						{
							const unsigned char *r =
								(const unsigned char*)d->pixels + yy * d->pitch;

							for(int xx = 0; xx < d->w; xx++)
							{
								summe = summe * 33u + r[xx];
								punkte++;
							}
						}

						SDL_FreeSurface(d);
					}

				SDL_FreeSurface(src);
			}

		printf("Pruefsumme ueber beide Wege: 0x%08lx (%lu Bildpunkte)\n\n",
		       summe, punkte);
	}

	/* --- Reiner Zoom (Winkel 0) -------------------------------------------
	 * Dieser Weg (zoomSurfaceY) gilt fuer die Mehrheit der Explosions-Effekte:
	 * ESideExplosion, EDeathSparks, EToughMushroom, ERobotTurrent,
	 * ERockParticle und EUnitParticle setzen KEINEN Winkel. */
	printf("\nReiner Zoom (Winkel 0, also zoomSurfaceY):\n");
	printf("%-28s %10s\n", "Fall", "gesamt/us");

	for(unsigned f = 0; f < sizeof(faelle) / sizeof(faelle[0]); f++)
	{
		SDL_Surface *src = quelle_bauen(faelle[f].w, faelle[f].h);

		if(!src) continue;

		const double t0 = jetzt();

		for(int i = 0; i < DURCHGAENGE; i++)
		{
			const double groesse = 1.0 + ((i % 20) * 0.05);
			SDL_Surface *d = rotozoomSurface(src, 0.0, groesse, 0);

			if(d) SDL_FreeSurface(d);
		}

		printf("%-28s %10.2f\n", faelle[f].was,
		       (jetzt() - t0) * 1000000.0 / DURCHGAENGE);

		SDL_FreeSurface(src);
	}

	printf("\n\"Rest\" = Belegung + Loeschen + Palettenkopie + SetColorKey.\n"
	       "Hinweis: \"Rest\" ist eine Differenz zweier Messungen mit\n"
	       "unterschiedlich grossen Zielflaechen und deshalb nur grob.\n");

	SDL_Quit();

	return 0;
}
