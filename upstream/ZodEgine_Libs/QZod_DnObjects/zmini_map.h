#ifndef ZMINIMAP_H
#define ZMINIMAP_H

#include "qzod_dnobjects_global.h"
#include "zobject.h"



#define MINIMAP_W_MAX (647 - 555)
#define MINIMAP_H_MAX (388 - 299)

class QZOD_DNOBJECTSSHARED_EXPORT ZMiniMap
{
public:
	ZMiniMap();

	void Setup(ZMap *zmap_, vector<ZObject*> *object_list_);
	void Setup_Boundaries();
	void DoRender(SDL_Surface *dest, int x, int y);
	bool ClickedMap(int x, int y, int &map_x, int &map_y);
	void SetShowTerrain(bool show_terrain_) { show_terrain = show_terrain_; }
	void ToggleShowTerrain() { show_terrain = !show_terrain; }
private:
	//Inhalt der Minikarte in eine eigene Flaeche zeichnen und nur mit fester,
	//niedriger Rate auffrischen. Ohne das lief je Objekt eine Einzelfuellung
	//in JEDEM Bild.
	void RebuildCache();

	/* Der UNBEWEGLICHE Teil der Minikarte: Untergrund, Wasser, Zonen und die
	 * Kartenobjekte (Steine, Kakteen). Auf der gemessenen Karte sind das 1272
	 * von 1557 Objekten -- sie stehen fest und wechseln nie den Besitzer.
	 * Der Aufbau kostete sie trotzdem in jedem der 5 Durchgaenge je Sekunde.
	 *
	 * Ungueltig wird die Flaeche ueber eine Kennzahl (StaticSignature), nicht
	 * ueber Haken an den Setzern -- dieselbe Entscheidung wie bei
	 * ZMap::BakeZoneMarkers: es gibt zu viele Stellen, die etwas aendern
	 * koennen, und ein Vergleich je Aufbau kann keine uebersehen. */
	void RebuildStatic();
	unsigned long StaticSignature();
	//Ein Rumpf fuer beide Durchgaenge: `statisch` waehlt die Kartenobjekte
	//aus (true) oder alles uebrige (false).
	void ZeichneObjekte(bool statisch, ZSDL_Surface *ziel);

	ZSDL_Surface static_cache;
	bool static_ready;
	unsigned long static_sig;

	ZMap *zmap;
	vector<ZObject*> *object_list;

	SDL_Rect render_area;
	double render_ratio;

	double show_terrain;

	//genau so gross wie render_area, damit der Blit nur den Bereich ueberdeckt,
	//den die Minikarte wirklich bemalt (die Raender gehoeren dem HUD)
	ZSDL_Surface cache;
	bool cache_ready;
	double last_build;
};

#endif
