/* Vollbilder des Originals anzeigen. Begruendung: zod_schirm.h */

#include "zod_schirm.h"

#include <string.h>
#include <stdio.h>

#include <SDL/SDL.h>

#include "zod_pack.h"
#include "zod_log.h"
#include "zod_palette.h"

#ifdef __amigaos__
/* aus port/amiga/sdl_screen.cpp -- die Schirmpalette wirklich setzen
 * (LoadRGB32 auf den ViewPort) und wieder zuruecknehmen. */
extern "C" void zod_sdl_set_shared_palette(const SDL_Color *colors);
extern "C" void zod_sdl_palette_changed(void);
extern "C" void zod_sdl_reload_palette(void);

/* aus port/amiga/sdl_video.cpp -- DAS GANZE Bild als schmutzig vermerken.
 *
 * SDL_Flip kopiert auf dem Amiga nur die vermerkten Bloecke. Dieses Modul
 * schreibt aber DIREKT in screen->pixels und geht damit an SDL_FillRect
 * und am Blitter vorbei -- ohne diesen Vermerk landet nichts davon im
 * Grafikspeicher.
 *
 * Beim Ladebild fiel das nicht auf: der Schirm ist da frisch geoeffnet und
 * ohnehin ganz schmutzig. Am Rundenende schon -- der Nutzer meldete, das
 * Spiel schalte "direkt zum Lose Video, waehrend noch die Karte aktiv
 * ist", und genau das war es: der Statistikbildschirm wurde gezeichnet
 * und nie ausgegeben.
 *
 * Dieselbe Falle gibt es bei ZSDL_FillDots. */
extern "C" void zod_dirty_force_full(void);

/* aus port/amiga/sdl_screen.cpp -- Systemzeiger zeigen/verstecken. */
extern "C" void zod_schirm_zeiger_amiga(int an);
#else
static void zod_dirty_force_full(void) {}
#endif

namespace
{

int steht = 0;          /* eine fremde Palette ist gesetzt */

/* Das zuletzt gezeigte Bild bleibt liegen -- zod_schirm_putzen holt
 * daraus einen Ausschnitt zurueck. Ohne das bliebe jede ueberschriebene
 * Schrift als Schmutz stehen, und die Knoepfe koennten ihre Helligkeit
 * nicht wechseln. Freigegeben in zod_schirm_ende. */
SDL_Surface *letzt_bild = 0;

/* Lage und Groesse des zuletzt gezeigten Bildes -- damit zod_schirm_text
 * in BILDkoordinaten rechnen kann und die Anordnung nicht an der
 * Aufloesung haengt. */
int letzt_f = 1, letzt_x = 0, letzt_y = 0;

/* Die Palette des zuletzt gezeigten Bildes. Der Text wird dorthin
 * umgesetzt. */
SDL_Color bild_farben[256];
SDL_Palette bild_palette = { 256, bild_farben };

/* Planetennamen in der Reihenfolge von planet_type (constants.h).
 * Bewusst hier und nicht aus der Engine geholt: dieses Modul soll ohne
 * Engine-Koepfe uebersetzbar bleiben. Die Reihenfolge ist durch das
 * Archiv festgelegt, das ZExtract schreibt -- beide Tabellen stehen in
 * tools/cd/amiga/schirme.c und hier, und sie muessen zusammenpassen. */
const char *planet_name[5] =
	{ "desert", "volcanic", "arctic", "jungle", "city" };

/* Teamfarben. Die Reihenfolge ist die von team_type (RED, BLUE, GREEN,
 * YELLOW, ...). Alles dahinter gibt es im Original nicht. */
const char *team_name[4] = { "red", "blue", "green", "yellow" };

/* --- Schrift --------------------------------------------------------
 *
 * Die Glyphen liegen als assets/fonts/<satz>/char_NNN.png im Archiv
 * (NNN = ASCII-Code) und sind in die GEMEINSAME Palette indiziert. Auf
 * dem Schirm steht waehrend eines Vollbildes aber dessen eigene Palette
 * -- die Indizes muessen also umgesetzt werden, sonst kaeme die Schrift
 * in beliebigen Farben.
 *
 * Die Umsetztabelle erhaelt die SCHATTIERUNG: eine flache Maske (alles,
 * was nicht 0 ist, weiss) saehe nach Schreibmaschine aus statt nach dem
 * Original -- und die Originalschrift IST schattiert (siehe das
 * DOSBox-Bildschirmfoto des Nutzers). */
/* --- Die Originalschrift, und warum die Farbe vorher falsch war ----
 *
 * Gemessen am DOSBox-Bild des Nutzers (23.09.), nicht geraten:
 *
 * 1. ES IST EINE EINZIGE SCHRIFT, font19, in zwei Groessen.
 *    Sprite 6448+0..35 sind A..Z und 0..9, 19 Punkt hoch (Titel,
 *    Knoepfe, "LEVEL 01" und der Levelname); Sprite 6512+0..35 ist
 *    dieselbe Schrift 12 Punkt hoch (die Statistikzeilen, "LOADING").
 *    Dahinter je sechs Satzzeichen . , ? ! * : -- die kleine Fassung
 *    hat zusaetzlich einen Bindestrich.
 *    Nachgerechnet: "QUIT" 70 gegen 70 gemessen, "UNITS KILLED" 147
 *    gegen 147, "YOU LOST" 141 gegen 140,4, "VIRGIN SOLDIERS" 247
 *    gegen 248,2.
 *
 * 2. DIE FARBE STECKT IN DER PALETTE DES BILDES, NICHT IN DER SCHRIFT.
 *    Die Glyphen benutzen im Original NUR die VGA-Plaetze 1..4, und
 *    jedes dieser Vollbilder belegt genau diese vier gleich:
 *
 *        1 = (120,120,143)   2 = (156,166,176)
 *        3 = (255,255,255)   4 = (10,10,12)
 *
 *    -- eine Grauverlauf-Rampe auf Weiss plus schwarzer Schatten.
 *    (Plaetze 5..16 sind vier Teamfarben-Rampen; sie gehoeren zu den
 *    farbigen Fassungen derselben Schrift, font21..font25.)
 *
 *    Im Bestand tragen die Glyphen aber die Farben der SPRITES.RSC-
 *    Palette, also BRAUN. Die bisherige Fassung suchte dazu die
 *    naechste Farbe im Bild -- und schrieb damit braune Schrift auf
 *    das Ladebild. Genau das hat der Nutzer gemeldet.
 *
 *    Richtig ist: den ORIGINALINDEX schreiben, nicht die Farbe. Die
 *    Rueckrechnung geht ueber die vier bekannten Braunwerte; sie liegen
 *    im RGB-Wuerfel weit auseinander, die Zuordnung ist eindeutig.
 *
 * 3. Die Knoepfe sind im Original GRAU, nicht weiss (gemessen
 *    (154,154,174) = Platz 2). Das Original dimmt also die Rampe um
 *    eine Stufe. Daran haengt hier die Rueckmeldung: der Knopf unter
 *    dem Zeiger ist weiss, die uebrigen sind grau -- ohne das ist
 *    nicht zu sehen, dass sie anklickbar sind.
 */

/* Zeichenreihenfolge der Sprites, ab Satzanfang. */
const char SATZ_ZEICHEN[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.,?!*:-";

struct SatzInfo
{
	const char *name;    /* Name fuer zod_schirm_schrift */
	const char *ordner;  /* Ordner im Archiv */
	int basis;           /* Spritenummer von 'A' */
	int abstand;         /* Punkte zwischen zwei Zeichen */
	int leer;            /* Vorschub des Leerzeichens */
};

const SatzInfo saetze[] =
{
	/* Gemessen: gross und klein laufen mit Abstand 2, die Zeile
	 * "LOADING" auf dem Ladebild dagegen eng (79 gegen 91 Punkte
	 * gerechnet, 77,5 gemessen). Deshalb ein eigener Eintrag statt
	 * einer geratenen Mitte. */
	{ "gross",  "font19", 6448, 2, 8 },
	{ "klein",  "font19", 6512, 2, 8 },
	{ "laden",  "font19", 6512, 0, 8 },
	{ 0, 0, 0, 0, 0 }
};

/* Die vier Braunwerte der SPRITES.RSC-Palette, wie sie im Bestand
 * stehen, in der Reihenfolge der VGA-Plaetze 1..4. */
const struct { int r, g, b; } satz_farbe[4] =
{
	{  63,  31,   0 },   /* 1 */
	{  95,  63,  31 },   /* 2 */
	{ 127,  95,  63 },   /* 3 -- der Koerper der Schrift */
	{  63,  63,  95 }    /* 4 -- der Schatten */
};

char schrift_satz[16] = "gross";
int schrift_skala = 1;
int schrift_hell = 1;
const SatzInfo *schrift_info = &saetze[0];

SDL_Surface *glyph[96];          /* ASCII 32..127 */
int glyph_vor[96];               /* Vorschub, aus den deckenden Punkten */
int glyph_geholt[96];            /* schon einmal versucht */

/* Rohwert der Glyphenflaeche -> VGA-Platz 1..4. Alle Glyphen eines
 * Satzes teilen dieselbe Palette, eine Tabelle genuegt. */
unsigned char vga[256];
int vga_gut = 0;

/* Fuer fremde Saetze (nicht font19): der alte Weg ueber die naechste
 * Farbe. */
unsigned char umsetz[256];
int umsetz_gut = 0;

/* Den Platz suchen, der einer Farbe am naechsten kommt. Quadratischer
 * Abstand im RGB-Wuerfel -- fuer eine Schriftumsetzung genuegt das; eine
 * wahrnehmungsrichtige Metrik waere hier Scheingenauigkeit. */
int naechster_platz(const SDL_Palette *p, int r, int g, int b)
{
	int best = 0;
	long best_d = -1;
	int i;

	if(!p) return 0;

	for(i = 0; i < p->ncolors; i++)
	{
		long dr = (long)p->colors[i].r - r;
		long dg = (long)p->colors[i].g - g;
		long db = (long)p->colors[i].b - b;
		long d = dr * dr + dg * dg + db * db;

		if(best_d < 0 || d < best_d) { best_d = d; best = i; }
	}

	return best;
}

void glyphen_weg(void)
{
	int i;

	for(i = 0; i < 96; i++)
	{
		if(glyph[i]) { SDL_FreeSurface(glyph[i]); glyph[i] = 0; }
		glyph_vor[i] = 0;
		glyph_geholt[i] = 0;
	}

	vga_gut = 0;
	umsetz_gut = 0;
}

/* Platz eines Zeichens im Satz, oder -1. */
int satz_platz(int c)
{
	const char *p;

	if(c >= 'a' && c <= 'z') c = c - 'a' + 'A';

	p = strchr(SATZ_ZEICHEN, c);

	return (p && c) ? (int)(p - SATZ_ZEICHEN) : -1;
}

SDL_Surface *glyphe(int c)
{
	char name[64];
	SDL_Surface *g;

	if(c < 32 || c > 127) c = 32;

	if(glyph_geholt[c - 32]) return glyph[c - 32];

	glyph_geholt[c - 32] = 1;

	if(schrift_info)
	{
		int k = satz_platz(c);

		if(k < 0) return 0;   /* Leerzeichen, Unbekanntes */

		snprintf(name, sizeof(name), "assets/fonts/%s/SPRITES_%d.png",
		         schrift_info->ordner, schrift_info->basis + k);
	}
	else
	{
		snprintf(name, sizeof(name), "assets/fonts/%s/char_%03d.png",
		         schrift_satz, c);
	}

	g = zod_pack_load_quiet(name);

	glyph[c - 32] = g;

	/* Der Vorschub kommt aus den DECKENDEN Punkten, nicht aus der
	 * Feldbreite: die Felder sind 24 bzw. 16 Punkte breit, die
	 * Buchstaben darin 7 bis 23. Mit der Feldbreite stuende alles
	 * viel zu weit auseinander. */
	if(g)
	{
		int x, y, rechts = -1;

		if(SDL_MUSTLOCK(g)) SDL_LockSurface(g);

		for(y = 0; y < g->h; y++)
		{
			const Uint8 *q = (const Uint8 *)g->pixels + y * g->pitch;

			for(x = g->w - 1; x > rechts; x--)
				if(q[x]) { rechts = x; break; }
		}

		if(SDL_MUSTLOCK(g)) SDL_UnlockSurface(g);

		glyph_vor[c - 32] = rechts + 1
		                  + (schrift_info ? schrift_info->abstand : 1);
	}

	return g;
}

/* Vorschub eines Zeichens in BILDpunkten (ohne Skalierung). */
int glyph_breite(int c)
{
	SDL_Surface *g = glyphe(c);

	if(g) return glyph_vor[c - 32];

	return schrift_info ? schrift_info->leer : 8;
}

/* Einen Punktblock der Groesse letzt_f setzen -- ein "Bildpunkt" des
 * 320er Bildes. */
void punkt(SDL_Surface *s, int px, int py, Uint32 farbe)
{
	int k, l;

	for(l = 0; l < letzt_f; l++)
	{
		int yy = py + l;
		Uint8 *z8;
		Uint16 *z16;
		Uint32 *z32;

		if(yy < 0 || yy >= s->h) continue;

		switch(s->format->BytesPerPixel)
		{
		case 1:
			z8 = (Uint8 *)s->pixels + yy * s->pitch + px;
			for(k = 0; k < letzt_f; k++)
				if(px + k >= 0 && px + k < s->w) z8[k] = (Uint8)farbe;
			break;
		case 2:
			z16 = (Uint16 *)((Uint8 *)s->pixels + yy * s->pitch) + px;
			for(k = 0; k < letzt_f; k++)
				if(px + k >= 0 && px + k < s->w) z16[k] = (Uint16)farbe;
			break;
		case 4:
			z32 = (Uint32 *)((Uint8 *)s->pixels + yy * s->pitch) + px;
			for(k = 0; k < letzt_f; k++)
				if(px + k >= 0 && px + k < s->w) z32[k] = farbe;
			break;
		default:
			break;
		}
	}
}

}

extern "C" {

void zod_schirm_name(char *aus, int platz, const char *anlass,
                     int planet, int team)
{
	if(!aus || platz <= 0) return;

	if(planet < 0 || planet > 4) planet = 0;
	if(team   < 0 || team   > 3) team   = 0;   /* Rueckfall auf rot */

	snprintf(aus, platz, "assets/screens/%s_%s_%s",
	         anlass ? anlass : "load", planet_name[planet], team_name[team]);
}

int zod_schirm_da(const char *name)
{
	SDL_Surface *s;

	if(!name || !*name) return 0;

	s = zod_pack_load_quiet(name);

	if(!s) return 0;

	SDL_FreeSurface(s);

	return 1;
}

void zod_schirm_schrift(const char *satz, int skala)
{
	const SatzInfo *si = 0;
	int i;

	schrift_skala = (skala > 0) ? skala : 1;

	if(!satz || !*satz) return;
	if(!strcmp(satz, schrift_satz)) return;

	for(i = 0; saetze[i].name; i++)
		if(!strcmp(saetze[i].name, satz)) { si = &saetze[i]; break; }

	glyphen_weg();

	schrift_info = si;
	strncpy(schrift_satz, satz, sizeof(schrift_satz) - 1);
	schrift_satz[sizeof(schrift_satz) - 1] = 0;
}

void zod_schirm_hell(int an)
{
	schrift_hell = an ? 1 : 0;
}

int zod_schirm_zeigen(const char *name)
{
	SDL_Surface *bild;
	SDL_Surface *schirm = SDL_GetVideoSurface();
	int fx, fy, f, zx, zy, x, y;

	if(!name || !*name || !schirm) return 0;

	if(letzt_bild) { SDL_FreeSurface(letzt_bild); letzt_bild = 0; }

	bild = zod_pack_load_quiet(name);

	if(!bild)
	{
		/* NICHT still zurueckkehren. Fehlt ein Bildschirm, soll das im
		 * Protokoll stehen -- sonst ist "kein Statistikbildschirm" von
		 * "Statistikbildschirm kaputt" nicht zu unterscheiden. */
		ZLOG("Schirm: %s ist nicht im Archiv\n", name);

		return 0;
	}

	if(bild->format->BitsPerPixel != 8 || !bild->format->palette)
	{
		ZLOG("Schirm: %s ist nicht 8 Bit (%d)\n", name,
		     (int)bild->format->BitsPerPixel);
		SDL_FreeSurface(bild);

		return 0;
	}

	/* Ganzzahliger Faktor, sonst entstehen beim Vergroessern ungleich
	 * breite Spalten -- bei einem 320er Bild auf 640 faellt das sofort
	 * auf. Mindestens 1, auch wenn der Schirm kleiner ist. */
	fx = schirm->w / bild->w;
	fy = schirm->h / bild->h;
	f  = (fx < fy) ? fx : fy;

	if(f < 1) f = 1;

	zx = (schirm->w - bild->w * f) / 2;
	zy = (schirm->h - bild->h * f) / 2;

	if(zx < 0) zx = 0;
	if(zy < 0) zy = 0;

	/* Palette merken, Umsetztabelle verwerfen -- sie gilt je Bild. */
	{
		int n = bild->format->palette->ncolors;

		if(n > 256) n = 256;

		memcpy(bild_farben, bild->format->palette->colors,
		       (size_t)n * sizeof(SDL_Color));

		umsetz_gut = 0;
	}

	if(schirm->format->BitsPerPixel == 8)
	{
		/* 8 Bit: die Indizes wandern UNVERAENDERT in den Schirm, also
		 * muss die Schirmpalette die des Bildes werden. ERST die
		 * Palette, DANN zeichnen -- andersherum blitzt fuer ein Bild
		 * die alte Palette auf dem neuen Bild auf. */
#ifdef __amigaos__
		zod_sdl_set_shared_palette(bild->format->palette->colors);
		zod_sdl_palette_changed();
#else
		SDL_SetColors(schirm, bild->format->palette->colors, 0,
		              bild->format->palette->ncolors);
#endif
		steht = 1;

		SDL_FillRect(schirm, 0, 0);       /* Platz 0 ist ueberall schwarz */

		if(SDL_MUSTLOCK(schirm)) SDL_LockSurface(schirm);

		for(y = 0; y < bild->h * f; y++)
		{
			const Uint8 *q = (const Uint8 *)bild->pixels
			               + (y / f) * bild->pitch;
			Uint8 *z = (Uint8 *)schirm->pixels
			         + (zy + y) * schirm->pitch + zx;

			if(f == 1) { memcpy(z, q, bild->w); continue; }

			for(x = 0; x < bild->w; x++)
			{
				Uint8 c = q[x];
				int k;

				for(k = 0; k < f; k++) z[x * f + k] = c;
			}
		}

		if(SDL_MUSTLOCK(schirm)) SDL_UnlockSurface(schirm);
	}
	else
	{
		/* Echtfarbschirm (Host): KEIN Palettenwechsel noetig. Eine
		 * vergroesserte 8-Bit-Zwischenflaeche mit der Palette des
		 * Bildes, und SDL setzt sie beim Blitten selbst um.
		 *
		 * Dieser Zweig ist nicht nur Beiwerk: ohne ihn liesse sich der
		 * ganze Weg auf dem Host gar nicht pruefen, und geprueft wuerde
		 * erst auf dem Amiga -- also dort, wo ein Fehler teuer ist. */
		SDL_Surface *gross = SDL_CreateRGBSurface(SDL_SWSURFACE,
		                                          bild->w * f,
		                                          bild->h * f,
		                                          8, 0, 0, 0, 0);
		SDL_Rect nach;

		if(!gross) { SDL_FreeSurface(bild); return 0; }

		SDL_SetColors(gross, bild->format->palette->colors, 0,
		              bild->format->palette->ncolors);

		for(y = 0; y < gross->h; y++)
		{
			const Uint8 *q = (const Uint8 *)bild->pixels
			               + (y / f) * bild->pitch;
			Uint8 *z = (Uint8 *)gross->pixels + y * gross->pitch;

			if(f == 1) { memcpy(z, q, bild->w); continue; }

			for(x = 0; x < bild->w; x++)
			{
				Uint8 c = q[x];
				int k;

				for(k = 0; k < f; k++) z[x * f + k] = c;
			}
		}

		SDL_FillRect(schirm, 0, 0);

		nach.x = (Sint16)zx; nach.y = (Sint16)zy;
		nach.w = (Uint16)gross->w; nach.h = (Uint16)gross->h;

		SDL_BlitSurface(gross, 0, schirm, &nach);
		SDL_FreeSurface(gross);
	}

	letzt_f = f;
	letzt_x = zx;
	letzt_y = zy;

	zod_dirty_force_full();
	SDL_Flip(schirm);

	ZLOG("Schirm: %s (%dx%d, Faktor %d)\n", name, bild->w, bild->h, f);

	/* NICHT freigeben -- siehe letzt_bild. */
	letzt_bild = bild;

	return 1;
}

int zod_schirm_breite(const char *text)
{
	const char *p;
	int b = 0;

	if(!text) return 0;

	for(p = text; *p; p++)
		b += glyph_breite((unsigned char)*p) * schrift_skala;

	/* Der Abstand HINTER dem letzten Zeichen gehoert nicht zur Breite
	 * -- sonst sitzt jeder zentrierte Text um ein Halbes daneben. */
	if(b && schrift_info && glyphe((unsigned char)p[-1]))
		b -= schrift_info->abstand * schrift_skala;

	return b;
}

void zod_schirm_text(int x, int y, const char *text)
{
	SDL_Surface *schirm = SDL_GetVideoSurface();
	const char *p;
	int sx = x;

	if(!schirm || !text) return;

	if(SDL_MUSTLOCK(schirm)) SDL_LockSurface(schirm);

	for(p = text; *p; p++)
	{
		SDL_Surface *g = glyphe((unsigned char)*p);
		int gx, gy;

		if(!g) { sx += glyph_breite((unsigned char)*p) * schrift_skala; continue; }

		/* Umsetztabellen beim ersten Glyphen bauen. */
		if(g->format->palette)
		{
			int i;

			if(schrift_info && !vga_gut)
			{
				for(i = 0; i < 256; i++)
				{
					const SDL_Color &c = g->format->palette->colors[i];
					int k, best = 0;
					long best_d = -1;

					for(k = 0; k < 4; k++)
					{
						long dr = satz_farbe[k].r - (long)c.r;
						long dg = satz_farbe[k].g - (long)c.g;
						long db = satz_farbe[k].b - (long)c.b;
						long d = dr * dr + dg * dg + db * db;

						if(best_d < 0 || d < best_d)
						{ best_d = d; best = k; }
					}

					vga[i] = (unsigned char)(best + 1);
				}

				vga_gut = 1;
			}

			if(!schrift_info && !umsetz_gut)
			{
				for(i = 0; i < 256; i++)
				{
					const SDL_Color &c = g->format->palette->colors[i];

					umsetz[i] = (unsigned char)
						naechster_platz(&bild_palette, c.r, c.g, c.b);
				}

				umsetz_gut = 1;
			}
		}

		for(gy = 0; gy < g->h; gy++)
		{
			const Uint8 *q = (const Uint8 *)g->pixels + gy * g->pitch;

			for(gx = 0; gx < g->w; gx++)
			{
				Uint32 farbe;
				int platz;
				int mx, my;

				/* Platz 0 ist durchgehend der Farbschluessel. */
				if(!q[gx]) continue;

				if(schrift_info)
				{
					platz = vga_gut ? vga[q[gx]] : 3;

					/* Gedimmt: die Rampe eine Stufe herunter, der
					 * Schatten bleibt schwarz. */
					if(!schrift_hell && platz >= 2 && platz <= 3)
						platz--;
				}
				else
					platz = umsetz_gut ? umsetz[q[gx]] : q[gx];

				if(schirm->format->BitsPerPixel == 8)
					farbe = (Uint32)platz;
				else
				{
					const SDL_Color &c = bild_farben[platz & 255];

					farbe = SDL_MapRGB(schirm->format, c.r, c.g, c.b);
				}

				/* Ein Glyphenpunkt wird zu skala x skala
				 * Bildpunkten, und jeder davon zu letzt_f x
				 * letzt_f Schirmpunkten. */
				for(my = 0; my < schrift_skala; my++)
					for(mx = 0; mx < schrift_skala; mx++)
						punkt(schirm,
						      letzt_x + (sx + gx * schrift_skala + mx) * letzt_f,
						      letzt_y + (y + gy * schrift_skala + my) * letzt_f,
						      farbe);
			}
		}

		sx += glyph_vor[(unsigned char)*p - 32] * schrift_skala;
	}

	if(SDL_MUSTLOCK(schirm)) SDL_UnlockSurface(schirm);
}

void zod_schirm_putzen(int x, int y, int b, int h)
{
	SDL_Surface *schirm = SDL_GetVideoSurface();
	int gx, gy;

	if(!schirm || !letzt_bild) return;

	if(x < 0) { b += x; x = 0; }
	if(y < 0) { h += y; y = 0; }
	if(x + b > letzt_bild->w) b = letzt_bild->w - x;
	if(y + h > letzt_bild->h) h = letzt_bild->h - y;
	if(b <= 0 || h <= 0) return;

	if(SDL_MUSTLOCK(schirm)) SDL_LockSurface(schirm);

	for(gy = 0; gy < h; gy++)
	{
		const Uint8 *q = (const Uint8 *)letzt_bild->pixels
		               + (y + gy) * letzt_bild->pitch + x;

		for(gx = 0; gx < b; gx++)
		{
			Uint32 farbe;

			if(schirm->format->BitsPerPixel == 8)
				farbe = q[gx];
			else
			{
				const SDL_Color &c = bild_farben[q[gx]];

				farbe = SDL_MapRGB(schirm->format, c.r, c.g, c.b);
			}

			punkt(schirm, letzt_x + (x + gx) * letzt_f,
			      letzt_y + (y + gy) * letzt_f, farbe);
		}
	}

	if(SDL_MUSTLOCK(schirm)) SDL_UnlockSurface(schirm);
}

int zod_schirm_maus(int mx, int my, int *bx, int *by)
{
	int x = (mx - letzt_x) / (letzt_f > 0 ? letzt_f : 1);
	int y = (my - letzt_y) / (letzt_f > 0 ? letzt_f : 1);

	if(bx) *bx = x;
	if(by) *by = y;

	return (x >= 0 && x < 320 && y >= 0 && y < 200) ? 1 : 0;
}

void zod_schirm_zeiger(int an)
{
#ifdef __amigaos__
	zod_schirm_zeiger_amiga(an);
#else
	SDL_ShowCursor(an ? SDL_ENABLE : SDL_DISABLE);
#endif
}

void zod_schirm_ausgeben(void)
{
	SDL_Surface *schirm = SDL_GetVideoSurface();

	if(!schirm) return;

	zod_dirty_force_full();
	SDL_Flip(schirm);
}

void zod_schirm_ende(void)
{
	if(letzt_bild) { SDL_FreeSurface(letzt_bild); letzt_bild = 0; }

	SDL_Surface *schirm = SDL_GetVideoSurface();

	/* Die Glyphen werden nur hier gebraucht -- zwischen zwei
	 * Bildschirmen belegen sie nur Speicher, und auf dem Amiga ist der
	 * knapp. */
	glyphen_weg();
	umsetz_gut = 0;

	if(!steht) return;

	steht = 0;

	/* Schwarz, BEVOR die Palette zurueckgeht: sonst steht das Bild einen
	 * Augenblick in den falschen Farben da. Platz 0 ist in beiden
	 * Paletten schwarz, das Loeschen ist also in jedem Fall richtig. */
	if(schirm) SDL_FillRect(schirm, 0, 0);

#ifdef __amigaos__
	zod_sdl_reload_palette();
#else
	if(schirm && zod_palette_ready())
		SDL_SetColors(schirm, (SDL_Color *)zod_palette_colors(), 0, 256);
#endif

	if(schirm)
	{
		zod_dirty_force_full();
		SDL_Flip(schirm);
	}
}

}
