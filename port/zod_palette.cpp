/*
 * Laedt palette.zpl (Format ZPL2, siehe tools/assets/palette.py).
 *
 *   "ZPL2"
 *   u16 Planeten, u16 Plaetze je Bank, u16 Teams, u16 Rampenlaenge
 *   256*3 Byte   Grundpalette (Platz 0 = Farbschluessel)
 *   je Planet:   u16 Anzahl, dann je Eintrag u8 Platz + u8 r + u8 g + u8 b
 *   je Team:     256 Byte Umsetztabelle
 *
 * Alles Big-Endian, also in Amiga-Reihenfolge -- gelesen wird byteweise, damit
 * es auf beiden Seiten gleich funktioniert.
 */
#include "zod_palette.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "zod_log.h"

#define ZOD_PAL_MAX_PLANETS 8
#define ZOD_PAL_MAX_TEAMS   16

namespace {

struct BankEntry
{
	unsigned char slot;
	unsigned char r, g, b;
};

bool loaded = false;

SDL_Color base[256];        /* wie geladen */
SDL_Color current[256];     /* Grundpalette plus Bank des Planeten */
SDL_Color planet_colors[ZOD_PAL_MAX_PLANETS][256];

/* Reihenfolge wie in tools/assets/palette.py */
/* Reihenfolge wie planet_type in constants.h (DESERT, VOLCANIC, ARCTIC,
 * JUNGLE, CITY) -- palette.py legt die Baenke in derselben Folge ab. */
const char *planet_names[] = { "desert", "volcanic", "arctic", "jungle", "city" };

int planets = 0;
int teams = 0;
int cur_planet = -1;

size_t bank_count[ZOD_PAL_MAX_PLANETS];
BankEntry bank[ZOD_PAL_MAX_PLANETS][256];

unsigned char xlat[ZOD_PAL_MAX_TEAMS][256];
unsigned char identity[256];

unsigned rd_be16(const unsigned char *p)
{
	return ((unsigned)p[0] << 8) | p[1];
}

} /* namespace */

int zod_palette_load(const char *dir)
{
	char path[512];

	if(!dir || !*dir) dir = "packs";

	snprintf(path, sizeof(path), "%s/palette.zpl", dir);

	FILE *fp = fopen(path, "rb");

	if(!fp)
	{
		/* Kein Fehler: ohne Palettendatei laeuft alles wie bisher. */
		ZLOG("Palette: %s nicht vorhanden -- 8-Bit-Pfad bleibt aus\n", path);
		return 0;
	}

	unsigned char head[12];

	if(fread(head, 1, sizeof(head), fp) != sizeof(head) ||
	   memcmp(head, "ZPL2", 4) != 0)
	{
		ZLOG("Palette: %s hat keinen ZPL2-Kopf\n", path);
		fclose(fp);
		return 0;
	}

	planets = (int)rd_be16(head + 4);
	teams   = (int)rd_be16(head + 8);

	if(planets > ZOD_PAL_MAX_PLANETS) planets = ZOD_PAL_MAX_PLANETS;
	if(teams > ZOD_PAL_MAX_TEAMS) teams = ZOD_PAL_MAX_TEAMS;

	unsigned char rgb[256 * 3];

	if(fread(rgb, 1, sizeof(rgb), fp) != sizeof(rgb))
	{
		ZLOG("Palette: %s zu kurz (Grundpalette)\n", path);
		fclose(fp);
		return 0;
	}

	for(int i = 0; i < 256; i++)
	{
		base[i].r = rgb[i * 3];
		base[i].g = rgb[i * 3 + 1];
		base[i].b = rgb[i * 3 + 2];
		base[i].unused = 0;
	}

	for(int p = 0; p < planets; p++)
	{
		unsigned char cnt[2];

		if(fread(cnt, 1, 2, fp) != 2)
		{
			ZLOG("Palette: %s zu kurz (Bank %d)\n", path, p);
			fclose(fp);
			return 0;
		}

		unsigned n = rd_be16(cnt);

		if(n > 256) n = 256;

		bank_count[p] = n;

		for(unsigned i = 0; i < n; i++)
		{
			unsigned char e[4];

			if(fread(e, 1, 4, fp) != 4)
			{
				ZLOG("Palette: %s zu kurz (Bank %d, Eintrag %u)\n", path, p, i);
				fclose(fp);
				return 0;
			}

			bank[p][i].slot = e[0];
			bank[p][i].r = e[1];
			bank[p][i].g = e[2];
			bank[p][i].b = e[3];
		}
	}

	for(int t = 0; t < teams; t++)
		if(fread(xlat[t], 1, 256, fp) != 256)
		{
			ZLOG("Palette: %s zu kurz (Team %d)\n", path, t);
			fclose(fp);
			return 0;
		}

	fclose(fp);

	for(int i = 0; i < 256; i++) identity[i] = (unsigned char)i;

	/* Je Planet eine fertige Palette: Grundpalette plus Bank. */
	for(int p = 0; p < planets; p++)
	{
		memcpy(planet_colors[p], base, sizeof(base));

		for(size_t i = 0; i < bank_count[p]; i++)
		{
			const BankEntry &e = bank[p][i];

			planet_colors[p][e.slot].r = e.r;
			planet_colors[p][e.slot].g = e.g;
			planet_colors[p][e.slot].b = e.b;
		}
	}

	memcpy(current, base, sizeof(current));
	cur_planet = -1;
	loaded = true;

	ZLOG("Palette: %s geladen, %d Planeten, %d Teams\n", path, planets, teams);

	return 1;
}

int zod_palette_ready(void)
{
	return loaded ? 1 : 0;
}

const SDL_Color *zod_palette_colors(void)
{
	return current;
}

const SDL_Color *zod_palette_colors_for(const char *name)
{
	if(!loaded) return 0;
	if(!name) return base;

	for(int p = 0; p < planets && p < (int)(sizeof(planet_names) / sizeof(planet_names[0])); p++)
		if(strstr(name, planet_names[p]))
			return planet_colors[p];

	return base;
}

int zod_palette_set_planet(int planet)
{
	if(!loaded) return 0;
	if(planet < 0 || planet >= planets) return 0;
	if(planet == cur_planet) return 0;

	memcpy(current, base, sizeof(current));

	for(size_t i = 0; i < bank_count[planet]; i++)
	{
		const BankEntry &e = bank[planet][i];

		current[e.slot].r = e.r;
		current[e.slot].g = e.g;
		current[e.slot].b = e.b;
	}

	cur_planet = planet;

	return 1;
}

const unsigned char *zod_palette_xlat(int team)
{
	if(!loaded || team <= 0 || team >= teams) return identity;

	return xlat[team];
}

int zod_palette_teams(void)
{
	return loaded ? teams : 0;
}
