/*
 * Pruefstand fuer die eigene Grafikschicht (port/amiga/sdl_video.cpp).
 *
 * Warum es ihn gibt: Auf dem Amiga endete der erste Lauf der SDL-freien
 * Fassung mit Guru 81000005 (AN_MemCorrupt) -- also einem Schreibzugriff ueber
 * eine Speicherbelegung hinaus. Auf dem Amiga ist so etwas nur mit
 * Emulatorlaeufen von acht Minuten und den Speicherwaechtern zu finden.
 *
 * Derselbe Quelltext laeuft aber auch auf dem Host: Er haengt nur an
 * <SDL/SDL.h> (unserem eigenen Kopf), zod_log, zod_palette und
 * zod_big_alloc/zod_big_free. Damit laesst er sich hier unter valgrind gegen
 * die ECHTEN Archive fahren, und jeder Ueberlauf wird auf Byte und Zeile
 * genau gemeldet.
 *
 * Bauen: tools/build.sh shimtest
 */
#include <SDL/SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "SDL_rotozoom.h"

#include "zod_pack.h"
#include "zod_palette.h"

static int fehler = 0;

static void pruefe(int bedingung, const char *was)
{
	if(!bedingung)
	{
		printf("  FEHLER: %s\n", was);
		fehler++;
	}
}

/* Die Teamfarben entstehen genau so wie in ZTeam::Make. */
static void team_umsetzen(SDL_Surface *s, const unsigned char *xlat)
{
	if(!s || s->format->BytesPerPixel != 1 || !xlat) return;

	SDL_Surface *conv = SDL_ConvertSurface(s, s->format, s->flags);

	if(!conv) return;

	for(int y = 0; y < conv->h; y++)
	{
		unsigned char *row = (unsigned char*)conv->pixels + y * conv->pitch;

		for(int x = 0; x < conv->w; x++) row[x] = xlat[row[x]];
	}

	SDL_FreeSurface(conv);
}

int main(int argc, char **argv)
{
	const char *dir = argc > 1 ? argv[1] : "data/game/packs";

	printf("Pruefstand Grafikschicht, Archive aus %s\n", dir);

	/* ---------------------------------------------- Flaechen und Blits */

	/* Absichtlich schiefe Breiten: Bei ihnen ist die Zeilenlaenge groesser
	 * als w, und genau da verrechnet sich ein Blitter gern. */
	static const int breiten[] = { 1, 2, 3, 5, 7, 13, 31, 33, 63, 65, 127, 129 };

	for(unsigned i = 0; i < sizeof(breiten) / sizeof(breiten[0]); i++)
		for(unsigned j = 0; j < sizeof(breiten) / sizeof(breiten[0]); j++)
		{
			const int w = breiten[i], h = breiten[j];

			SDL_Surface *a = SDL_CreateRGBSurface(0, w, h, 8, 0, 0, 0, 0);
			SDL_Surface *b = SDL_CreateRGBSurface(0, 64, 64, 8, 0, 0, 0, 0);

			pruefe(a && b, "Flaeche angelegt");
			if(!a || !b) continue;

			memset(a->pixels, 0x55, (size_t)a->pitch * a->h);

			SDL_SetColorKey(a, SDL_SRCCOLORKEY, 0);

			/* in die Mitte, ueber den rechten Rand, ueber den unteren,
			 * ganz ausserhalb, negativ */
			static const int pos[][2] = {
				{ 0, 0 }, { 10, 10 }, { 60, 60 }, { 63, 63 },
				{ -5, -5 }, { 64, 0 }, { 0, 64 }, { -1000, -1000 },
				{ 1000, 1000 }
			};

			for(unsigned k = 0; k < sizeof(pos) / sizeof(pos[0]); k++)
			{
				SDL_Rect r;

				r.x = (Sint16)pos[k][0];
				r.y = (Sint16)pos[k][1];
				r.w = 0; r.h = 0;

				SDL_BlitSurface(a, 0, b, &r);

				SDL_Rect f;

				f.x = (Sint16)pos[k][0];
				f.y = (Sint16)pos[k][1];
				f.w = (Uint16)w;
				f.h = (Uint16)h;

				SDL_FillRect(b, &f, 7);
			}

			SDL_Surface *c = SDL_ConvertSurface(a, a->format, a->flags);

			if(c) SDL_FreeSurface(c);

			SDL_FreeSurface(a);
			SDL_FreeSurface(b);
		}

	printf("Flaechen und Blits: geprueft\n");

	/* ------------------------------------------------- Durchsichtigkeit */

	/* Ein gedrehtes Bild muss an den Ecken durchsichtig sein, nicht schwarz.
	 *
	 * Genau das ging einmal verloren: SDL_rotozoom faerbt die Zielflaeche
	 * ueber SDL_MapRGB(Farbe des Schluesselplatzes) vor. Lieferte SDL_MapRGB
	 * dafuer nicht wieder den Schluesselplatz, war der Rand deckend --
	 * auf der V2 als schwarzer Hintergrund der Objekte zu sehen. */
	{
		SDL_Surface *q = SDL_CreateRGBSurface(0, 32, 32, 8, 0, 0, 0, 0);

		if(q)
		{
			memset(q->pixels, 5, (size_t)q->pitch * q->h);
			SDL_SetColorKey(q, SDL_SRCCOLORKEY, 0);

			SDL_Surface *d = rotozoomSurface(q, 45.0, 1.0, 0);

			pruefe(d != 0, "gedrehte Flaeche entstanden");

			if(d)
			{
				const unsigned char *px = (const unsigned char*)d->pixels;

				pruefe((d->flags & SDL_SRCCOLORKEY) != 0,
				       "gedrehte Flaeche hat einen Farbschluessel");
				pruefe(d->format->colorkey == px[0],
				       "linke obere Ecke ist der Schluessel (sonst deckender Rand)");
				pruefe(px[0] == px[d->w - 1],
				       "rechte obere Ecke ebenso");

				SDL_FreeSurface(d);
			}

			SDL_FreeSurface(q);
		}
	}

	printf("Durchsichtigkeit beim Drehen: geprueft\n");

	/* ------------------------------------------------- echte Archive */

	if(!zod_palette_load(dir))
	{
		printf("  (keine palette.zpl in %s -- Archivteil uebersprungen)\n", dir);
		printf("%s\n", fehler ? "FEHLGESCHLAGEN" : "BESTANDEN");
		return fehler ? 1 : 0;
	}

	if(!zod_pack_init(dir))
	{
		printf("  (keine Archive in %s -- Archivteil uebersprungen)\n", dir);
		printf("%s\n", fehler ? "FEHLGESCHLAGEN" : "BESTANDEN");
		return fehler ? 1 : 0;
	}

	SDL_Surface *ziel = SDL_CreateRGBSurface(0, 640, 480, 8, 0, 0, 0, 0);

	pruefe(ziel != 0, "Zeichenflaeche angelegt");

	/* Quellrechteck mit Breite 0 heisst NICHTS zeichnen, nicht "ganze Breite".
	 *
	 * ZSDL_Surface::GetMapBlitInfo rechnet fuer ein Sprite am rechten
	 * Kartenrand from_rect.w = view_w - to_rect.x -- genau 0, sobald die
	 * Einheit die Grenze zur Anzeige erreicht. Deutete der Blitter das als
	 * "ganze Breite", malte er das volle Sprite dort hin, also mitten in das
	 * HUD. Auf der V1200 gesehen als "Objekte zeichnen manchmal am rechten
	 * Rand in die HUD hinein". */
	if(ziel)
	{
		SDL_Surface *muster = SDL_CreateRGBSurface(0, 16, 16, 8, 0, 0, 0, 0);

		if(muster)
		{
			memset(muster->pixels, 0x2a, (size_t)muster->pitch * muster->h);
			memset(ziel->pixels, 0, (size_t)ziel->pitch * ziel->h);

			SDL_Rect von = { 0, 0, 0, 16 };     /* Breite 0 */
			SDL_Rect nach = { 100, 100, 0, 0 };

			SDL_BlitSurface(muster, &von, ziel, &nach);

			int gemalt = 0;

			for(int y = 90; y < 130; y++)
				for(int x = 90; x < 130; x++)
					if(((Uint8*)ziel->pixels)[y * ziel->pitch + x]) gemalt++;

			pruefe(gemalt == 0, "srcrect mit Breite 0 zeichnet nichts");

			/* Gegenprobe: mit Breite 5 muessen genau 5 Spalten kommen. */
			memset(ziel->pixels, 0, (size_t)ziel->pitch * ziel->h);
			von.w = 5;
			nach.x = 100; nach.y = 100;
			SDL_BlitSurface(muster, &von, ziel, &nach);

			gemalt = 0;

			for(int y = 90; y < 130; y++)
				for(int x = 90; x < 130; x++)
					if(((Uint8*)ziel->pixels)[y * ziel->pitch + x]) gemalt++;

			pruefe(gemalt == 5 * 16, "srcrect mit Breite 5 zeichnet 5 Spalten");

			SDL_FreeSurface(muster);
		}
	}

	const char **namen = 0;
	unsigned int anzahl = zod_pack_image_names(&namen);

	printf("Archive: %u Bilder\n", anzahl);

	unsigned int geladen = 0;

	for(unsigned int i = 0; i < anzahl; i++)
	{
		SDL_Surface *s = zod_pack_load_quiet(namen[i]);

		if(!s) continue;

		geladen++;

		/* Jedes Bild einmal zeichnen -- an eine Stelle, an der es ueber
		 * beide Raender hinausragt. */
		if(ziel)
		{
			SDL_Rect r;

			r.x = (Sint16)(600 - (i % 64));
			r.y = (Sint16)(450 - (i % 32));
			r.w = 0; r.h = 0;

			SDL_BlitSurface(s, 0, ziel, &r);
		}

		/* und einmal als Teamfarbe umsetzen */
		if(s->format->BytesPerPixel == 1)
			team_umsetzen(s, zod_palette_xlat(2 + (i % 7)));

		/* Drehen und Skalieren -- fremder Quelltext (SDL_rotozoom), der auf
		 * diesem Unterbau zum ersten Mal 8-Bit-Flaechen bekommt. Jedes
		 * zwanzigste Bild, sonst dauert der Lauf zu lange. */
		if((i % 20) == 0)
		{
			static const double winkel[] = { 0.0, 7.5, 45.0, 90.0, 180.0, 271.3, 359.9 };
			static const double groesse[] = { 0.25, 0.5, 1.0, 1.3, 2.0 };

			for(unsigned a = 0; a < sizeof(winkel) / sizeof(winkel[0]); a++)
			{
				SDL_Surface *r = rotozoomSurface(s, winkel[a],
				                                 groesse[a % 5], 0);

				if(r) SDL_FreeSurface(r);
			}

			SDL_Surface *z = zoomSurface(s, 1.7, 0.6, 0);

			if(z) SDL_FreeSurface(z);
		}

		SDL_FreeSurface(s);
	}

	printf("Bilder geladen, gezeichnet, umgefaerbt: %u\n", geladen);

	if(ziel) SDL_FreeSurface(ziel);

	printf("%s\n", fehler ? "FEHLGESCHLAGEN" : "BESTANDEN");

	return fehler ? 1 : 0;
}
