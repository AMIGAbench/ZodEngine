#ifndef ZX_JMP2_H
#define ZX_JMP2_H
/*
 * Leser fuer den ZWEITEN Container der Z-CD.
 *
 * Neben z.pac (Kennung "NI", zpac.py/zextract.c) traegt die CD vier weitere
 * Container im Verzeichnis z/ -- main.pac, wardata.pac, headfx.pac,
 * sheadfx.pac -- und einen INNERHALB von z.pac (DOOR.PAC). Alle fuenf haben
 * dasselbe Format, erkennbar an den letzten vier Byte: "JMP2".
 *
 * Es ist NICHT das Format von z.pac:
 *
 *              z.pac                     JMP2
 *   Verzeichnis  am Anfang, ab 0x81        am ENDE
 *   Eintrag      29 Byte, DOS-8+3          24 Byte
 *   Fuss         --                        u32 Anzahl + "JMP2"
 *   Bloecke      65 000 Byte je Block      keine, ein Strom je Eintrag
 *   Packer       LZSS, Bits LSB zuerst     LZSS, Flagbits MSB zuerst
 *
 * Eintrag (24 Byte, alles Little-Endian -- DOS-Herkunft):
 *
 *    0  char[16]  Name, mit Nullbytes aufgefuellt
 *   16  u32       Versatz der gepackten Daten, vom Dateianfang
 *   20  u32       Groesse der ENTPACKTEN Datei
 *
 * Die gepackten Daten eines Eintrags laufen bis zum Versatz des naechsten;
 * beim letzten bis zum Anfang des Verzeichnisses.
 *
 * Der Packer:
 *
 *   Flagbyte, danach acht Einheiten, OBERSTES Bit zuerst
 *     Bit 1 -> ein Literalbyte
 *     Bit 0 -> zwei Byte (a,b):  Ringindex = (b << 4) | (a >> 4)   12 Bit
 *                                Laenge    = (a & 0x0F) + 2        2..17
 *   Ring 4096 Byte, mit 0x20 vorbelegt, SCHREIBZEIGER BEGINNT BEI 1.
 *
 * Der Schreibzeiger bei 1 ist die einzige Abweichung vom Lehrbuch-LZSS und
 * war nicht zu raten -- zurueckgerechnet aus dem bekannten Klartext
 * "FORM....PBM .BMHD" am Anfang eines ILBM.
 *
 * GEGENPROBE, die das Format belegt: Das Verzeichnis nennt je Eintrag die
 * entpackte Groesse. Fuer alle 166 Eintraege der fuenf Container liefert der
 * Entpacker genau diese Zahl. Eine falsche Ringvorbelegung oder ein
 * verschobener Index wuerde das nicht schaffen.
 */

#include "zextract.h"

#define JMP2_NAME_MAX 17

struct Jmp2Eintrag
{
	char	name[JMP2_NAME_MAX];
	ULONG	roh_ab;		/* Versatz der gepackten Daten in der Datei */
	ULONG	roh_len;	/* bis zum naechsten Eintrag bzw. Verzeichnis */
	ULONG	soll;		/* entpackte Groesse laut Verzeichnis */
};

/* Verzeichnis eines JMP2-Containers lesen.
 *
 * `datei` ist der VOLLSTAENDIGE Inhalt der Containerdatei, `len` seine
 * Laenge. Die Eintraege kommen nach `aus`, hoechstens `platz` Stueck.
 *
 * Rueckgabe: Zahl der Eintraege, oder -1, wenn es kein JMP2 ist. */
LONG jmp2_verzeichnis(const UBYTE *datei, ULONG len,
                      struct Jmp2Eintrag *aus, LONG platz);

/* Einen Eintrag entpacken.
 *
 * `quelle`/`quell_len` ist der gepackte Strom (also datei + e->roh_ab),
 * `ziel` ein Puffer von mindestens `soll` Byte.
 *
 * Rueckgabe: TRUE, wenn genau `soll` Byte entstanden sind. Alles andere ist
 * ein Fehler -- eine kuerzere oder laengere Ausgabe heisst, dass der Strom
 * nicht zu diesem Eintrag gehoert. */
BOOL jmp2_entpacken(const UBYTE *quelle, ULONG quell_len,
                    UBYTE *ziel, ULONG soll);

#endif
