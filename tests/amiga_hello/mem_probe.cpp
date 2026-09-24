/*
 * Winziges Messprogramm: meldet den freien Speicher über den seriellen Kanal.
 *
 * Zweck: Die Engine misst ihren eigenen Speicherstand in einem atexit-Behandler
 * und damit VOR dem Aufräumen der C-Laufzeit. Ob beim Beenden wirklich alles
 * zurückgegeben wird, lässt sich deshalb nur von außen feststellen -- der
 * Autostart ruft dieses Programm vor und nach dem Spiel auf.
 *
 * Aufruf: mem_probe <text>   (der Text erscheint in der Meldung)
 */
#include <cstdio>

#include <exec/memory.h>
#include <proto/exec.h>

#include "debug.h"

int main(int argc, char **argv)
{
	const char *label = argc > 1 ? argv[1] : "";
	unsigned long total = AvailMem(MEMF_ANY);
	unsigned long largest = AvailMem(MEMF_ANY | MEMF_LARGEST);

	dbg_printf("MEM %s: %lu KB frei, groesster Block %lu KB\n",
	           label, total / 1024, largest / 1024);
	printf("MEM %s: %lu KB frei, groesster Block %lu KB\n",
	       label, total / 1024, largest / 1024);

	return 0;
}
