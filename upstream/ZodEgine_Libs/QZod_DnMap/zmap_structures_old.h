#ifndef ZMAP_STRUCTURES_OLD_H
#define ZMAP_STRUCTURES_OLD_H
// =========================================================
// *********************************************************
// =========================================================

#include "qzod_dnmap_global.h"

#include <lib_qZod_DnSeparate/constants.h>
#include <lib_qZod_DnSeparate/zsdl.h>
#include <lib_qZod_DnSeparate/zsdl_opengl.h>

//#include <constants.h>
//#include <zsdl.h>
//#include <zsdl_opengl.h>

// =========================================================
// *********************************************************
// =========================================================
#define MAX_SHIFT_CLICK 1
#define SHIFT_CLICK_S 320
#define SHIFT_CLICK_STREAM 0.1


enum QZOD_DNMAPSHARED_EXPORT map_object_type
{
    ROCK_OBJECT,
    BRIDGE_OBJECT,
    BUILDING_OBJECT,
    CANNON_OBJECT,
    VEHICLE_OBJECT,
    ROBOT_OBJECT,
    ANIMAL_OBJECT,
    MAP_ITEM_OBJECT,
    MAX_MAP_OBJECT_TYPES
};



extern const string map_object_type_string[MAX_MAP_OBJECT_TYPES];

//ensure the follow has a uniform size
#pragma pack(1)

struct QZOD_DNMAPSHARED_EXPORT palette_tile_info
{
    bool is_water;
    bool is_passable;
    bool is_usable;
    bool is_road;
    bool is_effect;
    bool is_water_effect; //does it happen randomly within a water tile?
    unsigned short next_tile_in_effect;
    bool takes_tank_tracks;
    short crater_type;

    //items for the map editor
    bool is_starter_tile;
};

struct QZOD_DNMAPSHARED_EXPORT palette_tile_info_new
{
    bool is_water;
    bool is_passable;
    bool is_usable;
    bool is_road;
    bool is_effect;
    bool is_water_effect; //does it happen randomly within a water tile?
    unsigned short next_tile_in_effect;
    bool takes_tank_tracks;
    short crater_type;

    //items for the map editor
    bool is_starter_tile;
};

//undo the uniform size command
#pragma pack()

class QZOD_DNMAPSHARED_EXPORT map_zone_info_tile
{
    public:
        map_zone_info_tile(int x=0, int y=0, bool is_water_ = false)
        {
            render_loc.x = x;
            render_loc.y = y;
            render_loc.w = 0;
            render_loc.h = 0;
            is_water = is_water_;
            bob_i = rand() % 2;
            next_time=0;
        }

        SDL_Rect render_loc;
        bool is_water;
        int bob_i;
        double next_time;
};

struct QZOD_DNMAPSHARED_EXPORT map_zone_info
{
    /* Ausdruecklicher Konstruktor, weil frischer Heap auf dem Amiga NICHT
     * genullt ist (AllocMem(size, 0)). Ein nicht gesetztes `baked_owner`
     * koennte dort zufaellig dem echten Besitzer gleichen -- dann wuerden die
     * Marker nie eingebacken, und zwar nur auf dem Amiga und nur manchmal. */
    map_zone_info()
        : owner(NULL_TEAM), x(0), y(0), w(0), h(0), id(0),
          baked_owner(-2), baked_w(0), baked_h(0), has_water(false) {}

    team_type owner;
    vector<map_zone_info_tile> tile;
    int x, y, w, h;
    int id;

    /* Welcher Besitzer steckt gerade in der Kartenflaeche (full_render)?
     * -2 heisst "noch nichts eingebacken". Siehe ZMap::BakeZoneMarkers. */
    int baked_owner;
    int baked_w, baked_h;   /* GEZEICHNETER Bereich des eingebackenen Markers */

    /* Hat diese Zone ueberhaupt Wasserkacheln? Nur die muessen je Bild
     * gezeichnet werden (sie wippen); alles andere steckt seit
     * BakeZoneMarkers in der Kartenflaeche. Ohne diesen Merker liefe die
     * innere Schleife samt GetBlitInfo je Bild ueber ALLE Umrisskacheln,
     * nur um festzustellen, dass nichts zu tun ist -- auf der V1200 sind das
     * 458 Kacheln und 668 us je Bild. Die meisten Karten haben gar kein
     * Wasser am Zonenrand. */
    bool has_water;
};

class QZOD_DNMAPSHARED_EXPORT map_effect_info
{
public:
    map_effect_info();
    map_effect_info(unsigned int tile_) {tile = tile_; next_effect_time = 0;};
    unsigned int tile;
    double next_effect_time;
};

class QZOD_DNMAPSHARED_EXPORT map_basics
{
public:
    map_basics() { clear(); }

    void clear()
    {
        width=0;
        height=0;
        map_name[0] = 0;
        player_count = 0;
        object_count = 0;
        terrain_type = 0;
        zone_count = 0;
        pad_align_ = 0;
    }

    unsigned short width;
    unsigned short height;
    char map_name[50];
    unsigned char player_count;
    unsigned short object_count;
    unsigned char terrain_type;
    unsigned short zone_count;

    /* Fuellfeld, NICHT Teil des Dateiformats. Es haelt sizeof(map_basics)
     * durch 4 teilbar.
     *
     * Ohne das ist die Struktur 62 Byte gross (2 modulo 4). In ZMap folgt
     * unmittelbar der Vektor zone_list, der damit auf einem nur 2-fach
     * ausgerichteten Versatz beginnt. gcc 6.5.0b legt dann beim Nullen der
     * Mitglieder die Setzungen zu einer 6-Byte-Gruppe zusammen und gibt davon
     * nur ein Langwort aus -- die uebrigen zwei Byte, ausgerechnet das
     * Tiefwort von zone_list._M_start, bleiben UNBESCHRIEBEN.
     *
     * Folge: der Vektor meldet eine riesige Kapazitaet, reserve() belegt
     * nichts, und push_back schreibt durch eine Unsinn-Adresse mitten in die
     * AmigaOS-Systemstrukturen -- ohne MMU still, mit Absturz irgendwann
     * spaeter und anderswo. Belegt per Disassemblat und eigenstaendigem
     * Minimalbeispiel; fehlerfrei nur bei -O0.
     *
     * Die Dateigroesse steckt unabhaengig davon in ZOD_MAPFILE_BASICS_SIZE (62),
     * und gelesen wird feldweise ueber feste Versaetze: Karten und Drahtformat
     * bleiben unveraendert. */
    unsigned short pad_align_;
};

/* Rueckfallsicherung: bricht den Bau, falls die Groesse je wieder 2 modulo 4
 * wird, statt still falschen Code erzeugen zu lassen. */
static_assert(sizeof(map_basics) % 4 == 0,
              "map_basics muss durch 4 teilbar sein, sonst laesst gcc 6.5.0b "
              "beim Nullen der ZMap-Mitglieder zwei Byte aus");

struct QZOD_DNMAPSHARED_EXPORT map_zone
{
    unsigned short x, y, w, h; //in tile form
};

struct QZOD_DNMAPSHARED_EXPORT map_object_old
{
    unsigned short x,y; //this is in tile form, not pixel
    char owner;
    unsigned char object_type;
    unsigned char object_id;
    char blevel;
};

struct QZOD_DNMAPSHARED_EXPORT map_object
{
    unsigned short x,y; //this is in tile form, not pixel
    char owner;
    unsigned char object_type;
    unsigned char object_id;
    char blevel;
    unsigned short extra_links;
    int health_percent;
};

struct QZOD_DNMAPSHARED_EXPORT map_tile
{
    unsigned short tile;
};




// =========================================================
// *********************************************************
// =========================================================
#endif // ZMAP_STRUCTURES_OLD_H
