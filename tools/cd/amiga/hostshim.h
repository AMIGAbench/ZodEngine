/*
 * Attrappe fuer exec und dos.library, damit ZExtract auf dem Host laeuft.
 *
 * Warum: Der Amiga-Extraktor soll bitgleiche Archive liefern wie der
 * Linux-Extraktor. Das laesst sich mit `cmp` beweisen -- aber nur, wenn man
 * denselben C-Quelltext auch auf dem Host ausfuehren kann. Ein Emulatorlauf
 * kostet Minuten und liefert nur Zahlen; hier laeuft es in Sekunden, unter
 * AddressSanitizer und mit `cmp` als Urteil.
 *
 * Es wird NICHTS am Verhalten geschoent: gelesen, gerechnet und geschrieben
 * wird von demselben Code. Ersetzt sind nur Datei- und Speicherzugriff.
 *
 * LONG ist hier absichtlich `long` (64 Bit), nicht 32 Bit: Das Programm
 * uebergibt Zeiger als LONG an Printf, wie es die Amiga-Konvention verlangt.
 * Mit 32-Bit-LONG wuerden sie auf dem Host abgeschnitten.
 */
#ifndef ZX_HOSTSHIM_H
#define ZX_HOSTSHIM_H

#include <stddef.h>

typedef unsigned char	UBYTE;
typedef signed char	BYTE;
typedef unsigned short	UWORD;
typedef unsigned long	ULONG;
typedef long		LONG;
typedef short		WORD;
typedef void		*APTR;
typedef void		*BPTR;
typedef int		BOOL;

#ifndef TRUE
#define TRUE	1
#define FALSE	0
#endif

#define MEMF_ANY		0
#define MODE_OLDFILE		1005
#define MODE_NEWFILE		1006
#define OFFSET_BEGINNING	(-1)
#define OFFSET_CURRENT		0
#define OFFSET_END		1

#define RETURN_OK	0
#define RETURN_WARN	5
#define RETURN_ERROR	10
#define RETURN_FAIL	20

#define ERROR_OBJECT_EXISTS	203

APTR	AllocVec(ULONG groesse, ULONG art);
void	FreeVec(APTR p);

BPTR	Open(const char *name, LONG art);
void	Close(BPTR f);
LONG	Read(BPTR f, APTR puffer, LONG n);
LONG	Write(BPTR f, APTR puffer, LONG n);
LONG	Seek(BPTR f, LONG pos, LONG modus);
LONG	CreateDir(const char *name);
LONG	IoErr(void);
LONG	Printf(const char *fmt, ...);

#endif
