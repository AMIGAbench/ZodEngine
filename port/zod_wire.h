#ifndef ZOD_WIRE_H
#define ZOD_WIRE_H
/*
 * Drahtformat und Dateiformat sind Little-Endian (x86-Layout des Originals).
 * Auf Big-Endian-Zielen (m68k) wird beim Senden/Empfangen feldweise getauscht,
 * damit ein Amiga gegen unveraenderte PC-Clients und -Server spielen kann.
 *
 * Alle Paket-Structs liegen im Original in #pragma pack(1), die Feld-Offsets
 * sind auf m68k daher identisch -- es fehlt nur die Byte-Reihenfolge.
 * Ausnahme sind die Map-Datei-Structs (nicht gepackt): die werden feldweise
 * mit den zod_rd_- und zod_wr_-Helfern gelesen und geschrieben.
 *
 * -DZOD_WIRE_SWAPTEST erzwingt den Tauschpfad auch auf x86: damit laesst sich
 * die Big-Endian-Seite auf dem Host testen (zwei Swaptest-Builds ergeben
 * denselben Bytestrom wie zwei normale Builds).
 */

#if defined(ZOD_WIRE_SWAPTEST)
#  define ZOD_WIRE_SWAP 1
#elif defined(__BYTE_ORDER__) && defined(__ORDER_BIG_ENDIAN__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
#  define ZOD_WIRE_SWAP 1
#elif defined(__BIG_ENDIAN__) || defined(__m68k__) || defined(__AMIGA__)
#  define ZOD_WIRE_SWAP 1
#else
#  define ZOD_WIRE_SWAP 0
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* 1 = Hostreihenfolge weicht vom Drahtformat ab (oder Swaptest-Build) */
int zod_wire_swaps(void);

/* Tabelle aufbauen; vor dem ersten Paket aufrufen (wird sonst nachgeholt) */
void zod_wire_init(void);

/* Nutzlast eines Pakets zwischen Host- und Drahtreihenfolge wandeln.
 * to_wire = 1 vor dem Senden, 0 nach dem Empfangen. Ohne Eintrag in der
 * Tabelle (Text, Rohdaten) bleibt der Puffer unveraendert. */
void zod_wire_swap_payload(int pack_id, char *data, int size, int to_wire);

/* Little-Endian-Zugriffe, byteweise: unabhaengig von Ausrichtung und
 * Hostreihenfolge (Map-Dateien enthalten unausgerichtete Felder). */
unsigned short zod_rd_le16(const void *p);
unsigned int zod_rd_le32(const void *p);
float zod_rd_lef32(const void *p);
double zod_rd_lef64(const void *p);
void zod_wr_le16(void *p, unsigned short v);
void zod_wr_le32(void *p, unsigned int v);
void zod_wr_lef32(void *p, float v);
void zod_wr_lef64(void *p, double v);

#ifdef __cplusplus
}
#endif

#endif
