#include "zod_mapfile.h"

#include <string.h>

#include "zod_wire.h"

using std::vector;

/* Offsets im Dateiformat (x86-Layout mit Fuellbytes) */
#define OFF_BASICS_WIDTH        0
#define OFF_BASICS_HEIGHT       2
#define OFF_BASICS_NAME         4
#define OFF_BASICS_PLAYERS     54
/* 55: Fuellbyte */
#define OFF_BASICS_OBJECTS     56
#define OFF_BASICS_TERRAIN     58
/* 59: Fuellbyte */
#define OFF_BASICS_ZONES       60

#define OFF_OBJ_X               0
#define OFF_OBJ_Y               2
#define OFF_OBJ_OWNER           4
#define OFF_OBJ_TYPE            5
#define OFF_OBJ_ID              6
#define OFF_OBJ_BLEVEL          7
#define OFF_OBJ_EXTRA_LINKS     8
/* 10, 11: Fuellbytes */
#define OFF_OBJ_HEALTH         12

int zod_mapfile_size(const map_basics &basics)
{
	return ZOD_MAPFILE_BASICS_SIZE
	     + basics.zone_count * ZOD_MAPFILE_ZONE_SIZE
	     + basics.object_count * ZOD_MAPFILE_OBJECT_SIZE
	     + basics.width * basics.height * ZOD_MAPFILE_TILE_SIZE;
}

int zod_mapfile_parse(const char *data, int size,
                      map_basics &basics,
                      vector<map_zone> &zones,
                      vector<map_object> &objects,
                      vector<map_tile> &tiles)
{
	int i, tile_count, need;

	if(!data || size < ZOD_MAPFILE_BASICS_SIZE) return 0;

	basics.width        = zod_rd_le16(data + OFF_BASICS_WIDTH);
	basics.height       = zod_rd_le16(data + OFF_BASICS_HEIGHT);
	memcpy(basics.map_name, data + OFF_BASICS_NAME, sizeof(basics.map_name));
	basics.map_name[sizeof(basics.map_name) - 1] = 0;
	basics.player_count = (unsigned char)data[OFF_BASICS_PLAYERS];
	basics.object_count = zod_rd_le16(data + OFF_BASICS_OBJECTS);
	basics.terrain_type = (unsigned char)data[OFF_BASICS_TERRAIN];
	basics.zone_count   = zod_rd_le16(data + OFF_BASICS_ZONES);

	tile_count = basics.width * basics.height;
	need = zod_mapfile_size(basics);
	if(size < need) return 0;

	data += ZOD_MAPFILE_BASICS_SIZE;

	zones.clear();
	zones.reserve(basics.zone_count);

	for(i = 0; i < basics.zone_count; i++)
	{
		map_zone z;

		z.x = zod_rd_le16(data + 0);
		z.y = zod_rd_le16(data + 2);
		z.w = zod_rd_le16(data + 4);
		z.h = zod_rd_le16(data + 6);
		zones.push_back(z);

		data += ZOD_MAPFILE_ZONE_SIZE;
	}

	objects.clear();
	objects.reserve(basics.object_count);
	for(i = 0; i < basics.object_count; i++)
	{
		map_object o;

		o.x             = zod_rd_le16(data + OFF_OBJ_X);
		o.y             = zod_rd_le16(data + OFF_OBJ_Y);
		o.owner         = data[OFF_OBJ_OWNER];
		o.object_type   = (unsigned char)data[OFF_OBJ_TYPE];
		o.object_id     = (unsigned char)data[OFF_OBJ_ID];
		o.blevel        = data[OFF_OBJ_BLEVEL];
		o.extra_links   = zod_rd_le16(data + OFF_OBJ_EXTRA_LINKS);
		o.health_percent = (int)zod_rd_le32(data + OFF_OBJ_HEALTH);
		objects.push_back(o);

		data += ZOD_MAPFILE_OBJECT_SIZE;
	}

	tiles.clear();
	tiles.reserve(tile_count);
	for(i = 0; i < tile_count; i++)
	{
		map_tile t;

		t.tile = zod_rd_le16(data);
		tiles.push_back(t);

		data += ZOD_MAPFILE_TILE_SIZE;
	}

	return 1;
}

int zod_mapfile_serialize(const map_basics &basics,
                          const vector<map_zone> &zones,
                          const vector<map_object> &objects,
                          const vector<map_tile> &tiles,
                          char *out, int out_cap)
{
	char *p = out;
	size_t i;
	int need = ZOD_MAPFILE_BASICS_SIZE
	         + (int)zones.size() * ZOD_MAPFILE_ZONE_SIZE
	         + (int)objects.size() * ZOD_MAPFILE_OBJECT_SIZE
	         + (int)tiles.size() * ZOD_MAPFILE_TILE_SIZE;

	if(!out || out_cap < need) return 0;

	/* Fuellbytes auf 0, wie sie die Originaldateien enthalten */
	memset(p, 0, ZOD_MAPFILE_BASICS_SIZE);
	zod_wr_le16(p + OFF_BASICS_WIDTH, basics.width);
	zod_wr_le16(p + OFF_BASICS_HEIGHT, basics.height);
	memcpy(p + OFF_BASICS_NAME, basics.map_name, sizeof(basics.map_name));
	p[OFF_BASICS_PLAYERS] = (char)basics.player_count;
	zod_wr_le16(p + OFF_BASICS_OBJECTS, basics.object_count);
	p[OFF_BASICS_TERRAIN] = (char)basics.terrain_type;
	zod_wr_le16(p + OFF_BASICS_ZONES, basics.zone_count);
	p += ZOD_MAPFILE_BASICS_SIZE;

	for(i = 0; i < zones.size(); i++)
	{
		zod_wr_le16(p + 0, zones[i].x);
		zod_wr_le16(p + 2, zones[i].y);
		zod_wr_le16(p + 4, zones[i].w);
		zod_wr_le16(p + 6, zones[i].h);
		p += ZOD_MAPFILE_ZONE_SIZE;
	}

	for(i = 0; i < objects.size(); i++)
	{
		memset(p, 0, ZOD_MAPFILE_OBJECT_SIZE);
		zod_wr_le16(p + OFF_OBJ_X, objects[i].x);
		zod_wr_le16(p + OFF_OBJ_Y, objects[i].y);
		p[OFF_OBJ_OWNER]  = objects[i].owner;
		p[OFF_OBJ_TYPE]   = (char)objects[i].object_type;
		p[OFF_OBJ_ID]     = (char)objects[i].object_id;
		p[OFF_OBJ_BLEVEL] = objects[i].blevel;
		zod_wr_le16(p + OFF_OBJ_EXTRA_LINKS, objects[i].extra_links);
		zod_wr_le32(p + OFF_OBJ_HEALTH, (unsigned int)objects[i].health_percent);
		p += ZOD_MAPFILE_OBJECT_SIZE;
	}

	for(i = 0; i < tiles.size(); i++)
	{
		zod_wr_le16(p, tiles[i].tile);
		p += ZOD_MAPFILE_TILE_SIZE;
	}

	return (int)(p - out);
}

static void swap16_at(void *p)
{
	unsigned char *b = (unsigned char*)p;
	unsigned char t = b[0];

	b[0] = b[1];
	b[1] = t;
}

void zod_tileinfo_swap(palette_tile_info *entries, int count)
{
	int i;

	if(!zod_wire_swaps() || !entries) return;

	for(i = 0; i < count; i++)
	{
		swap16_at(&entries[i].next_tile_in_effect);
		swap16_at(&entries[i].crater_type);
	}
}

void zod_tileinfo_new_swap(palette_tile_info_new *entries, int count)
{
	int i;

	if(!zod_wire_swaps() || !entries) return;

	for(i = 0; i < count; i++)
	{
		swap16_at(&entries[i].next_tile_in_effect);
		swap16_at(&entries[i].crater_type);
	}
}
