/* Attrappe fuer exec und dos.library auf dem Host -- siehe hostshim.h. */
#include "hostshim.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>

static LONG letzter_fehler;

APTR AllocVec(ULONG groesse, ULONG art)
{
	(void)art;

	/* Anders als auf dem Amiga liefert malloc hier genullten Speicher nicht
	 * zwangslaeufig -- genau wie dort. Also bewusst NICHT nullen, sonst
	 * verdeckt der Host einen Fehler, den der Amiga zeigen wuerde. */
	return malloc(groesse);
}

void FreeVec(APTR p)
{
	free(p);
}

BPTR Open(const char *name, LONG art)
{
	FILE *f = fopen(name, art == MODE_NEWFILE ? "wb" : "rb");

	letzter_fehler = f ? 0 : errno;

	return (BPTR)f;
}

void Close(BPTR f)
{
	if(f) fclose((FILE *)f);
}

LONG Read(BPTR f, APTR puffer, LONG n)
{
	return (LONG)fread(puffer, 1, (size_t)n, (FILE *)f);
}

LONG Write(BPTR f, APTR puffer, LONG n)
{
	return (LONG)fwrite(puffer, 1, (size_t)n, (FILE *)f);
}

LONG Seek(BPTR f, LONG pos, LONG modus)
{
	long alt = ftell((FILE *)f);
	int whence = (modus == OFFSET_BEGINNING) ? SEEK_SET
	           : (modus == OFFSET_END) ? SEEK_END : SEEK_CUR;

	if(fseek((FILE *)f, pos, whence) != 0) return -1;

	return alt;
}

LONG CreateDir(const char *name)
{
	if(mkdir(name, 0755) == 0) return 1;

	letzter_fehler = (errno == EEXIST) ? ERROR_OBJECT_EXISTS : errno;

	return 0;
}

LONG IoErr(void)
{
	return letzter_fehler;
}

/* Printf der dos.library: alle Argumente sind LONG, %ld ist eine Zahl und
 * %s ein Zeiger. Genau so ruft ZExtract es auf. */
LONG Printf(const char *fmt, ...)
{
	va_list ap;
	const char *p;

	va_start(ap, fmt);

	for(p = fmt; *p; p++)
	{
		if(*p != '%')
		{
			fputc(*p, stdout);

			continue;
		}

		p++;

		if(*p == 'l') p++;

		if(*p == 'd')      printf("%ld", va_arg(ap, LONG));
		else if(*p == 's') printf("%s", (const char *)va_arg(ap, LONG));
		else if(*p == '%') fputc('%', stdout);
		else               fputc(*p, stdout);
	}

	va_end(ap);
	fflush(stdout);

	return 0;
}
