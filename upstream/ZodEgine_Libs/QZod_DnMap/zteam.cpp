#include "zteam.h"

/* Ein Pixel unabhaengig von der Farbtiefe lesen.
 *
 * Die alten Fassungen lasen hier fest *(Uint32*) -- seit die Flaechen 8 Bit
 * haben, liest das drei Byte zu viel und am letzten Pixel ueber die Belegung
 * hinaus. Genau das war Guru 81000005 ("Speicherliste beschaedigt") beim
 * ersten Start der eigenen Grafikschicht. */
static Uint32 zod_read_pixel(SDL_Surface *s, int x, int y)
{
	const Uint8 *p = (const Uint8*)s->pixels + (size_t)y * s->pitch
	                 + (size_t)x * s->format->BytesPerPixel;

	switch(s->format->BytesPerPixel)
	{
	case 1: return *p;
	case 2: return *(const Uint16*)p;
	case 3:
#if defined(__amigaos__) || defined(__BIG_ENDIAN__)
		return ((Uint32)p[0] << 16) | ((Uint32)p[1] << 8) | p[2];
#else
		return p[0] | ((Uint32)p[1] << 8) | ((Uint32)p[2] << 16);
#endif
	default: return *(const Uint32*)p;
	}
}

#include <zod_palette.h>


ZTeam_Palette ZTeam::team_palette[MAX_TEAM_TYPES];

SDL_Color team_color[MAX_TEAM_TYPES];

bool ZTeam_Palette::LoadSurfacePalette(SDL_Surface *src)
{
	//dimensions ok?
	if(src->w != 2)
	{
		ZLOG("ZTeam_Palette::LoadSurfacePalette:palette width not 2\n");
		return false;
	}

	if(src->h != ZTEAM_PALETTE_MAX)
	{
		ZLOG("ZTeam_Palette::LoadSurfacePalette:palette height not %d\n", ZTEAM_PALETTE_MAX);
	}

	//clear out
	//base_color.clear();
	//replace_color.clear();

	//load in
	for(int j=0;j<src->h;j++)
	{
		Uint8 r, g, b, a;
		Uint32 pixel;
		SDL_Color nc;

		//base color
		{
			pixel = zod_read_pixel(src, 0, j);
			SDL_GetRGBA(pixel, src->format, &r, &g, &b, &a);

			nc.r = r;
			nc.g = g;
			nc.b = b;

			//base_color.push_back(nc);
			if(j<ZTEAM_PALETTE_MAX) base_color[j] = nc;
		}

		//replace color
		{
			pixel = zod_read_pixel(src, 1, j);
			SDL_GetRGBA(pixel, src->format, &r, &g, &b, &a);

			nc.r = r;
			nc.g = g;
			nc.b = b;

			//replace_color.push_back(nc);
			if(j<ZTEAM_PALETTE_MAX) replace_color[j] = nc;
		}
	}
	
	return true;
}

bool ZTeam_Palette::SaveSurfacePalette(string filename)
{
    ZLOG("ZTeam_Palette::SaveSurfacePalette: "
           "this function requires color arrays be vectors\n");
	return false;
	/*
	SDL_Surface *src;

	if(!base_color.size())
	{
		ZLOG("ZTeam_Palette::SaveSurfacePalette:palette not loaded to save to '%s'\n", filename.c_str());
		return false;
	}

	src = ZSDL_NewSurface(2, base_color.size());

	if(!src)
	{
		ZLOG("ZTeam_Palette::SaveSurfacePalette:could not make surface to save to '%s'\n", filename.c_str());
		return false;
	}

	//load surface
	for(int i=0;i<src->h;i++)
	{
		SDL_Rect point_rect;
		SDL_Color c;

		//base color
		{
			c = base_color[i];

			point_rect.x = 0;
			point_rect.y = i;
			point_rect.w = 1;
			point_rect.h = 1;

			SDL_FillRect(src, &point_rect, SDL_MapRGB(src->format, c.r, c.g, c.b));
		}

		//replace color
		{
			c = replace_color[i];

			point_rect.x = 1;
			point_rect.y = i;
			point_rect.w = 1;
			point_rect.h = 1;

			SDL_FillRect(src, &point_rect, SDL_MapRGB(src->format, c.r, c.g, c.b));
		}
	}

	//save surface
	SDL_SaveBMP(src, filename.c_str());

	//free surface
	SDL_FreeSurface(src);

	//good
	return true;
	*/
}

bool ZTeam_Palette::AddColor(SDL_Color &bc, SDL_Color &rc)
{
	ZLOG("ZTeam_Palette::AddColor: this function requires color arrays be vectors\n");
	return false;
	/*
	//this color already in the list?
	for(vector<SDL_Color>::iterator i=base_color.begin(); i!=base_color.end(); i++)
		if(bc.r == i->r && bc.g == i->g && bc.b == i->b)
			return false;

	//add it
	base_color.push_back(bc);
	replace_color.push_back(rc);

	return true;
	*/
}

bool ZTeam_Palette::GetReplacement(Uint8 &r, Uint8 &g, Uint8 &b, SDL_Color &c)
{
	for(int i=0;i<ZTEAM_PALETTE_MAX;i++)
	{
		SDL_Color &bi = base_color[i];

		if(r == bi.r && g == bi.g && b == bi.b)
		{
			SDL_Color &ri = replace_color[i];

			c.r = ri.r;
			c.g = ri.g;
			c.b = ri.b;

			return true;
		}
	}

	return false;

	/*
	vector<SDL_Color>::iterator bi;
	vector<SDL_Color>::iterator ri;

	if(!r && !g && !b) return false;

	bi=base_color.begin();
	ri=replace_color.begin();

	for(; bi!=base_color.end(); bi++, ri++)
		if(r == bi->r && g == bi->g && b == bi->b)
		{
			c.r = ri->r;
			c.g = ri->g;
			c.b = ri->b;

			return true;
		}

	return false;
	*/
}

/* WIE GetReplacement, ABER MIT TOLERANZ -- und das ist kein Weichspueler,
 * sondern die Reparatur eines echten Fehlers (23.09.).
 *
 * Vom Nutzer gemeldet: "3 Bots aktiviert ... auf der minimap wird nur die
 * Farbe des blauen Teams und meins (rot) angezeigt. Die anderen erscheinen
 * als neutral (grau)."
 *
 * Die Teamfarben werden aus assets/teams/<team>_palette.bmp abgeleitet --
 * einer 2x16-Tabelle: Spalte 0 die rote Grundrampe, Spalte 1 die Farben
 * dieses Teams. Gesucht wird der Platz (223,0,0).
 *
 * Im Archiv liegen diese Dateien aber als RGB565 (tools/assets/pack.py waehlt
 * das fuer alles, was nicht schon indiziert ist), und 5 Bit je Kanal machen
 * aus 223 eine 222. Der EXAKTE Vergleich in GetReplacement findet den Platz
 * also nie, und jedes Team faellt auf das Grau aus Setup_team_color zurueck.
 * Rot und Blau sahen richtig aus, weil sie dort fest eingetragen sind --
 * genau die zwei, die der Nutzer gesehen hat.
 *
 * Die Toleranz ist nicht geraten: RGB565 kann je Kanal hoechstens um 4
 * danebenliegen, der schlimmste Abstand ist also 3*16 = 48. Der naechste
 * ANDERE Eintrag der Rampe ist (199,0,0), Abstand 576. Mit 8 als Grenze
 * (64) ist die Zuordnung damit eindeutig, und eine gar nicht geladene
 * Palette (alles 0) faellt mit 49729 sicher durch.
 *
 * Auf dem Host faellt es nicht auf: dort liest IMG_Load die BMP direkt von
 * der Platte, mit exakten Farben. Nur auf dem Amiga kommen sie aus dem
 * Archiv. */
bool ZTeam_Palette::GetReplacementNear(Uint8 r, Uint8 g, Uint8 b,
                                       SDL_Color &c, int max_abstand)
{
	int  best = -1;
	long best_d = 0;
	int  i;

	for(i = 0; i < ZTEAM_PALETTE_MAX; i++)
	{
		long dr = (long)base_color[i].r - (long)r;
		long dg = (long)base_color[i].g - (long)g;
		long db = (long)base_color[i].b - (long)b;
		long d  = dr * dr + dg * dg + db * db;

		if(best < 0 || d < best_d) { best_d = d; best = i; }
	}

	if(best < 0 || best_d > (long)max_abstand * (long)max_abstand)
		return false;

	c = replace_color[best];

	return true;
}

void ZTeam::Init()
{
	int i;

	for(i=0;i<MAX_TEAM_TYPES;i++)
		LoadPalette(i);

	Setup_team_color();
}

/*
{
	{115, 115, 115, 0},
	{233, 0, 0, 0},
	{19, 55, 251, 0},
	{23, 143, 19, 0},
	{203, 99, 47, 0}
};*/

void ZTeam::Setup_team_color()
{
	int i;
	int abgeleitet = 0;

	for(i=0;i<MAX_TEAM_TYPES;i++)
	{
		team_color[i].r = 115;
		team_color[i].g = 115;
		team_color[i].b = 115;
	}

	team_color[NULL_TEAM].r = 115;
	team_color[NULL_TEAM].g = 115;
	team_color[NULL_TEAM].b = 115;

	/* Rot ist die GRUNDFARBE -- es gibt kein red_palette.bmp, aus dem sich
	 * etwas ableiten liesse. Blau bleibt ebenfalls fest, damit die Minikarte
	 * genau so aussieht wie bisher. */
	team_color[RED_TEAM].r = 223;
	team_color[RED_TEAM].g = 0;
	team_color[RED_TEAM].b = 0;

	team_color[BLUE_TEAM].r = 19;
	team_color[BLUE_TEAM].g = 55;
	team_color[BLUE_TEAM].b = 251;

	/* HIER STANDEN ZWEI BLOECKE HINTER #ifdef GREEN_TEAM und
	 * #ifdef YELLOW_TEAM. Beide waren TOT: GREEN_TEAM und YELLOW_TEAM sind
	 * Werte einer Aufzaehlung (constants.h), keine Makros -- #ifdef darauf
	 * ist immer falsch. Gruen und Gelb bekamen ihre Farbe also nie von
	 * dort, und weil die Ableitung darunter ebenfalls scheiterte (siehe
	 * GetReplacementNear), blieben sie grau.
	 *
	 * Entfernt statt repariert: die Ableitung aus der Palettendatei ist die
	 * eine Quelle, und zwei Quellen fuer dieselbe Farbe laufen frueher oder
	 * spaeter auseinander. */

	for(i=BLUE_TEAM+1;i<MAX_TEAM_TYPES;i++)
	{
		SDL_Color c;

		if(team_palette[i].GetReplacementNear(223, 0, 0, c, 8))
		{
			team_color[i] = c;
			abgeleitet++;
		}
		else
			ZLOG("ZTeam: keine Farbe fuer das %s-Team -- bleibt neutral "
			     "(fehlt assets/teams/%s_palette.bmp?)\n",
			     team_type_string[i].c_str(), team_type_string[i].c_str());
	}

	/* Die Stueckzahl steht bewusst da: ohne sie waere "alle grau" von
	 * "keine weiteren Teams vorgesehen" nicht zu unterscheiden -- und genau
	 * so ist der Fehler monatelang unbemerkt geblieben. */
	ZLOG("Teamfarben: %d von %d abgeleitet\n",
	     abgeleitet, (int)MAX_TEAM_TYPES - (BLUE_TEAM + 1));
}

void ZTeam::LoadPalette(int team)
{
	string filename;
	SDL_Surface *surface;

	if(team == ZTEAM_BASE_TEAM) return;

	filename = "assets/teams/" + team_type_string[team] + "_palette.bmp";
	surface = IMG_Load(filename.c_str());

	if(!surface)
	{
		ZLOG("ZTeam::Could not load palette for the %s team:'%s'\n", team_type_string[team].c_str(), filename.c_str());
		return;
	}

	//convert it to 32 bits
	surface = ZSDL_ConvertImage(surface);

	//load it and free it
	team_palette[team].LoadSurfacePalette(surface);
	SDL_FreeSurface(surface);
}

void ZTeam::SavePalette(int team)
{
	if(team == ZTEAM_BASE_TEAM)
	{
		ZLOG("ZTeam::SavePalette:You can not save the base palette (%s)\n", team_type_string[ZTEAM_BASE_TEAM].c_str());
		return;
	}

	string filename;

	filename = "assets/teams/" + team_type_string[team] + "_palette.bmp";

	team_palette[team].SaveSurfacePalette(filename);

	//SDL_SaveBMP(team_palette[team], filename.c_str());
}

void ZTeam::SaveAllPalettes()
{
	int i;

	for(i=0;i<MAX_TEAM_TYPES;i++)
		SavePalette(i);
}

void ZTeam::AppendPalette(int team, SDL_Surface *bv, SDL_Surface *rv)
{
	if(team == ZTEAM_BASE_TEAM)
	{
		ZLOG("ZTeam::AppendPalette:You can not append to the base palette (%s)\n", team_type_string[ZTEAM_BASE_TEAM].c_str());
		return;
	}

	//surfaces ok?
	if(!bv || !rv) return;

	if(!bv->w || !bv->h) return;
	if(!rv->w || !rv->h) return;

	if(bv->w != rv->w) return;
	if(bv->h != rv->h) return;

	//get list of differences
	int i, j;

	for(i=0;i<bv->w;i++)
		for(j=0;j<bv->h;j++)
		{
			Uint8 br, bg, bb, ba;
			Uint8 rr, rg, rb, ra;
			Uint32 pixel;

			pixel = zod_read_pixel(bv, i, j);
			SDL_GetRGBA(pixel, bv->format, &br, &bg, &bb, &ba);

			pixel = zod_read_pixel(rv, i, j);
			SDL_GetRGBA(pixel, rv->format, &rr, &rg, &rb, &ra);

			//add to the list?
			if(br != rr || bg != rg || bb != rb)
			{
				SDL_Color bc, rc;

				bc.r = br;
				bc.g = bg;
				bc.b = bb;

				rc.r = rr;
				rc.g = rg;
				rc.b = rb;

				team_palette[team].AddColor(bc, rc);
			}
		}
}

//#define COLLECT_TEAM_COLORS

void ZTeam::LoadZSurface(int team, ZSDL_Surface &bv, ZSDL_Surface &rv, string filename)
{
#ifdef USE_TEAM_COLORS
	#ifdef COLLECT_TEAM_COLORS
			rv.LoadBaseImage(filename);
			if(team != NULL_TEAM && team != ZTEAM_BASE_TEAM)
				ZTeam::AppendPalette(team, bv.GetBaseSurface(), rv.GetBaseSurface());
	#else
			if(team == NULL_TEAM || team == ZTEAM_BASE_TEAM) rv.LoadBaseImage(filename);
			else rv.LoadBaseImage(ZTeam::Make(team, bv.GetBaseSurface())); 
	#endif
#else
	rv.LoadBaseImage(filename);
#endif
}

SDL_Surface *ZTeam::Make(int team, SDL_Surface *base_version)
{
	if(team == ZTEAM_BASE_TEAM)
	{
		ZLOG("ZTeam::Make:You can not make from the base palette (%s)\n", team_type_string[ZTEAM_BASE_TEAM].c_str());
		return nullptr;
	}

	if(!base_version) return nullptr;

	/* 8-Bit-Pfad: Die Teamfarbe ist ein Tausch von Palettenplaetzen, kein
	 * Vergleich von Farbwerten. tools/assets/palette.py legt je Team eine
	 * Tabelle Index->Index an; hier wird sie nur angewendet.
	 *
	 * Die alte Fassung suchte jeden Pixel in der Teampalette und ersetzte ihn
	 * per SDL_FillRect (1x1). Sie brauchte exakte RGB-Treffer -- und genau
	 * daran scheiterte sie, sobald die Flaechen nicht mehr 32 Bit hatten:
	 * auf der V2 blieb der Gegner rot, weil RGB565 die Werte rundet. */
	if(base_version->format->BytesPerPixel == 1 && zod_palette_ready())
	{
		const unsigned char *xlat = zod_palette_xlat(team);

		SDL_Surface *conv = SDL_ConvertSurface(base_version, base_version->format,
		                                       base_version->flags);

		if(!conv) return nullptr;

		if(base_version->flags & SDL_SRCCOLORKEY)
			SDL_SetColorKey(conv, SDL_SRCCOLORKEY, base_version->format->colorkey);

		if(SDL_MUSTLOCK(conv) && SDL_LockSurface(conv) < 0) return conv;

		for(int j = 0; j < conv->h; j++)
		{
			unsigned char *row = (unsigned char*)conv->pixels + j * conv->pitch;

			for(int i = 0; i < conv->w; i++)
				row[i] = xlat[row[i]];
		}

		if(SDL_MUSTLOCK(conv)) SDL_UnlockSurface(conv);

		return conv;
	}

	SDL_Surface *nw;

	nw = CopyImage(base_version);

	if(!nw) return nullptr;


	for(int j=0;j<nw->h;j++)
		for(int i=0;i<nw->w;i++)
		{
			Uint8 r, g, b, a;
			Uint32 pixel;
			SDL_Color c;

			pixel = zod_read_pixel(nw, i, j);
			SDL_GetRGBA(pixel, nw->format, &r, &g, &b, &a);

			//bypass invisible
			if(!a) continue;

			if(team_palette[team].GetReplacement(r,g,b,c))
			{
				SDL_Rect point_rect;

				point_rect.x = i;
				point_rect.y = j;
				point_rect.w = 1;
				point_rect.h = 1;

				SDL_FillRect(nw, &point_rect, SDL_MapRGB(nw->format, c.r, c.g, c.b));
			}
		}

	return nw;
}
