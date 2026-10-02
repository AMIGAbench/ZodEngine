#ifndef ZOD_MAPFILE_H
#define ZOD_MAPFILE_H
/*
 * Kartendateien (.map) und .tileinfo im Originalformat lesen und schreiben.
 *
 * Das Original schreibt die Structs roh per fwrite. Zwei Fallen auf m68k:
 *  - Byte-Reihenfolge (Little-Endian im Format),
 *  - Ausrichtung: map_object ist auf x86 16 Byte gross (int auf 4 ausgerichtet),
 *    auf m68k nur 14 (int auf 2). Deshalb feste Offsets statt memcpy.
 *
 * Die Schicht ist bewusst frei von Grafik- und Netzcode, damit sie sich auf
 * dem Host gegen alle Originalkarten testen laesst (tests/host/mapfile_test).
 */
#include <vector>

#include <lib_qZod_DnMap/zmap_structures_old.h>

/* Groessen im Dateiformat (x86-Layout), nicht sizeof() der Structs! */
#define ZOD_MAPFILE_BASICS_SIZE 62
#define ZOD_MAPFILE_ZONE_SIZE    8
#define ZOD_MAPFILE_OBJECT_SIZE 16
#define ZOD_MAPFILE_TILE_SIZE    2

/* erwartete Dateigroesse fuer einen Kopf */
int zod_mapfile_size(const map_basics &basics);

/* Rohdaten -> Strukturen. Gibt 0 zurueck, wenn die Daten nicht passen. */
int zod_mapfile_parse(const char *data, int size,
                      map_basics &basics,
                      std::vector<map_zone> &zones,
                      std::vector<map_object> &objects,
                      std::vector<map_tile> &tiles);

/* Strukturen -> Rohdaten. Liefert die geschriebene Laenge oder 0. */
int zod_mapfile_serialize(const map_basics &basics,
                          const std::vector<map_zone> &zones,
                          const std::vector<map_object> &objects,
                          const std::vector<map_tile> &tiles,
                          char *out, int out_cap);

/* .tileinfo: gepackt, nur die beiden 16-Bit-Felder brauchen den Tausch.
 * Wird nach dem Lesen und vor dem Schreiben aufgerufen (selbstinvers). */
void zod_tileinfo_swap(palette_tile_info *entries, int count);
void zod_tileinfo_new_swap(palette_tile_info_new *entries, int count);

#endif
