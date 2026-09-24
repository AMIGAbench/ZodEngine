#ifndef ZX_LBM_H
#define ZX_LBM_H
/*
 * ILBM/PBM-Leser fuer die Vollbilder der Z-CD.
 *
 * IFF, also BIG-Endian -- anders als alles andere auf dieser CD. Das ist
 * kein Zufall: das Format stammt vom Amiga, und Deluxe Paint hat es auf den
 * PC mitgebracht.
 *
 *   FORM <u32 Laenge> <Typ>            Typ = "ILBM" oder "PBM "
 *     BMHD  u16 w, u16 h, i16 x, i16 y,
 *           u8 Ebenen, u8 Maske, u8 Packung, u8 Fuellung,
 *           u16 durchsichtige Farbe, u8 xAspekt, u8 yAspekt,
 *           i16 Seitenbreite, i16 Seitenhoehe
 *     CMAP  drei Byte je Farbe
 *     BODY  die Bilddaten
 *
 * ZWEI FORMEN, und der Unterschied ist der ganze Grund fuer dieses Modul:
 *
 *   PBM     chunky -- ein Byte je Bildpunkt, zeilenweise. So liegen die
 *           Bilder aus DOOR.PAC.
 *   ILBM    planar -- je Zeile erst alle Bits der Ebene 0, dann Ebene 1 und
 *           so fort. Ein Bildpunkt steht also in 8 verschiedenen Bytes. So
 *           liegen die Lade- und Statistikbilder aus main.pac.
 *
 * Packung 1 ist ByteRun1: ein Steuerbyte n, dann
 *   n <  128   die naechsten n+1 Byte woertlich
 *   n >  128   das naechste Byte 257-n mal
 *   n == 128   nichts (Fuellung)
 *
 * WARUM DIE LAENGE JEDER ZEILE AUFGERUNDET WIRD: Eine Bitebene belegt je
 * Zeile immer eine gerade Zahl von Byte ((w+15)/16*2). Bei 320 Punkten geht
 * das glatt auf; bei 322 waeren es 42 statt 41. Wer das vergisst, bekommt
 * ein Bild, das sich Zeile fuer Zeile schraeg zieht -- und genau so sieht
 * auch ein um ein Byte verschobener Satz aus SPRITES.RSC aus, was dieses
 * Projekt schon einmal teuer bezahlt hat.
 */

#include "zextract.h"

struct LbmBild
{
	UWORD	w;
	UWORD	h;
	UBYTE	ebenen;
	UBYTE	palette[768];	/* aus CMAP, auf 8 Bit gebracht */
	UWORD	farben;		/* wie viele davon wirklich aus CMAP kamen */
	UBYTE	*pixel;		/* w * h Byte, ein Index je Punkt */
};

/* Ein FORM/ILBM oder FORM/PBM auswerten.
 *
 * `pixel` muss auf einen Puffer von mindestens `platz` Byte zeigen; gebraucht
 * werden w*h. Reicht er nicht, schlaegt der Aufruf fehl, statt zu schreiben.
 *
 * Rueckgabe: TRUE bei Erfolg. */
BOOL lbm_lesen(const UBYTE *daten, ULONG len, struct LbmBild *aus,
               UBYTE *pixel, ULONG platz);

#endif
