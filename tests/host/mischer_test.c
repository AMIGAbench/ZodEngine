/*
 * mischer_test -- den Amiga-Musikmischer auf dem Host pruefen.
 *
 * Host-first, wie alles in diesem Projekt: Der Mischer ist gewoehnliche
 * Ganzzahlarithmetik und liest sein Eingabeformat ausdruecklich
 * big-endian. Er laeuft deshalb unveraendert auf x86 -- und dort kann man
 * sich das Ergebnis ANHOEREN, statt es auf dem Amiga zu erraten.
 *
 *   mischer_test <bank> <stueck.zmu> <ziel.wav> [sekunden] [rate] [stimmen]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../port/amiga/musik_mischer.h"

static unsigned char *datei(const char *name, unsigned long *len)
{
	FILE *f = fopen(name, "rb");
	if(!f) return NULL;
	fseek(f, 0, SEEK_END);
	long n = ftell(f);
	fseek(f, 0, SEEK_SET);
	unsigned char *p = malloc(n);
	if(!p || fread(p, 1, n, f) != (size_t)n) { free(p); fclose(f); return NULL; }
	fclose(f);
	*len = (unsigned long)n;
	return p;
}

static void le32(unsigned char *p, unsigned long v)
{ p[0]=v; p[1]=v>>8; p[2]=v>>16; p[3]=v>>24; }

int main(int argc, char **argv)
{
	if(argc < 4)
	{
		fprintf(stderr, "Aufruf: %s <bank> <stueck.zmu> <ziel.wav>"
		                " [sekunden] [rate] [stimmen]\n", argv[0]);
		return 2;
	}

	unsigned long sek     = (argc > 4) ? strtoul(argv[4], NULL, 10) : 40;
	unsigned long rate    = (argc > 5) ? strtoul(argv[5], NULL, 10) : 22050;
	int           stimmen = (argc > 6) ? atoi(argv[6]) : 24;

	unsigned long bl = 0, zl = 0;
	unsigned char *bank = datei(argv[1], &bl);
	unsigned char *zmu  = datei(argv[2], &zl);

	if(!bank || !zmu) { fprintf(stderr, "Datei nicht lesbar\n"); return 1; }

	int n = mischer_init(bank, bl, rate, stimmen);

	if(!n) { fprintf(stderr, "Bank unbrauchbar\n"); return 1; }

	if(!mischer_stueck(zmu, zl)) { fprintf(stderr, "Stueck unbrauchbar\n"); return 1; }

	/* Siebtes Argument: an diese Sekunde springen, bevor gemischt wird. */
	if(argc > 7)
	{
		unsigned long ab = strtoul(argv[7], NULL, 10);

		mischer_springen((ULONG)(ab * 120));
		printf("gesprungen auf %lu s\n", ab);
	}

	printf("Bank: %d Instrumente, %lu Hz, %d Stimmen\n", n, rate, stimmen);

	unsigned long rahmen_gesamt = rate * sek;
	WORD *puffer = malloc(rahmen_gesamt * 2 * sizeof(WORD));

	if(!puffer) { fprintf(stderr, "kein Speicher\n"); return 1; }

	/* In Bloecken fuellen, genau wie es die Ausgabe spaeter tut -- ein
	 * einziger Riesenaufruf wuerde den Blockweg nicht pruefen. */
	const unsigned long block = 512;
	unsigned long getan = 0;

	while(getan < rahmen_gesamt)
	{
		unsigned long r = rahmen_gesamt - getan;
		if(r > block) r = block;
		mischer_fuellen(puffer + getan * 2, r);
		getan += r;
	}

	ULONG noten, spitze, verdraengt, ohne;
	ULONG gemeldet_geklemmt;
	mischer_zahlen(&noten, &spitze, &verdraengt, &ohne, &gemeldet_geklemmt);

	printf("%lu Noten, Spitze %lu Stimmen, %lu Verdraengungen, %lu ohne Instrument, %lu begrenzt\n",
	       (unsigned long)noten, (unsigned long)spitze,
	       (unsigned long)verdraengt, (unsigned long)ohne,
	       (unsigned long)gemeldet_geklemmt);

	/* Spitzenwert melden -- Uebersteuerung waere hoerbar und muss auffallen */
	long spitzenwert = 0;
	unsigned long geklemmt = 0;

	for(unsigned long i = 0; i < rahmen_gesamt * 2; i++)
	{
		long v = puffer[i];
		if(v < 0) v = -v;
		if(v > spitzenwert) spitzenwert = v;
		if(puffer[i] == 32767 || puffer[i] == -32768) geklemmt++;
	}

	printf("Spitzenpegel %ld von 32767, %lu Werte am Anschlag\n",
	       spitzenwert, geklemmt);

	FILE *w = fopen(argv[3], "wb");
	if(!w) { fprintf(stderr, "Ziel nicht schreibbar\n"); return 1; }

	unsigned long daten = rahmen_gesamt * 2 * 2;
	unsigned char kopf[44];
	memcpy(kopf, "RIFF", 4);       le32(kopf+4, 36 + daten);
	memcpy(kopf+8, "WAVEfmt ", 8); le32(kopf+16, 16);
	kopf[20]=1; kopf[21]=0; kopf[22]=2; kopf[23]=0;
	le32(kopf+24, rate);           le32(kopf+28, rate*4);
	kopf[32]=4; kopf[33]=0; kopf[34]=16; kopf[35]=0;
	memcpy(kopf+36, "data", 4);    le32(kopf+40, daten);
	fwrite(kopf, 1, 44, w);
	fwrite(puffer, 1, daten, w);
	fclose(w);

	printf("geschrieben: %s (%lu s stereo)\n", argv[3], sek);

	return 0;
}
