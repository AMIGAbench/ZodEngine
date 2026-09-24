/* ZExtract -- gemeinsame Typen fuer Programm und erzeugte Tabellen. */
#ifndef ZEXTRACT_H
#define ZEXTRACT_H

#ifdef __amigaos__
#include <exec/types.h>
#else
#include "hostshim.h"
#endif

/* XMI (Miles/AIL) aus dem z.pac in den Abspielstrom ZMU1 umsetzen.
 * Rueckgabe: Zahl der geschriebenen Stroeme, -1 bei Fehler.
 * Die Abnahme ist byteweise gegen tools/cd/xmi2zmu.py. */
LONG xmi_nach_zmu(const UBYTE *xmi, ULONG len, const char *verzeichnis,
                  const char *basis);

/* Ein Bild: welcher Satz aus SPRITES.RSC unter welchem Namen in welches
 * Archiv gehoert, und mit welcher Farbumsetzung.
 *
 * 12 Byte, also ein Vielfaches von 4. Das ist Absicht: gcc 6.5.0b laesst bei
 * Strukturen mit ungerader Groesse modulo 4 unter Umstaenden zwei Byte beim
 * Nullen aus. */
struct ZxBild
{
	ULONG	name;		/* Versatz in zx_namen */
	UWORD	satz;		/* Nummer in SPRITES.RSC */
	UWORD	w;
	UWORD	h;
	UBYTE	gruppe;		/* Index in zx_gruppen */
	UBYTE	xlat;		/* Index in zx_xlat */
};

/* Ein Klang: der Name im Verzeichnis von z.pac (DOS 8+3, aufgefuellt) und
 * die Abtastrate. 20 Byte. */
struct ZxKlang
{
	ULONG	name;		/* Versatz in zx_namen */
	ULONG	rate;
	char	dos[12];
};

extern const char		zx_namen[];
extern const struct ZxBild	zx_bilder[];
extern const struct ZxKlang	zx_klaenge[];
extern const UBYTE		zx_xlat[][256];
extern const char *const	zx_gruppen[];

extern const LONG		zx_n_bilder;
extern const LONG		zx_n_klaenge;
extern const LONG		zx_n_gruppen;
extern const LONG		zx_n_xlat;

#endif
