// =========================================================
// *********************************************************
// =========================================================
#include "qzod_map.h"

//Kartendateien: feldweise, little endian (siehe port/zod_mapfile.cpp)
#include <zod_mapfile.h>
#include <amiga_startup.h>

#ifdef __amigaos__
#include "zod_palette.h"

#ifdef __amigaos__
/* port/amiga/sdl_video.cpp -- Schmutzspur des VORIGEN Bildes */
extern "C" int  zod_dirty_partial_ok(void);
extern "C" void zod_dirty_force_full(void);
extern "C" int  zod_dirty_prev_runs(int blockzeile, short *x0, short *x1, int max);
extern "C" int  zod_dirty_block_shift(void);
extern "C" int  zod_dirty_grid_h(void);
extern "C" void zod_dirty_restore_begin(void);
extern "C" void zod_dirty_restore_end(void);
#endif
extern "C" void zod_sdl_reload_palette(void);   /* port/amiga/sdl_screen.cpp */
#endif

// =========================================================
// *********************************************************
// =========================================================
ZSDL_Surface ZMap::planet_template[MAX_PLANET_TYPES];
palette_tile_info ZMap::planet_tile_info[MAX_PLANET_TYPES][MAX_PLANET_TILES];
vector<unsigned int> ZMap::map_water_plist[MAX_PLANET_TYPES];
vector<unsigned int> ZMap::map_water_effect_plist[MAX_PLANET_TYPES];
ZSDL_Surface ZMap::zone_marker[MAX_TEAM_TYPES];
ZSDL_Surface ZMap::zone_marker_water[MAX_TEAM_TYPES];
SDL_mutex *ZMap::init_mutex = SDL_CreateMutex();
// =========================================================
// *********************************************************
// =========================================================
ZMap::ZMap()
{
    //full_render = nullptr;
    last_shift_time = 0;
    submerge_info_setup = false;
    rock_list_setup = false;
    stamp_list_setup = false;
    render_fail_reported = false;
    stamp_list_w = 0;
    stamp_list_h = 0;
    map_data = nullptr;
    map_data_size = 0;

    ClearMap();
}

ZMap::~ZMap()
{
    DeRenderMap();
    DeletePathfindingInfo();
    FreeMapData();
    if( map_data != nullptr )
        free(map_data);
}
// =========================================================
// *********************************************************
// =========================================================
void ZMap::Init()
{
    FILE *fp = nullptr;
    int i,j;
    int ret;

    for(i=0;i<MAX_TEAM_TYPES;i++)
    {
        string filename;
        filename = "assets/planets/zone_marker_" + team_type_string[i] + ".png";
        //zone_marker[i].LoadBaseImage(filename);// = IMG_Load_Error ( filename );
        ZTeam::LoadZSurface(i, zone_marker[ZTEAM_BASE_TEAM], zone_marker[i], filename);

        filename = "assets/planets/zone_marker_water_" + team_type_string[i] + ".png";
        //zone_marker_water[i].LoadBaseImage(filename);// = IMG_Load_Error ( filename );
        ZTeam::LoadZSurface(i, zone_marker_water[ZTEAM_BASE_TEAM],
                            zone_marker_water[i], filename);
    }

    for(i=0;i<MAX_PLANET_TYPES;i++)
    {
        string filename;

        //filename = "assets/sounds/music_" + planet_type_string[i] + ".mp3";
        //music[i] = MUS_Load_Error ( filename.c_str() );


        //load BMP palette
        filename = "assets/planets/" + planet_type_string[i] + ".bmp";
        planet_template[i].LoadBaseImage(filename);// = SDL_LoadBMP ( filename.c_str() );

        //if(!planet_template[i])
        //	ZLOG("unable to load:%s\n", filename.c_str());

        //clear palette tile info
        for(j=0;j<MAX_PLANET_TILES;j++)
        {
            planet_tile_info[i][j].is_water = false;
            planet_tile_info[i][j].is_passable = true;
            planet_tile_info[i][j].is_usable = true;
            planet_tile_info[i][j].is_road = false;
            planet_tile_info[i][j].is_effect = false;
            planet_tile_info[i][j].is_water_effect = false;
            planet_tile_info[i][j].next_tile_in_effect = 0;
        }

        //load palette tile info
        LoadPaletteInfo(i);

        //now set some ptile info
        for(j=0;j<MAX_PLANET_TILES;j++)
        {
            if(!planet_tile_info[i][j].is_usable) continue;

            if(planet_tile_info[i][j].is_water && !planet_tile_info[i][j].is_effect)
                map_water_plist[i].push_back(j);

            if(planet_tile_info[i][j].is_water_effect)
                map_water_effect_plist[i].push_back(j);
        }
    }

    //load up crater graphics
    ZMapCraterGraphics::Init();
}



// =========================================================
// *********************************************************
// =========================================================
void ZMap::ServerInit()
{
    int i;

    for(i=0;i<MAX_PLANET_TYPES;i++)
        LoadPaletteInfo(i);
}

void ZMap::LoadPaletteInfo(int terrain_type)
{
    FILE *fp;
    int &i = terrain_type;
    size_t ret;
    string filename;

    SDL_LockMutex(init_mutex);

    filename = "assets/planets/" + planet_type_string[i] + ".tileinfo";
    fp = fopen(filename.c_str(), "rb");

    if(!fp)
    {
        ZLOG("unable to load:%s\n", filename.c_str());

        //so we'll write this default one...
        WriteMapPaletteTileInfo((planet_type)i);

        //und nochmal oeffnen -- vorher wurde aus einem ungueltigen
        //Dateizeiger gelesen
        fp = fopen(filename.c_str(), "rb");

        if(!fp)
        {
            SDL_UnlockMutex(init_mutex);
            return;
        }
    }

    //read em in
    ret = fread(planet_tile_info[i], sizeof(palette_tile_info), MAX_PLANET_TILES,  fp);

    if(ret != MAX_PLANET_TILES)
        ZLOG("unable to fully load:%s (loaded %u tiles)\n", filename.c_str(),
               static_cast<uint>(ret));

    //Datei ist little endian
    zod_tileinfo_swap(planet_tile_info[i], MAX_PLANET_TILES);

    fclose(fp);

    SDL_UnlockMutex(init_mutex);
}


bool ZMap::Loaded()
{
    return file_loaded;
}

ZSDL_Surface *ZMap::GetZoneMarkers()
{
    return zone_marker;
}

int ZMap::GetPaletteTile(int x, int y)
{
    int ret;

    x /= 16;
    y /= 16;

    ret = (y*20) + x;

    if(ret >= MAX_PLANET_TILES)
        ret = -1;
    else if(ret < 0)
        ret = -1;

    return ret;
}

palette_tile_info &ZMap::GetMapPaletteTileInfo(planet_type palette, int tile)
{
    return planet_tile_info[palette][tile];
}

int ZMap::WriteMapPaletteTileInfo(planet_type palette)
{
    FILE *fp;
    string filename;

    filename = "assets/planets/" + planet_type_string[palette] + ".tileinfo";

    fp = fopen(filename.c_str(), "wb");

    if(!fp) return 0;

    //Kopie im Dateiformat (little endian) schreiben
    {
        vector<palette_tile_info> out(planet_tile_info[palette],
                                      planet_tile_info[palette] + MAX_PLANET_TILES);

        zod_tileinfo_swap(&out[0], MAX_PLANET_TILES);
        fwrite(&out[0], sizeof(palette_tile_info), MAX_PLANET_TILES, fp);
    }

    fclose(fp);

    return 1;
}
// =========================================================
// *********************************************************
// =========================================================
int ZMap::UpdatePalettesTileFormat()
{
    FILE *fp;
    string filename;
    palette_tile_info_new planet_tile_info_new[MAX_PLANET_TYPES][MAX_PLANET_TILES];

    for(int i=0;i<MAX_PLANET_TYPES;i++)
    {
        filename = "assets/planets/" + planet_type_string[i] + ".tileinfo";

        fp = fopen(filename.c_str(), "wb");

        if(!fp)
        {
            ZLOG("ZMap::UpdatePalettesTileFormat: could update '%s'\n", filename.c_str());
            continue;
        }

        for(int j=0;j<MAX_PLANET_TILES;j++)
        {
            planet_tile_info_new[i][j].is_water = planet_tile_info[i][j].is_water;
            planet_tile_info_new[i][j].is_passable = planet_tile_info[i][j].is_passable;
            planet_tile_info_new[i][j].is_usable = planet_tile_info[i][j].is_usable;
            planet_tile_info_new[i][j].is_road = planet_tile_info[i][j].is_road;
            planet_tile_info_new[i][j].is_effect = planet_tile_info[i][j].is_effect;
            planet_tile_info_new[i][j].is_water_effect = planet_tile_info[i][j].is_water_effect;
            planet_tile_info_new[i][j].next_tile_in_effect = planet_tile_info[i][j].next_tile_in_effect;
            planet_tile_info_new[i][j].is_starter_tile = planet_tile_info[i][j].is_starter_tile;
            planet_tile_info_new[i][j].takes_tank_tracks = false;
            planet_tile_info_new[i][j].crater_type = -1;
        }
        zod_tileinfo_new_swap(planet_tile_info_new[i], MAX_PLANET_TILES);
        fwrite(planet_tile_info_new[i], sizeof(palette_tile_info_new), MAX_PLANET_TILES, fp);

        fclose(fp);
    }

    return 1;
}

ZSDL_Surface &ZMap::GetMapPalette(planet_type palette)
{
    return planet_template[palette];
}

ZSDL_Surface &ZMap::GetRender()
{
    if(!full_render.GetBaseSurface())
    {
        //is a file loaded to render?
        if(!file_loaded) return full_render;//return nullptr;
        else RenderMap();

        //it still not rendered?
        if(!full_render.GetBaseSurface()) return full_render;//return nullptr;
    }

    return full_render;
}

void ZMap::DoRender(SDL_Surface *dest, int shift_x_dest, int shift_y_dest)
{
    SDL_Rect to_rect;
    SDL_Rect from_rect;

#ifdef __amigaos__
    /* Teilweise Wiederherstellung.
     *
     * Der volle Blit (540x444 = 239 760 Byte je Bild) hat nur EINEN Zweck:
     * die Objekte des letzten Bildes zu uebermalen. Ueberall sonst steht der
     * Hintergrund unveraendert da. Es genuegt also, genau die Bereiche
     * zurueckzusetzen, die im letzten Bild beschrieben wurden -- die
     * Schmutzspur aus port/amiga/sdl_video.cpp fuehrt darueber Buch.
     *
     * Zurueck auf den vollen Blit, sobald sich der Ausschnitt verschiebt:
     * dann stimmt keine einzige Zeile mehr. Ebenso beim ersten Bild und bei
     * abgeschalteter Spur (ZOD_DIRTY=aus).
     *
     * Zeilen werden zu Rechtecken zusammengefasst, solange sie unmittelbar
     * aufeinanderfolgen -- ein Blit je Zeile waere bei 444 Zeilen teurer als
     * der volle Blit, den er ersetzen soll. */
    {
        static int letzter_shift_x = -1, letzter_shift_y = -1;
        static int letzte_view_w = -1, letzte_view_h = -1;

        const bool verschoben = (shift_x != letzter_shift_x ||
                                 shift_y != letzter_shift_y ||
                                 view_w  != letzte_view_w  ||
                                 view_h  != letzte_view_h);

        letzter_shift_x = shift_x;
        letzter_shift_y = shift_y;
        letzte_view_w = view_w;
        letzte_view_h = view_h;

        if(verschoben)
            zod_dirty_force_full();
        else if(zod_dirty_partial_ok())
        {
            /* Waehrend der Wiederherstellung NICHT in die Objektspur
             * eintragen -- sonst deckt sich die Flaeche im naechsten Bild
             * selbst wieder zu, und es wird nie weniger. Genau daran ist die
             * erste Fassung gescheitert (Hintergrundpunkte je Bild mit und
             * ohne Teilweg ziffergleich). */
            zod_dirty_restore_begin();

            /* Blockraster: je Blockzeile die Laeufe zusammenhaengender
             * Bloecke holen und als EIN Rechteck zurueckholen. Gegenueber
             * der frueheren Zeilenspur trennt das zwei Objekte, die in
             * derselben Bildzeile weit auseinanderliegen -- vorher wurde
             * alles dazwischen mitkopiert. */
            const int schub = zod_dirty_block_shift();
            const int block = 1 << schub;
            const int bz_von = shift_y_dest >> schub;
            const int bz_bis = (shift_y_dest + view_h - 1) >> schub;
            const int bz_max = zod_dirty_grid_h();

            enum { MAX_LAEUFE = 24 };
            short lx0[MAX_LAEUFE], lx1[MAX_LAEUFE];

            for(int bz = bz_von; bz <= bz_bis && bz < bz_max; bz++)
            {
                const int n = zod_dirty_prev_runs(bz, lx0, lx1, MAX_LAEUFE);

                if(!n) continue;

                int y0 = bz << schub;
                int y1 = y0 + block - 1;

                /* Zusaetzlich auf die KARTE klemmen, nicht nur auf den
                 * Ausschnitt: bei zentrierter Karte liegt aussen ein
                 * schwarzer Rand ohne Entsprechung in der Kartenflaeche, und
                 * der Mauszeiger kann ihn schmutzig machen. Ohne diese
                 * Klemme griffe die Wiederherstellung dort vor den Anfang
                 * der Flaeche.
                 *
                 * `karte_*` ist die Schirmlage des ersten bzw. letzten
                 * Kartenpunktes; fuer shift >= 0 liegt sie ausserhalb des
                 * Ausschnitts und die Klemme tut nichts. */
                const int karte_y0 = shift_y_dest - shift_y;
                const int karte_y1 = karte_y0 + (basic_info.height * 16) - 1;
                const int karte_x0 = shift_x_dest - shift_x;
                const int karte_x1 = karte_x0 + (basic_info.width * 16) - 1;

                if(y0 < shift_y_dest) y0 = shift_y_dest;
                if(y1 > shift_y_dest + view_h - 1) y1 = shift_y_dest + view_h - 1;
                if(y0 < karte_y0) y0 = static_cast<short>(karte_y0);
                if(y1 > karte_y1) y1 = static_cast<short>(karte_y1);

                if(y1 < y0) continue;

                for(int i = 0; i < n; i++)
                {
                    short xa = lx0[i], xb = lx1[i];

                    if(xa < shift_x_dest) xa = static_cast<short>(shift_x_dest);
                    if(xb > shift_x_dest + view_w - 1)
                        xb = static_cast<short>(shift_x_dest + view_w - 1);
                    if(xa < karte_x0) xa = static_cast<short>(karte_x0);
                    if(xb > karte_x1) xb = static_cast<short>(karte_x1);

                    if(xb < xa) continue;

                    from_rect.x = static_cast<short>(shift_x + (xa - shift_x_dest));
                    from_rect.y = static_cast<short>(shift_y + (y0 - shift_y_dest));
                    from_rect.w = static_cast<Uint16>(xb - xa + 1);
                    from_rect.h = static_cast<Uint16>(y1 - y0 + 1);

                    to_rect.x = xa;
                    to_rect.y = static_cast<short>(y0);

                    GetRender().BlitSurface(&from_rect, &to_rect);
                }
            }

            zod_dirty_restore_end();

            return;
        }
    }
#endif

    /* Bei zentrierter Karte ist shift_x (bzw. shift_y) NEGATIV: die Karte
     * beginnt dann nicht am linken Rand des Ausschnitts, sondern um den
     * halben schwarzen Rand weiter rechts. Die Quelle faengt bei 0 an, das
     * ZIEL rueckt. Ein negatives from_rect.x waere dagegen ein Griff vor den
     * Anfang der Kartenflaeche. */
    {
        int qx = shift_x, qy = shift_y;
        int zx = shift_x_dest, zy = shift_y_dest;
        int bw = view_w, bh = view_h;

        if(qx < 0) { zx -= qx; bw = (basic_info.width  * 16); qx = 0; }
        if(qy < 0) { zy -= qy; bh = (basic_info.height * 16); qy = 0; }

        from_rect.x = static_cast<short>(qx);
        from_rect.y = static_cast<short>(qy);
        from_rect.w = static_cast<Uint16>(bw);
        from_rect.h = static_cast<Uint16>(bh);

        to_rect.x = static_cast<short>(zx);
        to_rect.y = static_cast<short>(zy);

    }

    GetRender().BlitSurface(&from_rect, &to_rect);
    //SDL_BlitSurface(GetRender(), &from_rect, dest, &to_rect);
}

void ZMap::RenderMap()
{
    uint i,j,k;

    if(!file_loaded) return;

    if(full_render.GetBaseSurface()) DeRenderMap();

    /* Die Kartenflaeche entsteht gleich neu -- was an Zonenmarkern darin
     * steckte, ist damit weg. Ohne dieses Zuruecksetzen blieben sie auf ewig
     * unsichtbar, weil BakeZoneMarkers sie fuer bereits eingebacken hielte.
     * Betrifft jeden Kartenwechsel. */
    for(vector<map_zone_info>::iterator z=zone_list_info.begin();z!=zone_list_info.end();z++)
    {
        z->baked_owner = -2;
        z->baked_w = 0;
        z->baked_h = 0;
    }

    //begin.
    //full_render = SDL_CreateRGBSurface(SDL_HWSURFACE | SDL_SRCALPHA, basic_info.width * 16, basic_info.height * 16, 32, 0xFF000000, 0x0000FF00, 0x00FF0000, 0x000000FF);
    //full_render.LoadBaseImage(SDL_CreateRGBSurface(SDL_HWSURFACE | SDL_SRCALPHA, basic_info.width * 16, basic_info.height * 16, 32, 0xFF000000, 0x0000FF00, 0x00FF0000, 0x000000FF), false);
    //deckend: die Karte fuellt ihren Bereich vollstaendig, nichts scheint
    //durch. Spart die Alpha-Mischung beim groessten Blit jedes Bildes.
    full_render.LoadNewSurface(basic_info.width * 16, basic_info.height * 16, true);

    /* Fehlschlag SICHTBAR machen.
     *
     * Bisher war das der stillste Fehler des ganzen Spiels: LoadNewSurface
     * prueft das Ergebnis von SDL_CreateRGBSurface nicht, und LoadBaseImage
     * meldet einen Nullzeiger nur, wenn ein Dateiname gesetzt ist -- bei
     * full_render ist er leer. Danach gibt GetRender() die leere Flaeche
     * zurueck und BlitSurface steigt lautlos aus. Ergebnis: Der Hintergrund
     * wird nicht mehr gezeichnet, nichts ueberschreibt das Vorbild, alles
     * hinterlaesst Schlieren -- ohne eine einzige Zeile im Mitschnitt.
     * Genau dieses Bild hat der Nutzer ab dem zweiten bis dritten Level
     * gemeldet. */
    if(!full_render.GetBaseSurface())
    {
        //GetRender() ruft RenderMap in jedem Bild erneut auf, solange die
        //Flaeche fehlt -- nur einmal je Karte melden
        if(!render_fail_reported)
        {
            ZLOG("ZMap::RenderMap: Kartenflaeche %dx%d (%ld KB) nicht zu belegen -- "
                 "der Hintergrund bleibt leer und alles hinterlaesst Schlieren\n",
                 (int)(basic_info.width * 16), (int)(basic_info.height * 16),
                 (long)(((long)basic_info.width * 16 * basic_info.height * 16 * 4) / 1024));
            zod_mem_report("bei gescheiterter Kartenflaeche");
            render_fail_reported = true;
        }
        return;
    }

    render_fail_reported = false;
#ifdef __amigaos__
    /* Neue Kartenflaeche -- die Schmutzspur des alten Bildes ist wertlos.
     * Ohne das koennte ein Kartenwechsel mit UNVERAENDERTEM Ausschnitt
     * (beide bei 0,0) die teilweise Wiederherstellung auf eine falsche
     * Liste ansetzen, und Reste der alten Karte blieben stehen. */
    zod_dirty_force_full();
#endif

    ZLOG("Kartenflaeche %dx%d, %d Bit\n",
         (int)full_render.GetBaseSurface()->w, (int)full_render.GetBaseSurface()->h,
         (int)full_render.GetBaseSurface()->format->BitsPerPixel);
    zod_mem_report("nach Kartenflaeche");

    //SDL_Rect the_box;
    //the_box.x = 0;
    //the_box.y = 0;
    //the_box.w = basic_info.width * 16;
    //the_box.h = basic_info.height * 16;
    //ZSDL_FillRect(&the_box, 0, 0, 0, &full_render);

    if(!planet_template[basic_info.terrain_type].GetBaseSurface())
    {
        ZLOG("could not render map because terrain_type:%s was not previously loaded\n", planet_type_string[basic_info.terrain_type].c_str());
        return;
    }

#ifdef __amigaos__
    /* 8-Bit-Pfad: Die Bilder tragen keine eigene Palette, die Farben stehen
     * im Schirm. Jeder Planet hat eine eigene Bank auf den freien Plaetzen
     * (tools/assets/palette.py); hier wird sie gesetzt, sobald feststeht,
     * auf welchem Planeten gespielt wird. */
    if(zod_palette_set_planet(basic_info.terrain_type))
        zod_sdl_reload_palette();
#endif

    for(i=k=0;i<basic_info.width;i++)
        for(j=0;j<basic_info.height;j++,k++)
            RenderTile(k);
}

void ZMap::RenderTile(unsigned int index)
{
    int x, y;
    SDL_Rect from_rect, to_rect;

    //get the tile cords
    if(!GetPaletteTile(tile_list[index].tile,x,y)) return;

    from_rect.w = 16;
    from_rect.h = 16;
    from_rect.x = static_cast<short>(x);
    from_rect.y = static_cast<short>(y);

    GetTile(index, x, y);
    to_rect.w = 16;
    to_rect.h = 16;
    to_rect.x = static_cast<short>(x);
    to_rect.y = static_cast<short>(y);

// 		ZLOG("RenderMap:from(%d,%d) to(%d,%d)\n", from_rect.x, from_rect.y, to_rect.x, to_rect.y);

    //blit tile to the image
    planet_template[basic_info.terrain_type].BlitSurface(
                &from_rect, &to_rect, &full_render);
    //SDL_BlitSurface(planet_template[basic_info.terrain_type].GetBaseSurface(), &from_rect, full_render.GetBaseSurface(), &to_rect);
}
// =========================================================
// *********************************************************
// =========================================================
void ZMap::MakeNewMap(const char *new_name, planet_type palette, int width, int height)
{
    vector<unsigned short> start_tile_list;

    if(file_loaded) ClearMap();

    basic_info.width = static_cast<ushort>(width);
    basic_info.height = static_cast<ushort>(height);
    //Name kommt von aussen, das Ziel ist 50 Byte gross: ungeprueft schriebe
    //strcpy ueber die Struktur hinaus -- und direkt dahinter liegt zone_list,
    //also genau die Stelle, an der der Uebersetzerfehler schon einmal einen
    //Unsinn-Zeiger erzeugt hat.
    //snprintf statt strncpy, weil es den Abschluss garantiert.
    snprintf(basic_info.map_name, sizeof(basic_info.map_name), "%s", new_name);
    basic_info.player_count = 2;
    basic_info.object_count = 0;
    basic_info.terrain_type = palette;
    basic_info.zone_count = 0;

    //find all starter tiles
    for(ushort i=0;i<MAX_PLANET_TILES;i++)
    {
        palette_tile_info &t_info = planet_tile_info[basic_info.terrain_type][i];

        if(t_info.is_starter_tile) start_tile_list.push_back(i);
    }

    if(!start_tile_list.size())
    {
        ZLOG("MakeNewMap::Palette %s has no starter tiles set\n", planet_type_string[basic_info.terrain_type].c_str());
    }

    for(int i=0;i< basic_info.width * basic_info.height;i++)
    {
        map_tile temp_tile;
        ulong new_tile;

        if(start_tile_list.size())
        {
            new_tile = static_cast<ulong>(rand()) % start_tile_list.size();
            new_tile = start_tile_list[new_tile];
        }
        else
        {
            new_tile = 0;
        }

        temp_tile.tile = static_cast<ushort>(new_tile);

        tile_list.push_back(temp_tile);
    }

    file_loaded = true;
    InitEffects();
}

void ZMap::ReplaceUnusableTiles()
{
    vector<unsigned short> start_tile_list;

    for(ushort i=0;i<MAX_PLANET_TILES;i++)
    {
        palette_tile_info &t_info = planet_tile_info[basic_info.terrain_type][i];

        if(t_info.is_starter_tile) start_tile_list.push_back(i);
    }

    for(size_t i=0;i< basic_info.width * basic_info.height;i++)
    {
        //map_tile temp_tile;
        ulong new_tile;

        palette_tile_info &t_info =
                planet_tile_info[basic_info.terrain_type][tile_list[i].tile];
        if(t_info.is_usable) continue;
        if(start_tile_list.size())
        {
            new_tile = static_cast<ulong>(rand()) % start_tile_list.size();
            new_tile = start_tile_list[new_tile];
        }
        else
            new_tile = 0;
        tile_list[i].tile = static_cast<ushort>(new_tile);
    }
}

// =========================================================
// *********************************************************
// =========================================================
void ZMap::MakeRandomMap()
{
    if(file_loaded) ClearMap();

    basic_info.width = 60;
    basic_info.height = 180;
    snprintf(basic_info.map_name, sizeof(basic_info.map_name), "%s", "Random Map");
    basic_info.player_count = 2;
    basic_info.object_count = 0;
    basic_info.terrain_type = DESERT;
    basic_info.zone_count = 0;

    for(ulong i=0;i< basic_info.width * basic_info.height;i++)
    {
        map_tile temp_tile;
        int new_tile;
        while(1)
        {
            new_tile = rand() % MAX_PLANET_TILES;
            if(!planet_tile_info[basic_info.terrain_type][new_tile].is_usable) continue;
            if(!planet_tile_info[basic_info.terrain_type][new_tile].is_passable) continue;

            break;
        }
        tile_list[i].tile = static_cast<ushort>(new_tile);
        temp_tile.tile = static_cast<ushort>(new_tile);
        tile_list.push_back(temp_tile);
    }

    file_loaded = true;
    InitEffects();
}

bool ZMap::CheckLoad()
{
    //got as many tiles as we should?
    if(tile_list.size() != basic_info.width * basic_info.height)
    {
        ZLOG("loaded map does not have right amount of tile information\n");
        return false;
    }

    //do all the tiles check out?
    for(vector<map_tile>::iterator i=tile_list.begin(); i != tile_list.end(); ++i)
    {
        if(i->tile > MAX_PLANET_TILES)
        {
            ZLOG("loaded map has bad tiles\n");
            return false;
        }
    }

    //is it a good palette setting?
    if(basic_info.terrain_type >= MAX_PLANET_TYPES)
    {
        ZLOG("loaded map has a bad terrain palette setting\n");
        return false;
    }

    return true;
}

int ZMap::GetPaletteTile(unsigned short index, int &x, int &y)
{
    //20x24 tiles, 16x16 pixels

    if(index >= MAX_PLANET_TILES)
    {
        ZLOG("GetPaletteTile:requested invalid index:%d\n", index);
        return 0;
    }


    y = index / 20;
    x = index % 20;

    x *= 16;
    y *= 16;

    return 1;
}

void ZMap::GetTile(unsigned int index, int &x, int &y, bool is_shifted)
{
    y = index / basic_info.width;
    x = index % basic_info.width;

    x *= 16;
    y *= 16;

    if(is_shifted)
    {
        x -= shift_x;
        y -= shift_y;
    }
}

int ZMap::GetTileIndex(int x, int y, bool is_shifted)
{
    if(is_shifted)
    {
        x += shift_x;
        y += shift_y;
    }

    if(x >= basic_info.width * 16) return -1;
    if(y >= basic_info.height * 16) return -1;
    if(x < 0) return -1;
    if(y < 0) return -1;

    x /= 16;
    y /= 16;

    return (y*basic_info.width)+x;
}

map_tile &ZMap::GetTile(int x, int y, bool is_shifted)
{
    int index = GetTileIndex(x, y, is_shifted);

    if(index != -1)
        return tile_list[index];
    else
        return tile_list[0];
}

map_tile &ZMap::GetTile(unsigned int index)
{
    return tile_list[index];
}

bool ZMap::CoordIsRoad(int x, int y)
{
    int tile = GetTileIndex(x, y);

    if(tile == -1)return false;

    return planet_tile_info[basic_info.terrain_type][tile_list[tile].tile].is_road;
}

double ZMap::GetTileWalkSpeed(int x, int y, bool is_shifted)
{
    double res(0.);
    int index = GetTileIndex(x, y, is_shifted);

    if( index != -1 )
    {
        map_tile &t = tile_list[static_cast<size_t>(index)];
        palette_tile_info &t_info = planet_tile_info[basic_info.terrain_type][t.tile];

        if( t_info.is_passable ){
            if(t_info.is_road)          res= ROAD_SPEED;
            else if(t_info.is_water)    res= WATER_SPEED;
            else                        res= 1.0;
        }
    }

   return res;
}


void ZMap::FreeMapData()
{
    if(map_data) free(map_data);
    map_data = nullptr;
    map_data_size = 0;
}

void ZMap::DeRenderMap()
{
    full_render.Unload();
}

void ZMap::ClearMap()
{
    file_loaded = false;

    //Die drei Tabellen VOR basic_info.clear() freigeben: ihre Schleifen
    //laufen bis basic_info.width. Upstream stand das danach -- die Breite
    //war schon 0, freigegeben wurde nur das aeussere Feld, alle Spalten
    //blieben liegen (je ZMap 6*B*H Byte in 3*B Kleinstbloecken bei JEDEM
    //Kartenwechsel; auf dem Amiga zerstueckeln genau die den Speicher).
    DeleteSubmergeAmounts();
    DeleteRockList();
    DeleteStampList();

    basic_info.clear();
    zone_list.clear();
    object_list.clear();
    tile_list.clear();
    map_effect_list.clear();
    map_water_list.clear();

    shift_x = shift_y = 0;
    view_w = view_h = 0;

    width_pix = 0;
    height_pix = 0;

    DeRenderMap();
    DeletePathfindingInfo();
    DeleteSubmergeAmounts();
    DeleteRockList();
    DeleteStampList();
    FreeMapData();
}
// =========================================================
// *********************************************************
// =========================================================
void ZMap::InitEffects()
{
    unsigned int i;
    map_effect_list.clear();
    map_water_list.clear();

    ulong new_tile;

    // find all tiles which are apart of an effect and water tiles
    for(i=0; i<tile_list.size(); ++i)
    {
        map_tile &t = tile_list[i];
        palette_tile_info &t_info = planet_tile_info[basic_info.terrain_type][t.tile];

        if(!t_info.is_usable) continue;

        //we want to clear out all water tiles that start off as effects
        if(basic_info.terrain_type == DESERT)
        if(t_info.is_water && t_info.is_effect && map_water_plist[basic_info.terrain_type].size())
        {
            new_tile = static_cast<ulong>(rand()) % map_water_plist[basic_info.terrain_type].size();
            new_tile = map_water_plist[basic_info.terrain_type][new_tile];
            t.tile = static_cast<ushort>(new_tile);

            map_water_list.push_back(i);
            continue;
        }

        if(t_info.is_effect)
            map_effect_list.push_back(i);

        if(t_info.is_water && !t_info.is_effect)
        {
            map_water_list.push_back(i);
        }
    }

    //other stuff that needs init'd with a file load
    width_pix = basic_info.width * 16;
    height_pix = basic_info.height * 16;
    InitPathfinding();
    InitSubmergeAmounts();
    InitRockList();
    InitStampList();
    SetupAllZoneInfo();
    RebuildRegions();
}

int ZMap::DoEffects(double the_time, SDL_Surface *dest, int shift_x, int shift_y)
{
    double min_interval_time = 0.2;

    switch(basic_info.terrain_type)
    {
    case VOLCANIC:
        min_interval_time = 0.5;
        break;
    case ARCTIC:
        min_interval_time = 0.4;
        break;
    case DESERT:
    default:
        min_interval_time = 0.2;
        break;
    }

    //goto next frame and render
    for(vector<map_effect_info>::iterator i=map_effect_list.begin(); i != map_effect_list.end();)
    {
        unsigned int map_tile = i->tile;
        SDL_Rect src, dest;
        int x, y;

        palette_tile_info &p_info = planet_tile_info[basic_info.terrain_type][tile_list[map_tile].tile];


        //erasing is annoying
        if(the_time < i->next_effect_time)
        {
            //blit new tile
            src.w = 16;
            src.h = 16;
            dest.w = 16;
            dest.h = 16;

            /* Reihenfolge umgestellt: erst die Lage auf der Karte, dann der
             * Sichttest, und NUR bei Sicht die Lage im Kachelbild.
             *
             * Vorher liefen beide Rechnungen vor dem Test -- je zwei
             * Divisionen, also vier je Kachel. Diese Schleife laeuft in JEDEM
             * Bild ueber die Effektkacheln der GANZEN Karte (gezaehlt: 207 im
             * Mittel, 1137 auf der groessten Karte), waehrend das Sichtfenster
             * nur rund 7 % der Karte zeigt. Die Haelfte der Divisionen fiel
             * also fuer Kacheln an, die nie gezeichnet werden. */
            GetTile(map_tile, x, y);
            dest.x = static_cast<Sint16>(x);
            dest.y = static_cast<Sint16>(y);

            SDL_Rect from_rect, to_rect;
            if(GetBlitInfo(dest.x,dest.y,16,16, from_rect, to_rect))
            {
                GetPaletteTile(tile_list[map_tile].tile, x, y);
                src.x = static_cast<Sint16>(x);
                src.y = static_cast<Sint16>(y);

                from_rect.x += src.x;
                from_rect.y += src.y;

                to_rect.x += shift_x;
                to_rect.y += shift_y;

                planet_template[basic_info.terrain_type].BlitSurface(&from_rect, &to_rect);
            }
            i++;
            continue;
        }

        i->next_effect_time = the_time + min_interval_time + ((rand() % 4) * 0.033);

        //set next
// 			ZLOG("ZMap::DoEffects mtile:%d %d to %d\n", map_tile, tile_list[map_tile].tile, planet_tile_info[basic_info.terrain_type][tile_list[map_tile].tile].next_tile_in_effect);
        tile_list[map_tile].tile = planet_tile_info[basic_info.terrain_type][tile_list[map_tile].tile].next_tile_in_effect;

        //is it a water effect start tile
        //(therefor remove it from the list and replace it with a water tile)
        if(planet_tile_info[basic_info.terrain_type][tile_list[map_tile].tile].is_water_effect)
        {
            ulong new_tile;

            new_tile = static_cast<ulong>(rand()) % map_water_plist[basic_info.terrain_type].size();
            new_tile = map_water_plist[basic_info.terrain_type][new_tile];
            tile_list[map_tile].tile = static_cast<ushort>(new_tile);

            //do_erase = true;

            i = map_effect_list.erase(i);
            continue;
        }

        //blit new tile
        src.w = 16;
        src.h = 16;
        dest.w = 16;
        dest.h = 16;
        GetPaletteTile(tile_list[map_tile].tile, x, y);
        src.x = static_cast<Sint16>(x);
        src.y = static_cast<Sint16>(y);
        GetTile(map_tile, x, y);
        dest.x = static_cast<Sint16>(x);
        dest.y = static_cast<Sint16>(y);

// 			ZLOG("doing map effect Ptile:%d Mtile:%d (%d,%d) to (%d,%d)\n", tile_list[map_tile].tile, map_tile, src.x, src.y, dest.x, dest.y);

        SDL_Rect from_rect, to_rect;
        if(GetBlitInfo(dest.x,dest.y,16,16, from_rect, to_rect))
        {
            from_rect.x += src.x;
            from_rect.y += src.y;

            to_rect.x += shift_x;
            to_rect.y += shift_y;

            planet_template[basic_info.terrain_type].BlitSurface(&from_rect, &to_rect);
        }

        //planet_template[basic_info.terrain_type].BlitSurface(&src, &dest, &full_render);
        //SDL_BlitSurface(planet_template[basic_info.terrain_type], &src, full_render, &dest);

        i++;

        //if(do_erase)  i = map_effect_list.erase(i);
        //else ++i;
    }

    //start a water effect?
    if(map_water_effect_plist[basic_info.terrain_type].size())
    for(vector<map_effect_info>::iterator i=map_water_list.begin(); i != map_water_list.end(); ++i)
    {
        if(the_time < i->next_effect_time) continue;

        i->next_effect_time = the_time + min_interval_time + ((rand() % 4) * 0.033);

        unsigned int map_tile = i->tile;
        palette_tile_info &p_info = planet_tile_info[basic_info.terrain_type][tile_list[map_tile].tile];

        //is it available?
        if(!p_info.is_effect)
        {
            unsigned long new_tile;
            //one in...
            if(rand() % 40) continue;

            //add to effects list
            map_effect_list.push_back(map_tile);
// 				ZLOG("map_water_effect_list.size():%d\n", map_water_effect_list.size());

            new_tile = static_cast<ulong>(rand()) % map_water_effect_plist[basic_info.terrain_type].size();
            new_tile = map_water_effect_plist[basic_info.terrain_type][new_tile];

            tile_list[map_tile].tile = static_cast<ushort>(new_tile);
        }
    }

    return 1;
}
bool ZMap::GetMapData(char *&data, int &size)
{
    data = map_data;
    size = map_data_size;

    return true;
}

// =========================================================
// *********************************************************
// =========================================================
int ZMap::Read(char* data, int size, bool scrap_data)
{
    size_t item_size;
    int i;

    //scrap it
    if(scrap_data) ClearMap();

    //sanity
    if(!data) return 0;
    if(size <= 0) return 0;

    //feldweise lesen: das Dateiformat ist little endian und hat x86-Ausrichtung
    //(map_object 16 Byte statt 14 auf m68k), siehe port/zod_mapfile.cpp
    if(!zod_mapfile_parse(data, size, basic_info, zone_list, object_list, tile_list))
        return 0;

    //is the in data good?
    if(CheckLoad())
    {
        file_loaded = true;
        InitEffects();
    }

    return 1;
}

int ZMap::Read(const char* filename)
{
    FILE *fp;
    size_t ret;

    //scrap it
    ClearMap();

    //sanity checks
    if(!filename) return 0;
    if(!filename[0]) return 0;

    fp = fopen(filename, "rb");

    if(!fp) return 0;

    const int buf_size = 1024;
    char buf[buf_size];

    while( (ret = fread(buf, 1, buf_size, fp)) )
    {
        //Das Original nahm den Rueckgabewert der Belegung ungeprueft. Schlaegt
        //sie fehl, schreibt memcpy hinter einen Nullzeiger -- auf AmigaOS mitten
        //in die Systemstrukturen, und zwar still: ohne MMU faellt es erst
        //irgendwann spaeter und an ganz anderer Stelle auf.
        //Zudem gibt realloc bei Misserfolg den alten Block NICHT frei; das
        //Ergebnis darf daher nicht direkt nach map_data geschrieben werden.
        char *grown = static_cast<char*>(
                        map_data
                          ? realloc(map_data, static_cast<size_t>(map_data_size) + ret)
                          : malloc(ret));

        if(!grown)
        {
            ZLOG("ZMap::Read: kein Speicher fuer %ld Bytes Kartendaten\n",
                 (long)(map_data_size + (int)ret));
            fclose(fp);
            FreeMapData();
            return 0;
        }

        map_data = grown;

        memcpy(map_data + map_data_size, buf, ret);
        map_data_size += ret;
    }

    fclose(fp);

    return Read(map_data, map_data_size, false);
}

int ZMap::Write(const char* filename)
{
    FILE *fp;
    size_t ret;

    //sanity checks
    if(!filename) return 0;
    if(!filename[0]) return 0;

    fp = fopen(filename, "wb");

    if(!fp) return 0;

    //lets just make this double sure...
    basic_info.zone_count = static_cast<ushort>(zone_list.size());
    basic_info.object_count = static_cast<ushort>(object_list.size());
    if(basic_info.width * basic_info.height != tile_list.size())
        ZLOG("ZMap::Write::warning width * height != tile_list.size\n");

    //feldweise schreiben, damit die Datei auf jeder Plattform dieselbe ist
    {
        int need = ZOD_MAPFILE_BASICS_SIZE
                 + static_cast<int>(zone_list.size()) * ZOD_MAPFILE_ZONE_SIZE
                 + static_cast<int>(object_list.size()) * ZOD_MAPFILE_OBJECT_SIZE
                 + static_cast<int>(tile_list.size()) * ZOD_MAPFILE_TILE_SIZE;
        vector<char> out(need);
        int written = zod_mapfile_serialize(basic_info, zone_list, object_list, tile_list,
                                            &out[0], need);

        if(written != need)
        {
            fclose(fp);
            return 0;
        }

        ret = fwrite(&out[0], 1, static_cast<size_t>(written), fp);

        if(ret != static_cast<size_t>(written))
        {
            fclose(fp);
            return 0;
        }
    }

    fclose(fp);
    return 1;
}

void ZMap::ChangeTile(unsigned int index, map_tile new_tile)
{
    //take the old tile off any lists
    map_tile &to = tile_list[index];
    palette_tile_info &to_info = planet_tile_info[basic_info.terrain_type][to.tile];

    if(to_info.is_usable)
    {
        if(to_info.is_effect)
        {
            //map_effect_list.push_back(index);
            for(auto i=map_effect_list.begin();i!=map_effect_list.end(); )
            {
                //if(*i == index)
                if(i->tile == index)
                    i = map_effect_list.erase(i);
                else
                    i++;
            }
        }

        if(to_info.is_water && !to_info.is_effect)
        {
            //map_water_list.push_back(index);
            for(auto i=map_water_list.begin();i!=map_water_list.end();)
            {
                //if(*i == index)
                if(i->tile == index)
                    i = map_water_list.erase(i);
                else
                    i++;
            }
        }
    }

    //place
    tile_list[index] = new_tile;

    //does it need to enter a list?
    map_tile &t = tile_list[index];
    palette_tile_info &t_info = planet_tile_info[basic_info.terrain_type][t.tile];

    if(t_info.is_usable)
    {
        if(t_info.is_effect) map_effect_list.push_back(index);

        if(t_info.is_water && !t_info.is_effect)
        {
            map_water_list.push_back(index);
        }
    }

    //rerender
    if(full_render.GetBaseSurface())
        RenderTile(index);
}

map_basics &ZMap::GetMapBasics()
{
    return basic_info;
}

// =========================================================
// *********************************************************
// =========================================================
/* Die Verschiebung des Ausschnitts in gueltige Grenzen bringen.
 *
 * Diese drei Zeilen standen SIEBENMAL im Code (SetViewingDimensions,
 * SetViewShift, ShiftView{Left,Right,Up,Down}). Eine neue Regel siebenmal
 * richtig hinzuschreiben ist genau die Art Aenderung, bei der eine Stelle
 * uebersehen wird -- deshalb zuerst zusammengezogen.
 *
 * NEU: Ist die Karte SCHMALER (oder niedriger) als der Ausschnitt, gibt es
 * nichts zu rollen. Dann wird sie ZENTRIERT, und `shift_x` ist NEGATIV.
 *
 * Warum das der sichere Weg ist: Jede Umrechnung zwischen Schirm und Karte
 * laeuft ueber `shift_x` -- Maus (`map_x = mouse_x + shift_x`, 14 Stellen),
 * Objekte (`x - shift_x`), Sichttest, Wegpunkte, Auswahl. Ein negativer Wert
 * zentriert damit ALLES von selbst. Der Weg ueber `map_place_x` haette
 * dagegen nur das Zeichnen verschoben und jede einzelne Mausstelle
 * gebraucht -- eine uebersehene haette geheissen: Auswahl greift, aber der
 * Wegpunkt liegt 78 Punkte daneben.
 *
 * Wer das rueckgaengig machen will, aendert nur diese Funktion. */
void ZMap::ClampShift()
{
    const int full_width  = (basic_info.width  * 16);
    const int full_height = (basic_info.height * 16);

    if(full_width <= view_w)
        shift_x = (full_width - view_w) / 2;      /* zentriert, negativ */
    else
    {
        if(shift_x > full_width - view_w) shift_x = full_width - view_w;
        if(shift_x < 0) shift_x = 0;
    }

    if(full_height <= view_h)
        shift_y = (full_height - view_h) / 2;
    else
    {
        if(shift_y > full_height - view_h) shift_y = full_height - view_h;
        if(shift_y < 0) shift_y = 0;
    }
}

void ZMap::SetViewingDimensions(int w, int h)
{
    view_w = w;
    view_h = h;

    ClampShift();
}

void ZMap::SetViewShift(int x_, int y_)
{
    shift_x = x_;
    shift_y = y_;

    ClampShift();
}

bool ZMap::ShiftViewRight(int amt)
{
    const int vorher = shift_x;

    shift_x += amt;
    ClampShift();

    /* Wahr, wenn die Verschiebung vollstaendig ausgefuehrt wurde. Frueher
     * wurde dafuer geprueft, OB geklemmt wurde; das ist dasselbe, kommt aber
     * ohne eine zweite Kopie der Klemmregel aus. */
    return (shift_x == vorher + amt);
}

bool ZMap::ShiftViewUp(int amt)
{
    const int vorher = shift_y;

    shift_y -= amt;
    ClampShift();

    /* Wahr, wenn die Verschiebung vollstaendig ausgefuehrt wurde. Frueher
     * wurde dafuer geprueft, OB geklemmt wurde; das ist dasselbe, kommt aber
     * ohne eine zweite Kopie der Klemmregel aus. */
    return (shift_y == vorher - amt);
}

bool ZMap::ShiftViewDown(int amt)
{
    const int vorher = shift_y;

    shift_y += amt;
    ClampShift();

    /* Wahr, wenn die Verschiebung vollstaendig ausgefuehrt wurde. Frueher
     * wurde dafuer geprueft, OB geklemmt wurde; das ist dasselbe, kommt aber
     * ohne eine zweite Kopie der Klemmregel aus. */
    return (shift_y == vorher + amt);
}

bool ZMap::ShiftViewLeft(int amt)
{
    const int vorher = shift_x;

    shift_x -= amt;
    ClampShift();

    /* Wahr, wenn die Verschiebung vollstaendig ausgefuehrt wurde. Frueher
     * wurde dafuer geprueft, OB geklemmt wurde; das ist dasselbe, kommt aber
     * ohne eine zweite Kopie der Klemmregel aus. */
    return (shift_x == vorher - amt);
}

bool ZMap::ShiftViewRight()
{
    return ShiftViewRight(static_cast<int>(ShiftViewDifference()));
}

bool ZMap::ShiftViewUp()
{
    return ShiftViewUp(static_cast<int>(ShiftViewDifference()));
}

bool ZMap::ShiftViewDown()
{
    return ShiftViewDown(static_cast<int>(ShiftViewDifference()));
}

bool ZMap::ShiftViewLeft()
{
    return ShiftViewLeft(static_cast<int>(ShiftViewDifference()));
}

double ZMap::ShiftViewDifference()
{
    double shift_difference;
    double time_difference;
    double the_time = current_time();

    time_difference = the_time - last_shift_time;
    if(time_difference > SHIFT_CLICK_STREAM)
    {
        shift_difference = MAX_SHIFT_CLICK;
        shift_overflow = 0;
        last_shift_time = the_time;
    }
    else
    {
        shift_difference = (time_difference * SHIFT_CLICK_S) + shift_overflow;
        shift_overflow = shift_difference - static_cast<int>(shift_difference);
        last_shift_time = the_time;
    }

    return shift_difference;
}

void ZMap::GetViewLimits(int &map_left, int &map_right, int &map_top, int &map_bottom)
{
    map_left = shift_x;
    map_top = shift_y;
    map_right = map_left + view_w;
    map_bottom = map_top + view_h;
}

void ZMap::GetViewShiftFull(int &x, int &y, int &view_w_, int &view_h_)
{
    x = shift_x;
    y = shift_y;
    view_w_ = view_w;
    view_h_ = view_h;
}

void ZMap::GetViewShift(int &x, int &y)
{
    x = shift_x;
    y = shift_y;
}

void ZMap::GetMapCoords(int mouse_x, int mouse_y, int &map_x, int &map_y)
{
    map_x = mouse_x + shift_x;
    map_y = mouse_y + shift_y;
}

int ZMap::WithinView(int x, int y, int w, int h)
{
    if(x > shift_x + view_w) return 0;
    if(y > shift_y + view_h) return 0;
    if(x + w < shift_x) return 0;
    if(y + h < shift_y) return 0;

    return 1;
}
// =========================================================
// *********************************************************
// =========================================================
void ZMap::RenderZSurface(ZSDL_Surface *surface, int x, int y,
                          bool render_hit, bool about_center)
{
    surface->RenderSurface((x - shift_x), (y - shift_y), render_hit, about_center);
}

void ZMap::RenderZSurfaceHorzRepeat(ZSDL_Surface *surface,
                                    int x, int y, int w_total, bool render_hit)
{
    SDL_Rect from_rect, to_rect;
    int fw, fh;

    if(!surface) return;
    if(!surface->GetBaseSurface()) return;

    fw = surface->GetBaseSurface()->w;
    fh = surface->GetBaseSurface()->h;

    while(w_total>0)
    {
        if(w_total > fw)
        {
            if(GetBlitInfo(x, y, fw, fh, from_rect, to_rect))
                surface->BlitHitSurface(&from_rect, &to_rect, nullptr, render_hit);

            w_total -= fw;
            x += fw;
        }
        else
        {
            if(GetBlitInfo(x, y, w_total, fh, from_rect, to_rect))
                surface->BlitHitSurface(&from_rect, &to_rect, nullptr, render_hit);

            w_total = 0;
        }
    }
}

void ZMap::RenderZSurfaceVertRepeat(ZSDL_Surface *surface,
                                    int x, int y, int h_total, bool render_hit)
{
    SDL_Rect from_rect, to_rect;
    int fw, fh;

    if(!surface) return;
    if(!surface->GetBaseSurface()) return;

    fw = surface->GetBaseSurface()->w;
    fh = surface->GetBaseSurface()->h;

    while(h_total>0)
    {
        if(h_total > fh)
        {
            if(GetBlitInfo(x, y, fw, fh, from_rect, to_rect))
                surface->BlitHitSurface(&from_rect, &to_rect, nullptr, render_hit);

            h_total -= fh;
            y += fh;
        }
        else
        {
            if(GetBlitInfo(x, y, fw, h_total, from_rect, to_rect))
                surface->BlitHitSurface(&from_rect, &to_rect, nullptr, render_hit);

            h_total = 0;
        }
    }
}

int ZMap::GetBlitInfo(int x, int y, int w, int h, SDL_Rect &from_rect, SDL_Rect &to_rect)
{
    //is this visable at ??
    if(x > shift_x + view_w) return 0;
    if(y > shift_y + view_h) return 0;
    if(x + w < shift_x) return 0;
    if(y + h < shift_y) return 0;

    //setup to
    to_rect.x = static_cast<Sint16>(x - shift_x);
    to_rect.y = static_cast<Sint16>(y - shift_y);
    to_rect.w = 0;
    to_rect.h = 0;

    if(to_rect.x < 0)
    {
        from_rect.x = -to_rect.x;
        from_rect.w = static_cast<Uint16>(w + to_rect.x);
        to_rect.x += from_rect.x;

        if(from_rect.w > view_w) from_rect.w = static_cast<Uint16>(view_w);
    }
    else if(to_rect.x + w > view_w)
    {
        from_rect.x = 0;
        from_rect.w = static_cast<Uint16>(view_w - to_rect.x);
    }
    else
    {
        from_rect.x = 0;
        from_rect.w = static_cast<Uint16>(w);
    }

    if(to_rect.y < 0)
    {
        from_rect.y = -to_rect.y;
        from_rect.h = static_cast<Uint16>(h + to_rect.y);
        to_rect.y += from_rect.y;

        if(from_rect.h > view_h) from_rect.h = static_cast<Uint16>(view_h);
    }
    else if(to_rect.y + h > view_h)
    {
        from_rect.y = 0;
        from_rect.h = static_cast<Uint16>(view_h - to_rect.y);
    }
    else
    {
        from_rect.y = 0;
        from_rect.h = static_cast<Uint16>(h);
    }
    return 1;
}

extern unsigned long zod_sicht_tests;     /* zsdl_opengl.cpp */
extern unsigned long zod_sicht_treffer;

int ZMap::GetBlitInfo(SDL_Surface *src, int x, int y, SDL_Rect &from_rect,
                      SDL_Rect &to_rect)
{
    if(!src) return 0;

    /* Zweiter Sichtweg neben ZSDL_Surface::GetMapBlitInfo -- ueber ihn laufen
     * unter anderem die Steine (drei Schattenkacheln je Stein). Beide Wege
     * zaehlen in dieselben Zaehler, sonst beschreibt die Quote nur die Haelfte. */
    zod_sicht_tests++;

    /* Zugeschnitten wird auf den Schnitt aus Ausschnitt UND Karte, beides in
     * Kartenkoordinaten. Die Kartengrenze ist nicht nur Formsache: Ein Stein
     * zeichnet seinen Schatten drei Kacheln nach unten, am unteren Kartenrand
     * also darueber hinaus. Fuellt die Karte den Ausschnitt nicht aus, liegt
     * dort schwarzer Rand, und ohne diese Grenze landete der Schatten darin.
     * Dasselbe gilt in ZSDL_Surface::GetMapBlitInfo fuer Voegel und Effekte. */
    const int map_w = basic_info.width  * 16;
    const int map_h = basic_info.height * 16;

    int cx0 = shift_x, cy0 = shift_y;
    int cx1 = shift_x + view_w, cy1 = shift_y + view_h;

    if(cx0 < 0) cx0 = 0;
    if(cy0 < 0) cy0 = 0;
    if(cx1 > map_w) cx1 = map_w;
    if(cy1 > map_h) cy1 = map_h;

    int x0 = x, y0 = y;
    int x1 = x + src->w, y1 = y + src->h;

    if(x0 < cx0) x0 = cx0;
    if(y0 < cy0) y0 = cy0;
    if(x1 > cx1) x1 = cx1;
    if(y1 > cy1) y1 = cy1;

    if(x1 <= x0 || y1 <= y0) return 0;

    zod_sicht_treffer++;

    from_rect.x = static_cast<Sint16>(x0 - x);
    from_rect.y = static_cast<Sint16>(y0 - y);
    from_rect.w = static_cast<Uint16>(x1 - x0);
    from_rect.h = static_cast<Uint16>(y1 - y0);

    to_rect.x = static_cast<Sint16>(x0 - shift_x);
    to_rect.y = static_cast<Sint16>(y0 - shift_y);
    to_rect.w = 0;
    to_rect.h = 0;

    return 1;
}

void ZMap::PlaceObject(map_object new_object)
{
    object_list.push_back(new_object);
}

vector<map_object> &ZMap::GetObjectList()
{
    return object_list;
}

vector<map_zone> &ZMap::GetZoneList()
{
    return zone_list;
}

int ZMap::AddZone(map_zone new_zone)
{
    //is it a good zone?
    if(new_zone.x >= basic_info.width) return 0;
    if(new_zone.y >= basic_info.height) return 0;
    if(new_zone.w > basic_info.width) return 0;
    if(new_zone.h > basic_info.height) return 0;

    //does it already exist?
    for(vector<map_zone>::iterator i=zone_list.begin(); i!=zone_list.end(); i++)
        if(i->x == new_zone.x && i->y == new_zone.y)
            return 0;

    zone_list.push_back(new_zone);

    SetupAllZoneInfo();

    return 1;
}

int ZMap::RemoveZone(int x, int y)
{
    for(vector<map_zone>::iterator i=zone_list.begin(); i!=zone_list.end(); i++)
        if(i->x == x && i->y == y)
    {
        zone_list.erase(i);

        SetupAllZoneInfo();
        return 1;
    }
    return 0;
}

map_zone *ZMap::GetZoneExact(int x, int y)
{
    for(vector<map_zone>::iterator i=zone_list.begin(); i!=zone_list.end(); i++)
        if(i->x == x && i->y == y)
            return &(*i);
    return nullptr;
}

map_zone_info *ZMap::GetZone(int x, int y)
{
   for(vector<map_zone_info>::iterator i = zone_list_info.begin();i!=zone_list_info.end();i++)
   {
      if(x < i->x) continue;
      if(y < i->y) continue;
      if(x > i->x + i->w) continue;
      if(y > i->y + i->h) continue;

      return &(*i);
   }
   return nullptr;
}

vector<map_zone_info> &ZMap::GetZoneInfoList()
{
   return zone_list_info;
}

void ZMap::SetupAllZoneInfo()
{
    size_t i; //,j,k;

    zone_list_info.clear();

    for(i=0; i<zone_list.size(); i++)
    {
        size_t x,y; //,w,h;
        size_t mtile, mtile_x, mtile_y;
        map_zone &cur_zone = zone_list[i];
        map_zone_info new_map_zone_info;

        //set id
        new_map_zone_info.id = static_cast<int>(i);


        //find owner
        new_map_zone_info.owner = NULL_TEAM;

        //set dimensions
        new_map_zone_info.x = cur_zone.x * 16;
        new_map_zone_info.y = cur_zone.y * 16;
        new_map_zone_info.w = cur_zone.w * 16;
        new_map_zone_info.h = cur_zone.h * 16;

        //add in tiles
        new_map_zone_info.tile.clear();
        for(size_t j=1;j<cur_zone.w-1;j++)
        {
            mtile_x = (cur_zone.x + j);
            mtile_y = cur_zone.y;
            mtile = (mtile_y*basic_info.width)+mtile_x;

            x = mtile_x * 16 + 6;
            y = mtile_y * 16 + 6;

            if(planet_tile_info[basic_info.terrain_type][tile_list[mtile].tile].is_passable)
            {
                if(planet_tile_info[basic_info.terrain_type][tile_list[mtile].tile].is_water)
                    new_map_zone_info.tile.push_back(
                                map_zone_info_tile(static_cast<int>(x),
                                                   static_cast<int>(y),
                                                   true));
                else
                    new_map_zone_info.tile.push_back(map_zone_info_tile(
                                                         static_cast<int>(x),
                                                         static_cast<int>(y)
                                                         ));
            }

            mtile_x = (cur_zone.x + j);
            mtile_y = (cur_zone.y + (cur_zone.h - 1));
            mtile = (mtile_y*basic_info.width)+mtile_x;

            x = mtile_x * 16 + 6;
            y = mtile_y * 16 + 6;

            if(planet_tile_info[basic_info.terrain_type][tile_list[mtile].tile].is_passable)
            {
                if(planet_tile_info[basic_info.terrain_type][tile_list[mtile].tile].is_water)
                    new_map_zone_info.tile.push_back(
                                map_zone_info_tile(static_cast<int>(x),
                                                   static_cast<int>(y),
                                                   true));
                else
                    new_map_zone_info.tile.push_back(map_zone_info_tile(
                                                         static_cast<int>(x),
                                                         static_cast<int>(y)
                                                         ));
            }
        }
        for(int j=0;j<cur_zone.h;j++)
        {
            mtile_x = cur_zone.x;
            mtile_y = (cur_zone.y + j);
            mtile = (mtile_y*basic_info.width)+mtile_x;

            x = mtile_x * 16 + 6;
            y = mtile_y * 16 + 6;

            if(planet_tile_info[basic_info.terrain_type][tile_list[mtile].tile].is_passable)
            {
                if(planet_tile_info[basic_info.terrain_type][tile_list[mtile].tile].is_water)
                    new_map_zone_info.tile.push_back(
                                map_zone_info_tile(static_cast<int>(x),
                                                   static_cast<int>(y),
                                                   true));
                else
                    new_map_zone_info.tile.push_back(map_zone_info_tile(
                                                         static_cast<int>(x),
                                                         static_cast<int>(y)
                                                         ));
            }

            mtile_x = (cur_zone.x + (cur_zone.w - 1));
            mtile_y = (cur_zone.y + j);
            mtile = (mtile_y*basic_info.width)+mtile_x;

            x = mtile_x * 16 + 6;
            y = mtile_y * 16 + 6;

            if(planet_tile_info[basic_info.terrain_type][tile_list[mtile].tile].is_passable)
            {
                if(planet_tile_info[basic_info.terrain_type][tile_list[mtile].tile].is_water)
                    new_map_zone_info.tile.push_back(
                                map_zone_info_tile(static_cast<int>(x),
                                                   static_cast<int>(y),
                                                   true));
                else
                    new_map_zone_info.tile.push_back(map_zone_info_tile(
                                                         static_cast<int>(x),
                                                         static_cast<int>(y)
                                                         ));
            }
        }

        /* Einmal merken statt je Bild suchen. */
        new_map_zone_info.has_water = false;

        for(size_t t=0; t<new_map_zone_info.tile.size(); t++)
            if(new_map_zone_info.tile[t].is_water)
            {
                new_map_zone_info.has_water = true;
                break;
            }

        zone_list_info.push_back(new_map_zone_info);
    }
}

/* Zaehlwerte fuer DoZoneEffects. Der Posten ist auf der V1200 mit 1566 us je
 * Bild der zweitgroesste im Zeichnen -- aber eine Zeitangabe allein sagt nicht,
 * ob der Aufruf oder das Pixel kostet. Dieselbe Lehre wie beim Ausgabeweg:
 * erst die Stueckzahl zaehlen, dann rechnen. Die Zahlen sind
 * plattformunabhaengig (gleiche Karte, gleicher Ausschnitt), lassen sich also
 * auf dem Host holen. */
unsigned long zod_zone_zonen = 0;      /* Zonen betrachtet          */
unsigned long zod_zone_sichtbar = 0;   /* davon nach Sichttest      */
unsigned long zod_zone_kacheln = 0;    /* Kacheln betrachtet        */
unsigned long zod_zone_blits = 0;      /* davon wirklich gezeichnet */
unsigned long zod_zone_wasser = 0;     /* davon Wasser (bewegt sich)*/
unsigned long zod_zone_bilder = 0;
unsigned long zod_zone_gebacken = 0;   /* Einbackvorgaenge (Start + Wechsel) */

/* A/B-Schalter NUR fuer die Messung: `SetEnv ZOD_ZONEN aus` laesst die Blits
 * der Zonenmarker weg und behaelt Schleife, Sichttest und GetBlitInfo. Die
 * Differenz zweier Laeufe im Posten `zeichnen.zonen` ist damit genau der Preis
 * der 69 Blits -- die Frage "kostet der Aufruf oder das Pixel", die sich beim
 * Ausgabeweg nur durch Zaehlen klaeren liess.
 *
 * Waehrend so eines Laufes fehlen die Zonenmarker im Bild. Das ist Absicht und
 * betrifft nur die Darstellung; an der Spielmechanik aendert sich nichts, die
 * Zonen und ihre Besitzer bleiben unberuehrt. Vorgabe ist AN.
 *
 * Warum nicht mit der feinen Uhr um jeden Blit gemessen: Ein Zeitabruf kostet
 * auf der V1200 9882 ns, 69 Blits brauchten 138 Abrufe = 1366 us. Die Messung
 * waere so gross wie das Gemessene. */
static int zone_blits_an(void)
{
    static int wahl = -1;

    if(wahl < 0)
    {
        char buf[16];
        const char *e = zod_env("ZOD_ZONEN", buf, sizeof(buf));

        wahl = (e && (*e == 'a' || *e == 'A')) ? 0 : 1;

        if(!wahl) ZLOG("Zonenmarker: Blits AUS (nur Messung)\n");
    }

    return wahl;
}

extern "C" void zod_zone_report(unsigned long *zonen, unsigned long *sichtbar,
                                unsigned long *kacheln, unsigned long *blits,
                                unsigned long *wasser, unsigned long *bilder,
                                unsigned long *gebacken)
{
    *zonen = zod_zone_zonen; *sichtbar = zod_zone_sichtbar;
    *kacheln = zod_zone_kacheln; *blits = zod_zone_blits;
    *wasser = zod_zone_wasser; *bilder = zod_zone_bilder;
    *gebacken = zod_zone_gebacken;
}

/* Zonenmarker EINMALIG in die Kartenflaeche stempeln.
 *
 * WARUM: Die Marker sind 8x4 Punkte (32 Byte) und aendern sich nur bei
 * Besitzerwechsel -- gezeichnet wurden sie bis hierher in JEDEM Bild einzeln.
 * Auf der V1200 gemessen (A/B ueber ZOD_ZONEN, 19.09., 193 Blits je Bild):
 *
 *     zeichnen.zonen   1655 -> 668 us   (die Blits selbst)
 *     zeichnen.karte   1921 -> 1332 us  (Hintergrund zurueckholen)
 *     umschalten       2107 -> 1621 us  (Ausgabe)
 *     Arbeit je Bild  16419 -> 14226 us (-13,4 %)
 *
 * NUR 45 Prozent davon sind die Blits. Der Rest ist Folgewirkung auf die
 * Schmutzspur: verstreute Kleinstsprites machen Dutzende 16x16-Bloecke
 * schmutzig, danach muss dort der Kartenhintergrund zurueckgeholt UND mehr in
 * den Schirm kopiert werden. In der Kartenflaeche kostet beides nichts mehr --
 * der Marker IST dann der Hintergrund.
 *
 * WIE DER BESITZERWECHSEL ERKANNT WIRD: nicht an den Setzern (es gibt vier,
 * in zclient, oflag und zweimal zserver), sondern hier am Vergleich
 * `baked_owner != owner`. Das kostet neun Vergleiche je Bild und kann keinen
 * Setzer uebersehen.
 *
 * UNTERGRUND: Der alte Marker wird vom neuen einfach ueberdeckt, ohne dass
 * etwas gesichert werden muesste. Belegt, nicht angenommen -- ALLE Marker
 * haben genau 16 deckende Punkte in einem vollstaendig deckenden 4x4-Kasten
 * bei (0,0):
 *
 *     zone_marker_null.png   4x4   16 deckend, Kasten 0..3 / 0..3
 *     zone_marker_red.png    8x4   16 deckend, Kasten 0..3 / 0..3
 *
 * Die 8x4-Dateien sind lediglich mit durchsichtigen Spalten aufgefuellt; die
 * Teamvarianten entstehen ohnehin aus demselben Grundbild (ZTeam::LoadZSurface)
 * und teilen deshalb den Alphakanal.
 *
 * DAS WAR EIN UMWEG, und er gehoert festgehalten: Erst hatte ich aus den
 * Bildgroessen geschlossen, ein Marker koenne "wachsen, aber nicht
 * schrumpfen", und eine Wache auf die GROESSE gesetzt. Die schlug auf der
 * V1200 an ("Zone 8 schrumpft von 8x4 auf 4x4"). Daraufhin habe ich einen
 * gesicherten Untergrund gebaut -- und die Gegenprobe zeigte, dass Bild MIT
 * und OHNE Reparatur ziffergleich sind. Es gab nie einen Rueckstand, weil die
 * zusaetzlichen vier Spalten nie gezeichnet wurden. Der ganze Aufwand war
 * umsonst, die Wache ein Fehlalarm.
 *
 * Lehre: Eine Wache muss das pruefen, worauf es ankommt -- hier den GEZEICHNETEN
 * Bereich, nicht die Flaechengroesse. Und eine Reparatur ohne Gegenprobe haette
 * mir dauerhaft vorgetaeuscht, ein Problem geloest zu haben, das es nicht gab.
 *
 * WASSERMARKER bleiben aussen vor: die wippen (bob_i) und muessen je Bild
 * gezeichnet werden.
 */
/* Groesse des wirklich GEZEICHNETEN Bereichs einer Markerflaeche: der Kasten
 * um alle Punkte, die nicht der Farbschluessel sind. Die Bilddatei ist damit
 * nicht zu verwechseln -- zone_marker_red.png ist 8x4 gross und zeichnet 4x4. */
void ZMap::MarkerOpaqueBox(SDL_Surface *s, int &w, int &h)
{
    w = h = 0;

    if(!s || !s->pixels || s->format->BytesPerPixel != 1) { if(s) { w = s->w; h = s->h; } return; }

    const unsigned char key = (unsigned char)s->format->colorkey;
    const bool has_key = (s->flags & SDL_SRCCOLORKEY) != 0;

    int maxx = -1, maxy = -1;

    for(int y = 0; y < s->h; y++)
    {
        const unsigned char *row = (const unsigned char*)s->pixels + (size_t)y * s->pitch;

        for(int x = 0; x < s->w; x++)
            if(!has_key || row[x] != key) { if(x > maxx) maxx = x; if(y > maxy) maxy = y; }
    }

    w = maxx + 1;
    h = maxy + 1;
}

void ZMap::BakeZoneMarkers()
{
    if(!full_render.GetBaseSurface()) return;

    /* `ZOD_ZONEN aus` schaltet die Marker weiterhin ganz ab -- sonst wuerde der
     * Schalter nach dem Umbau nur noch die Wassermarker treffen und der
     * A/B-Vergleich waere still bedeutungslos geworden. */
    if(!zone_blits_an()) return;

    for(vector<map_zone_info>::iterator i=zone_list_info.begin();i!=zone_list_info.end();i++)
    {
        if(i->baked_owner == (int)i->owner) continue;

        ZSDL_Surface &marker = zone_marker[i->owner];
        SDL_Surface  *ms     = marker.GetBaseSurface();

        if(!ms) continue;

        /* Wache auf den GEZEICHNETEN Bereich (deckender Kasten), nicht auf
         * die Flaechengroesse. Schrumpft der, bliebe vom alten Marker etwas
         * stehen -- dann stimmt die Annahme oben nicht mehr. */
        int ow = 0, oh = 0;

        MarkerOpaqueBox(ms, ow, oh);

        if(i->baked_owner != -2 && (ow < i->baked_w || oh < i->baked_h))
            ZLOG("Zonenmarker: Zone %ld -- gezeichneter Bereich schrumpft von "
                 "%ldx%ld auf %ldx%ld, der alte Marker bleibt teilweise stehen\n",
                 (long)i->id, (long)i->baked_w, (long)i->baked_h,
                 (long)ow, (long)oh);

        for(vector<map_zone_info_tile>::iterator j=i->tile.begin();j!=i->tile.end();j++)
        {
            if(j->is_water) continue;   /* wippt, bleibt dynamisch */

            PermStamp(j->render_loc.x, j->render_loc.y, &marker, false);
        }

        i->baked_owner = (int)i->owner;
        i->baked_w = ow;
        i->baked_h = oh;

        zod_zone_gebacken++;

#ifdef __amigaos__
        /* Die Kartenflaeche hat sich geaendert, der Schirm weiss davon nichts.
         * Ohne das erschiene die neue Farbe erst dort, wo zufaellig ein Objekt
         * darueberlief -- die Teilwiederherstellung holt nur zurueck, was im
         * letzten Bild verdeckt war. */
        zod_dirty_force_full();
#endif
    }
}

void ZMap::DoZoneEffects(double the_time, SDL_Surface *dest, int shift_x, int shift_y)
{
    zod_zone_bilder++;

    BakeZoneMarkers();

    for(vector<map_zone_info>::iterator i=zone_list_info.begin();i!=zone_list_info.end();i++)
    {
        zod_zone_zonen++;
        /* Ganze Zone in EINEM Test verwerfen, statt je Randkachel zu pruefen.
         *
         * Die Urfassung liess den Kommentar "is it rendered at all?" ohne
         * Pruefung stehen und rief fuer jede Kachel jeder Zone GetBlitInfo --
         * auch fuer Zonen, die vollstaendig ausserhalb des Ausschnitts liegen.
         * Das Rechteck der Zone liegt in Pixelkoordinaten (qzod_map.cpp:1543:
         * cur_zone.x * 16), also im selben Raum wie WithinView.
         *
         * Rand von 16 Pixeln, weil die Marker um +6 innerhalb ihrer Kachel
         * sitzen und selbst noch Breite haben -- verworfen wird nur, was
         * sicher ausserhalb liegt. */
        if(!WithinView(i->x - 16, i->y - 16, i->w + 32, i->h + 32)) continue;

        zod_zone_sichtbar++;

        /* Ohne Wasser ist hier nichts mehr zu tun -- die uebrigen Marker
         * stecken in der Kartenflaeche. */
        if(!i->has_water) continue;

        //go through the tiles
        for(vector<map_zone_info_tile>::iterator j=i->tile.begin();j!=i->tile.end();j++)
        {
            SDL_Rect from_rect, to_rect;

            zod_zone_kacheln++;

            if(GetBlitInfo(zone_marker[0].GetBaseSurface(),
                           j->render_loc.x, j->render_loc.y, from_rect, to_rect))
            {
                zod_zone_blits++;

                if(j->is_water) zod_zone_wasser++;
                to_rect.x += shift_x;
                to_rect.y += shift_y;

                if(j->is_water)
                {
                    if(the_time > j->next_time)
                    {
                        j->bob_i = !j->bob_i;

                        j->next_time = the_time +
                                (((rand() % 300000) + 500000) *0.000001);
                    }

                    if(j->bob_i)
                        to_rect.y +=1;

                    if(zone_blits_an())
                        zone_marker_water[i->owner].BlitSurface(&from_rect, &to_rect);
                    //RenderZSurface(&zone_marker_water[i->owner], j->render_loc.x, j->render_loc.y);
                    //SDL_BlitSurface( zone_marker_water[i->owner], &from_rect, dest, &to_rect);
                }
                /* Nicht-Wasser steckt seit BakeZoneMarkers in der
                 * Kartenflaeche und wird hier NICHT mehr gezeichnet. */
                    //RenderZSurface(&zone_marker[i->owner], j->render_loc.x, j->render_loc.y);
                    //SDL_BlitSurface( zone_marker[i->owner], &from_rect, dest, &to_rect);
            }
        }
    }
}
// =========================================================
// *********************************************************
// =========================================================
void ZMap::MarkAreaStamped(int x, int y, int w, int h)
{
    int sx, sy, ex, ey;
    int tx, ty;

    MakeSureStampListExists();

    sx = x>>4;
    sy = y>>4;
    ex = (x+w)>>4;
    ey = (y+h)>>4;

    if(!((x+w)%16)) ex--;
    if(!((y+h)%16)) ey--;

    if(sx<0) sx=0;
    if(sy<0) sy=0;
    if(ex>=stamp_list_w) ex=stamp_list_w-1;
    if(ey>=stamp_list_h) ey=stamp_list_h-1;

    for(tx=sx;tx<=ex;tx++)
        for(ty=sy;ty<=ey;ty++)
            stamp_list[tx][ty] = true;
}

bool ZMap::PermStamp(int x_, int y_, ZSDL_Surface *surface, bool mark_stamped)
{
    SDL_Rect to_rect;

    if(!full_render.GetBaseSurface()) return false;

    if(!surface) return true;
    if(!surface->GetBaseSurface()) return true;

    if(mark_stamped) MarkAreaStamped(x_, y_, surface->GetBaseSurface()->w, surface->GetBaseSurface()->h);

    to_rect.x = static_cast<Sint16>(x_);
    to_rect.y = static_cast<Sint16>(y_);

    surface->BlitSurface(nullptr, &to_rect, &full_render);
    //SDL_BlitSurface( surface, nullptr, full_render, &to_rect);

    return true;
}

bool ZMap::PermStamp(int x_, int y_, SDL_Surface *surface, bool mark_stamped)
{
    SDL_Rect to_rect;

    if(!full_render.GetBaseSurface()) return false;

    if(!surface) return true;

    if(mark_stamped) MarkAreaStamped(x_, y_, surface->w, surface->h);

    to_rect.x = static_cast<Sint16>(x_);
    to_rect.y = static_cast<Sint16>(y_);

    full_render.BlitOnToMe(nullptr, &to_rect, surface);
    return true;
}

int ZMap::CoordCraterType(int tx, int ty)
{
    unsigned int tindex;
    int tile;

    if(tx >= basic_info.width) return -1;
    if(ty >= basic_info.height) return -1;
    if(tx < 0) return -1;
    if(ty < 0) return -1;

    tindex =  static_cast<uint>((ty*basic_info.width)+ tx);

    tile = tile_list[tindex].tile;
    return planet_tile_info[basic_info.terrain_type][tile].crater_type;
}

void ZMap::CreateCrater(int x, int y, bool is_big, double chance)
{
    int tx, ty;
    int tile_crater_type;

    //even pass chance?
    if(frand() > chance) return;

    //basic coord check / manipulation
    {
        if(is_big)
        {
            x-=8;
            y-=8;
        }

        tx = x>>4;
        ty = y>>4;

        if(tx >= basic_info.width) return;
        if(ty >= basic_info.height) return;
        if(tx < 0) return;
        if(ty < 0) return;

        if(is_big)
        {
            //downgrade?
            if(tx+1 >= basic_info.width) is_big = false;
            if(ty+1 >= basic_info.height) is_big = false;
        }
    }

    tile_crater_type = CoordCraterType(tx, ty);

    //big crater even exist?
    if(is_big && !ZMapCraterGraphics::CraterExists(false, basic_info.terrain_type, tile_crater_type)) is_big = false;

    //this already already stamped? (with a building?)
    {
        MakeSureStampListExists();

        if(!is_big)
        {
            if(stamp_list[tx][ty]) return;
        }
        else
        {
            vector<xy_struct> ok_points;

            if(!stamp_list[tx][ty]) ok_points.push_back(xy_struct(tx, ty));
            if(!stamp_list[tx+1][ty]) ok_points.push_back(xy_struct(tx+1, ty));
            if(!stamp_list[tx][ty+1]) ok_points.push_back(xy_struct(tx, ty+1));
            if(!stamp_list[tx+1][ty+1]) ok_points.push_back(xy_struct(tx+1, ty+1));

            if(!ok_points.size()) return;
            if(ok_points.size() < 4)
            {
                size_t choice;

                is_big = false;
                choice = static_cast<size_t>(rand()) % ok_points.size();
                tx = ok_points[choice].x;
                ty = ok_points[choice].y;
            }
        }
    }

    //all four covered tiles the same crater type?
    if(is_big)
    {
        int rct, dct, drct;

        //all following tiles are ok?
        rct = CoordCraterType(tx+1, ty);
        dct = CoordCraterType(tx, ty+1);
        drct = CoordCraterType(tx+1, ty+1);

        if(tile_crater_type != rct || tile_crater_type != dct || tile_crater_type != drct)
        {
            //can't be one uniform big crater if
            //all four squares do not have the same
            //crater type
            is_big = false;

            //can not do a big but maybe a small?
            vector<xy_struct> ok_points;

            if(ZMapCraterGraphics::CraterExists(true, basic_info.terrain_type, tile_crater_type)) ok_points.push_back(xy_struct(tx,ty));
            if(ZMapCraterGraphics::CraterExists(true, basic_info.terrain_type, rct)) ok_points.push_back(xy_struct(tx+1,ty));
            if(ZMapCraterGraphics::CraterExists(true, basic_info.terrain_type, dct)) ok_points.push_back(xy_struct(tx,ty+1));
            if(ZMapCraterGraphics::CraterExists(true, basic_info.terrain_type, drct)) ok_points.push_back(xy_struct(tx+1,ty+1));

            //no good choices at all?
            if(!ok_points.size()) return;

            //randomly choose one of the four
            //to make a small crater
            {
                size_t choice;
                choice = static_cast<size_t>(rand()) % ok_points.size();
                tx = ok_points[choice].x;
                ty = ok_points[choice].y;
            }
        }
    }

    if(ZMapCraterGraphics::CraterExists(!is_big, basic_info.terrain_type, tile_crater_type))
        PermStamp(tx<<4, ty<<4, &ZMapCraterGraphics::RandomCrater(!is_big, basic_info.terrain_type, tile_crater_type), is_big);
}
// =========================================================
// *********************************************************
// =========================================================
void ZMap::InitPathfinding()
{
    int i(0), j(0);
    int n_frei(0), n_sperr(0), n_wasser(0), n_strasse(0);

    path_finder.ResetTileInfo(basic_info.width, basic_info.height);
    for(vector<map_tile>::iterator x=tile_list.begin(); x!=tile_list.end(); x++)
    {
        if(planet_tile_info[basic_info.terrain_type][x->tile].is_passable)
        {
            n_frei++;

            if(planet_tile_info[basic_info.terrain_type][x->tile].is_water)
                n_wasser++;
            else if(planet_tile_info[basic_info.terrain_type][x->tile].is_road)
                n_strasse++;
        }
        else
            n_sperr++;

        if(planet_tile_info[basic_info.terrain_type][x->tile].is_passable)
        {
            if(planet_tile_info[basic_info.terrain_type][x->tile].is_water)
                path_finder.SetTileInfo(i, j, PF_WATER);
            else if(planet_tile_info[basic_info.terrain_type][x->tile].is_road)
                path_finder.SetTileInfo(i, j, PF_ROAD);
            else
                path_finder.SetTileInfo(i, j, PF_NORMAL);
        }
        else
            path_finder.SetTileInfo(i, j, PF_IMPASSABLE);


        i++;
        if(i>=basic_info.width)
        {
            i=0;
            j++;
        }
    }

    /* Gegenprobe fuer die Wegsuche. Sind hier 0 Kacheln begehbar, findet KEINE
     * Einheit je einen Weg -- sie bleibt an ihrem Platz und zittert, weil der
     * Client sie vorausrechnet und der Server sie zurueckholt. Genau so sah es
     * am 18.09. auf der V1200 aus, nachdem die .tileinfo im Paket fehlten.
     * Die Werte stehen in assets/planets/<planet>.tileinfo; fehlt die Datei,
     * bleibt planet_tile_info das, was zuletzt hineingeschrieben wurde. */
    ZLOG("Wegsuche-Kacheln: %d begehbar (%d Wasser, %d Strasse), %d gesperrt\n",
         n_frei, n_wasser, n_strasse, n_sperr);

    if(!n_frei)
        ZLOG("ACHTUNG: KEINE begehbare Kachel -- Einheiten koennen sich nicht "
             "bewegen. Fehlt assets/planets/*.tileinfo?\n");

    path_finder.SetTileWideWeights();
}

void ZMap::MakeSureStampListExists()
{
    if(!stamp_list_setup)
    {
        ZLOG("ZMap::MakeSureStampListExists:!stamp_list_setup\n");
        InitStampList();
    }
    else
    {
        if(basic_info.width != stamp_list_w || basic_info.height != stamp_list_h)
        {
            ZLOG("ZMap::MakeSureStampListExists:invalid stamp list!!\n");
            InitStampList();
        }
    }
}

bool ZMap::CoordStamped(int x, int y)
{
    int tx, ty;

    MakeSureStampListExists();

    tx = x>>4;
    ty = y>>4;

    if(tx<0) return false;
    if(ty<0) return false;
    if(tx>=stamp_list_w) return false;
    if(ty>=stamp_list_h) return false;

    return stamp_list[tx][ty];
}

void ZMap::InitStampList()
{
    int i, j;

    //delete if already setup
    if(stamp_list_setup) DeleteStampList();

    //we even loaded?
    if(!file_loaded) return;

    //alloc
    stamp_list = (bool**)malloc(basic_info.width * sizeof(bool *));
    for(i=0;i<basic_info.width;i++)
        stamp_list[i] = (bool*)malloc(basic_info.height * sizeof(bool));

    //set
    stamp_list_w = basic_info.width;
    stamp_list_h = basic_info.height;

    //clear the list
    for(i=0;i<basic_info.width;i++)
        for(j=0;j<basic_info.height;j++)
            stamp_list[i][j] = false;

    //set
    stamp_list_setup = true;
}

void ZMap::DeleteStampList()
{
    //dont delete it if it doesnt exist
    if(!stamp_list_setup) return;

    //dealloc
    for(int i=0;i<basic_info.width;i++)
        free(stamp_list[i]);
    free(stamp_list);

    //done
    stamp_list_setup = false;
//    stamp_list_w = -1;
//    stamp_list_h = -1;
    stamp_list_w = 0;
    stamp_list_h = 0;
}

void ZMap::InitRockList()
{
    int i, j;

    //delete if already setup
    if(rock_list_setup) DeleteRockList();

    //we even loaded?
    if(!file_loaded) return;

    //alloc
    rock_list = (bool**)malloc(basic_info.width * sizeof(bool *));
    for(i=0;i<basic_info.width;i++)
        rock_list[i] = (bool*)malloc(basic_info.height * sizeof(bool));

    //clear the list
    for(i=0;i<basic_info.width;i++)
        for(j=0;j<basic_info.height;j++)
            rock_list[i][j] = false;

    //set
    rock_list_setup = true;
}

void ZMap::DeleteRockList()
{
    //dont delete it if it doesnt exist
    if(!rock_list_setup) return;

    //dealloc
    for(int i=0;i<basic_info.width;i++)
        free(rock_list[i]);
    free(rock_list);

    //done
    rock_list_setup = false;
}

bool **ZMap::GetRockList()
{
    if(!rock_list_setup)
    {
        InitRockList();
        ZLOG("ZMap::GetRockList: had to initrocklist\n");
    }

    return rock_list;
}

void ZMap::InitSubmergeAmounts()
{
    int i, j;

    //delete if already setup
    if(submerge_info_setup) DeleteSubmergeAmounts();

    //we even loaded?
    if(!file_loaded) return;

    //alloc
    submerge_amount = (int **)malloc(basic_info.width * sizeof(int *));
    for(int i=0;i<basic_info.width;i++)
        submerge_amount[i] = (int *)malloc(basic_info.height * sizeof(int));

    //populate
    i= j= 0;
    for(vector<map_tile>::iterator x=tile_list.begin(); x!=tile_list.end(); x++)
    {
        if(planet_tile_info[basic_info.terrain_type][x->tile].is_water)
            submerge_amount[i][j] = 8;
        else
            submerge_amount[i][j] = 0;

        i++;
        if(i>=basic_info.width)
        {
            i=0;
            j++;
        }
    }

    //second run
    for(i=0;i<basic_info.width;i++)
        for(j=0;j<basic_info.height;j++)
            if(submerge_amount[i][j] == 8)
            {
                int i2, j2;

                for(i2=i-1;i2<i+3;i2++)
                {
                    if(i2<0) continue;
                    if(i2>=basic_info.width) continue;

                    for(j2=j-1;j2<j+3;j2++)
                    {
                        if(j2<0) continue;
                        if(j2>=basic_info.height) continue;

                        if(submerge_amount[i2][j2] == 0)
                        {
                            submerge_amount[i][j] = 6;
                            //escape the double loop now
                            i2=i+3;
                            break;
                        }
                    }
                }
            }

    //set
    submerge_info_setup = true;
}

void ZMap::DeleteSubmergeAmounts()
{
    //dont delete it if it doesnt exist
    if(!submerge_info_setup) return;

    //dealloc
    for(int i=0;i<basic_info.width;i++)
        free(submerge_amount[i]);
    free(submerge_amount);

    //done
    submerge_info_setup = false;
}

int ZMap::SubmergeAmount(int x, int y)
{
    int tx, ty;

    tx = x / 16;
    ty = y / 16;

    if(!submerge_info_setup) return 0;

    //checks
    if(tx < 0) return 0;
    if(ty < 0) return 0;
    if(tx >= basic_info.width) return 0;
    if(ty >= basic_info.height) return 0;

    //return path_robot_tile[tx][ty].submerge_amount;
    return submerge_amount[tx][ty];
}

void ZMap::DeletePathfindingInfo()
{
    path_finder.DeleteAllTileInfo();
}

// =========================================================
// *********************************************************
// =========================================================
bool ZMap::EngageBarrierBetweenCoords(int x1, int y1, int x2, int y2)
{
    ZPath_Finding_Bresenham bline;
    int x, y;

    if(!file_loaded) return false;

    //... nevermind if the target is a destroyable barrier
    //if(path_finder.HasDestroyableBarrier(x2>>4,y2>>4)) return false;

    //start needs checked by itself
    //if(path_finder.HasDestroyableBarrier(x1>>4,y1>>4)) return true;
    //if(path_finder.HasDestroyableBarrier(x1>>4,y1>>4)) return true;

    //forward...
    {
        bline.Init(x1>>4, y1>>4, x2>>4, y2>>4, basic_info.width, basic_info.height);

        while(bline.GetNext(x, y)) if(path_finder.HasDestroyableBarrier(x,y)) return true;
    }

    //reverse...
    {
        bline.Init(x2>>4, y2>>4, x1>>4, y1>>4, basic_info.width, basic_info.height);

        while(bline.GetNext(x, y)) if(path_finder.HasDestroyableBarrier(x,y)) return true;
    }

    return false;
}

// =========================================================
// *********************************************************
// =========================================================
void ZMap::DebugMapInfo()
{
    if(!file_loaded)
    {
        ZLOG("DebugMapInfo::map not loaded\n");
        return;
    }

    ZLOG("\nDebugMapInfo...\n");
    ZLOG("Map name:%s\n", basic_info.map_name);
    ZLOG("Map width:%d\n", basic_info.width);
    ZLOG("Map height:%d\n", basic_info.height);
    ZLOG("Map player_count:%d\n", basic_info.player_count);
    ZLOG("Map object_count:%d\n", basic_info.object_count);
    ZLOG("Map zone_count:%d\n", basic_info.zone_count);
    ZLOG("Map terrain_type:%s\n", planet_type_string[basic_info.terrain_type].c_str());

    ZLOG("\n");
}
