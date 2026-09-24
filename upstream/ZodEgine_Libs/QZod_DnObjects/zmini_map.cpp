#include "zmini_map.h"
#include "fineclock.h"

ZMiniMap::ZMiniMap()
{
    zmap = nullptr;
    object_list = nullptr;

	render_area.x = 0;
	render_area.y = 0;
	render_area.w = MINIMAP_W_MAX;
	render_area.h = MINIMAP_H_MAX;

	render_ratio = 0;

	show_terrain = false;

	cache_ready = false;
	static_ready = false;
	static_sig = 0;
	last_build = 0;
}

void ZMiniMap::Setup(ZMap *zmap_, vector<ZObject*> *object_list_)
{
	zmap = zmap_;
	object_list = object_list_;
}

void ZMiniMap::Setup_Boundaries()
{
	double map_ratio, max_mini_ratio;

	if(!zmap) return;
	if(!object_list) return;

	//now set our boundaries
	map_ratio = (1.0 * zmap->GetMapBasics().width) / zmap->GetMapBasics().height;
	max_mini_ratio = (1.0 * MINIMAP_W_MAX) / MINIMAP_H_MAX;

	if(map_ratio < max_mini_ratio)
	{
        render_area.w = static_cast<Uint16>(map_ratio * MINIMAP_H_MAX);
		render_area.h = MINIMAP_H_MAX;
		render_area.x = (MINIMAP_W_MAX - render_area.w) >> 1;
		render_area.y = (MINIMAP_H_MAX - render_area.h) >> 1;
	}
	else
	{
		render_area.w = MINIMAP_W_MAX;
        render_area.h = static_cast<Uint16>(MINIMAP_W_MAX / map_ratio);
		render_area.x = (MINIMAP_W_MAX - render_area.w) >> 1;
		render_area.y = (MINIMAP_H_MAX - render_area.h) >> 1;
	}

	render_area.x += 2;
	render_area.y += 2;
	render_area.w -= 4;
	render_area.h -= 4;

//	if(render_area.w < 0) render_area.w = 0;
//	if(render_area.h < 0) render_area.h = 0;

    render_ratio = static_cast<double>(render_area.h) / (zmap->GetMapBasics().height * 16.0);

	//Neue Karte, neue Groesse: die Zwischenflaeche muss weg, sonst behielte sie
	//die Masse der vorigen Karte.
	cache.Unload();
	cache_ready = false;
	static_ready = false;
}

bool ZMiniMap::ClickedMap(int x, int y, int &map_x, int &map_y)
{
	if(x < render_area.x) return false;
	if(x > render_area.x + render_area.w) return false;
	if(y < render_area.y) return false;
	if(y > render_area.y + render_area.h) return false;

	double x_percent = 1.0 * (x - render_area.x) / render_area.w;
	double y_percent = 1.0 * (y - render_area.y) / render_area.h;

    map_x = static_cast<int>(x_percent * (zmap->GetMapBasics().width * 16.0));
    map_y = static_cast<int>(y_percent * (zmap->GetMapBasics().height * 16.0));

	return true;
}

//Die Minikarte zeichnete in JEDEM Bild: Hintergrund, je Wasserkachel eine
//Fuellung, je Zone eine und je Objekt eine -- bei 70 Einheiten rund 75
//Einzelaufrufe fuer eine Flaeche von hoechstens 92x89 Pixeln.
//
//"Nur bei Aenderung" gaebe es hier praktisch nie: in einem Echtzeitspiel
//bewegen sich Einheiten staendig. Der Inhalt faellt deshalb in eine eigene
//Flaeche und wird nur mit fester, niedriger Rate aufgefrischt. Der Sichtrahmen
//bleibt bei jedem Bild, damit das Scrollen unmittelbar wirkt.
#define MINIMAP_REFRESH_SECONDS 0.2

/* Was der Aufbau der Minikarte wirklich kostet.
 *
 * Anlass: Die neue Bildratenanzeige meldete auf dem Host 16 fps, waehrend der
 * Mittelwert bei 207 lag -- es gibt also Bilder von 62 ms. Die Minikarte baut
 * sich 5-mal je Sekunde neu auf und laeuft dabei ueber ALLE Objekte (393 im
 * Mittel) mit je einem eigenen ZSDL_FillRect. Das ist der naechstliegende
 * Verdaechtige, und hier wird er geprueft statt vermutet. */
unsigned long zod_minimap_builds = 0;
unsigned long zod_minimap_ticks  = 0;
unsigned long zod_minimap_worst  = 0;
unsigned long zod_minimap_static = 0;   /* wie oft die unbewegliche Flaeche neu entstand */

extern "C" void zod_minimap_report(unsigned long *builds, unsigned long *ticks,
                                   unsigned long *worst, unsigned long *freq,
                                   unsigned long *statisch)
{
	*builds   = zod_minimap_builds;
	*ticks    = zod_minimap_ticks;
	*worst    = zod_minimap_worst;
	*freq     = zod_fineclock_freq();
	/* Ohne diese Zahl ist die Aufteilung nicht deutbar: Wuerde die
	 * unbewegliche Flaeche in JEDEM Aufbau neu entstehen, saehe die Zeit
	 * genauso aus wie vorher, nur mit einem Durchgang mehr. Dieselbe Lehre
	 * wie bei zeichnen.Objektlisten -- eine Zeitangabe allein sagt nicht,
	 * ob ein Filter greift. */
	*statisch = zod_minimap_static;
}

/* Kennzahl ueber alles, was in der unbeweglichen Flaeche steckt. Absichtlich
 * BILLIG: nur eingebettete Zugriffe (GetObjectID, GetCords, GetOwner), keine
 * Fliesskommarechnung, keine Fuellung. Gegenueber rund 3,5 us je Objekt im
 * vollen Aufbau kostet das je Objekt einige Befehle. */
unsigned long ZMiniMap::StaticSignature()
{
	unsigned long sig = (show_terrain != 0.) ? 1u : 2u;

	sig = sig * 31u + (unsigned long)render_area.w;
	sig = sig * 31u + (unsigned long)render_area.h;

	for(vector<map_zone_info>::iterator i = zmap->GetZoneInfoList().begin();
	    i != zmap->GetZoneInfoList().end(); i++)
		sig = sig * 31u + (unsigned long)i->owner;

	for(vector<ZObject*>::iterator i = object_list->begin(); i != object_list->end(); i++)
	{
		unsigned char ot, oid;
		int x, y;

		(*i)->GetObjectID(ot, oid);
		if(ot != MAP_ITEM_OBJECT) continue;

		(*i)->GetCords(x, y);

		sig = sig * 31u + (unsigned long)(*i)->GetOwner();
		sig = sig * 31u + (unsigned long)x;
		sig = sig * 31u + (unsigned long)y;
	}

	return sig;
}

void ZMiniMap::RebuildStatic()
{
	SDL_Rect to_rect;

	if(!static_cache.GetBaseSurface()) return;

	to_rect.x = 0;
	to_rect.y = 0;
	to_rect.w = render_area.w;
	to_rect.h = render_area.h;

	ZSDL_FillRect(&to_rect, 10, 10, 10, &static_cache);

	//render terrain
	if(show_terrain != 0. )
	{
		for(vector<map_effect_info>::iterator i=zmap->GetMapWaterList().begin(); i!=zmap->GetMapWaterList().end(); ++i)
		{
			SDL_Rect water_rect;
			int ix, iy;

			zmap->GetTile((unsigned int)i->tile, ix, iy);

			water_rect.x = ix;
			water_rect.y = iy;
			water_rect.w = water_rect.h = 1;

			water_rect.x *= render_ratio;
			water_rect.y *= render_ratio;

			ZSDL_FillRect(&water_rect, 0, 0, 250, &static_cache);
		}
	}

	//render zones
	for(vector<map_zone_info>::iterator i=zmap->GetZoneInfoList().begin(); i!=zmap->GetZoneInfoList().end(); i++)
	{
		SDL_Rect zone_rect;

		zone_rect.x = i->x;
		zone_rect.y = i->y;
		zone_rect.w = i->w;
		zone_rect.h = i->h;

		zone_rect.x *= render_ratio;
		zone_rect.y *= render_ratio;
		zone_rect.w *= render_ratio;
		zone_rect.h *= render_ratio;

		zone_rect.x++;
		zone_rect.y++;
		zone_rect.w -= 2;
		zone_rect.h -= 2;

		//do we actually render?
		if(zone_rect.w > 0 && zone_rect.h > 0)
			ZSDL_FillRect(&zone_rect, team_color[i->owner].r * 0.4, team_color[i->owner].g * 0.4, team_color[i->owner].b * 0.4, &static_cache);
	}

	/* Die Kartenobjekte gehoeren in die unbewegliche Flaeche, alles andere
	 * wird je Aufbau gezeichnet. Ab hier laeuft die Schleife deshalb zweimal
	 * ueber dieselbe Liste -- einmal hier mit `statisch`, einmal in
	 * RebuildCache ohne. Der Rumpf ist derselbe, deshalb ein Makro-freier
	 * Umweg ueber ZeichneObjekte(). */
	ZeichneObjekte(true, &static_cache);
}

void ZMiniMap::ZeichneObjekte(bool statisch, ZSDL_Surface *ziel)
{
	for(vector<ZObject*>::iterator i=object_list->begin(); i!=object_list->end(); i++)
	{
		SDL_Rect obj_rect;
		int x, y, w, h;
		unsigned char ot, oid;

		(*i)->GetObjectID(ot, oid);
		if((ot == MAP_ITEM_OBJECT) != statisch) continue;

		(*i)->GetCords(x, y);
		(*i)->GetDimensionsPixel(w, h);

		obj_rect.x = x;
		obj_rect.y = y;
		obj_rect.w = w * 0.8;
		obj_rect.h = h * 0.8;

		obj_rect.x *= render_ratio;
		obj_rect.y *= render_ratio;
		obj_rect.w *= render_ratio;
		obj_rect.h *= render_ratio;

		if(obj_rect.w < 1) obj_rect.w = 1;
		if(obj_rect.h < 1) obj_rect.h = 1;

		int towner;
		towner = (*i)->GetOwner();

		ZSDL_FillRect(&obj_rect, team_color[towner].r, team_color[towner].g , team_color[towner].b, ziel);
	}
}

void ZMiniMap::RebuildCache()
{
	if(!cache.GetBaseSurface()) return;

	/* Unbewegliche Flaeche nur bei echter Aenderung neu aufbauen. */
	{
		const unsigned long sig = StaticSignature();

		SDL_Surface *sb = static_cache.GetBaseSurface();

		/* Groesse kann sich mit der Karte aendern (Setup_Boundaries). */
		if(sb && (sb->w != render_area.w || sb->h != render_area.h))
		{
			static_cache.Unload();
			sb = nullptr;
			static_ready = false;
		}

		if(!static_ready || sig != static_sig)
		{
			if(!sb) static_cache.LoadNewSurface(render_area.w, render_area.h, true);

			RebuildStatic();
			static_sig   = sig;
			static_ready = true;
			zod_minimap_static++;
		}
	}

	static_cache.BlitSurface(&cache, 0, 0);
	ZeichneObjekte(false, &cache);
}

void ZMiniMap::DoRender(SDL_Surface *dest, int x, int y)
{
	SDL_Rect blit_rect;
	int base_x, base_y;

	//we need these to render
	if(!zmap) return;
	if(!object_list) return;

	//Flaeche anlegen, sobald die Groesse feststeht (Setup_Boundaries)
	if(!cache.GetBaseSurface())
	{
		if(render_area.w <= 0 || render_area.h <= 0) return;

		cache.LoadNewSurface(render_area.w, render_area.h, true);
		cache_ready = false;
		static_ready = false;
	}

	double now = current_time();

	if(!cache_ready || (now - last_build) >= MINIMAP_REFRESH_SECONDS)
	{
		/* Zeit um den Aufbau -- der Verdacht lautet, dass dieser 5-Hz-Takt
		 * die langen Bilder verursacht. Gemessen mit der E-Clock, weil
		 * current_time() auf dem Amiga nur 20 ms aufloest und ein Aufbau
		 * darunter liegen koennte. */
		const unsigned long t0 = zod_fineclock_ticks();

		RebuildCache();

		{
			const unsigned long dt = zod_fineclock_ticks() - t0;

			zod_minimap_builds++;
			zod_minimap_ticks += dt;

			if(dt > zod_minimap_worst) zod_minimap_worst = dt;
		}

		cache_ready = true;
		last_build = now;
	}

	base_x = render_area.x + x;
	base_y = render_area.y + y;

	//Achtung: SDL_BlitSurface veraendert das Zielrechteck, deshalb eine eigene
	//Kopie -- der Sichtrahmen unten braucht die urspruenglichen Werte.
	blit_rect.x = static_cast<short>(base_x);
	blit_rect.y = static_cast<short>(base_y);
	blit_rect.w = render_area.w;
	blit_rect.h = render_area.h;

	cache.BlitSurface(nullptr, &blit_rect);

	//draw view area -- jedes Bild, damit Scrollen unmittelbar wirkt
	{
		int mshift_x, mshift_y, mview_w, mview_h;
		SDL_Color the_color;

		zmap->GetViewShiftFull(mshift_x, mshift_y, mview_w, mview_h);

		/* Ist die Karte schmaler als der Ausschnitt, ist die Verschiebung
		 * NEGATIV (die Karte wird zentriert, links und rechts bleibt ein
		 * schwarzer Rand). Der Rahmen zeigt aber, welcher Teil der KARTE zu
		 * sehen ist -- der schwarze Rand gehoert nicht dazu. Ohne diese
		 * Klemme liefe er nach links aus der Minikarte heraus: draw_box
		 * klemmt nur gegen max_x/max_y, nicht gegen null. */
		{
			const int karte_w = zmap->GetMapBasics().width  * 16;
			const int karte_h = zmap->GetMapBasics().height * 16;

			if(mshift_x < 0) { mview_w += mshift_x; mshift_x = 0; }
			if(mshift_y < 0) { mview_h += mshift_y; mshift_y = 0; }
			if(mshift_x + mview_w > karte_w) mview_w = karte_w - mshift_x;
			if(mshift_y + mview_h > karte_h) mview_h = karte_h - mshift_y;
		}

		SDL_Rect view_box;
		view_box.x = mshift_x;
		view_box.y = mshift_y;
		view_box.w = mview_w;
		view_box.h = mview_h;

		view_box.x *= render_ratio;
		view_box.y *= render_ratio;
		view_box.w *= render_ratio;
		view_box.h *= render_ratio;

		view_box.x += base_x;
		view_box.y += base_y;

		the_color.r = 200;
		the_color.g = 200;
		the_color.b = 0;

		draw_box(dest, view_box, the_color, base_x + render_area.w, base_y + render_area.h);
	}
}
