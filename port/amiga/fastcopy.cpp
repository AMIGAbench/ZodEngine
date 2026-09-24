/*
 * Die drei Wege, eine Zeile zu kopieren -- und die Wahl zwischen ihnen.
 *
 * Anlass ist eine Messung des Nutzers auf der V1200: Das Umschalten kostet
 * **3021 µs = 15 % der Bildzeit** (307 200 Byte, also 102 MB/s in den
 * Grafikspeicher). Damit ist es der zweitgrößte Posten nach dem Zeichnen.
 * Die 102 MB/s sind die Latte, die jede Verbesserung schlagen muss.
 *
 * **AMMX ist die Vorgabe auf dem 68080**, gemessen auf der V1200 des Nutzers:
 *
 *     Weg           zeichnen   umschalten   Durchsatz   Arbeit je Bild
 *     Langwoerter       9840         3686      83 MB/s        16638 us
 *     AMMX              7773         2451     125 MB/s        14608 us  (-12 %)
 *     MOVE16           10115         3260      94 MB/s        16848 us  (+-0)
 *
 * AMMX wirkt auf beide grossen Kopien -- Bildausgabe (307 200 Byte) und
 * Kartenhintergrund (239 760 Byte); deshalb sinkt auch `zeichnen`.
 *
 * **MOVE16 ist wieder entfernt.** Beim Umschalten sah es besser aus, die Arbeit
 * je Bild blieb aber gleich, und das liegt innerhalb der Streuung: Derselbe
 * Langwort-Weg lieferte in zwei Laeufen 3021 und 3686 us fuer dieselbe Arbeit,
 * also 22 % Unterschied. Ein Gewinn von 12 % auf einem Einzelposten ist damit
 * nicht belegbar.
 *
 * Zum Abschalten (und fuer jeden kuenftigen A/B-Vergleich):
 *
 *     SetEnv ZOD_COPY lang      Langwoerter erzwingen
 *
 * Gelesen wird über GetVar: getenv sieht unter libnix keine mit SetEnv
 * gesetzten Variablen.
 */

#include "fastcopy.h"

#ifdef __amigaos__

#include "zod_log.h"

#include <exec/execbase.h>
#include <proto/exec.h>
#include <proto/dos.h>

extern struct ExecBase *SysBase;

#ifndef AFB_68080
#define AFB_68080 10
#endif

#ifdef ZOD_AMMX
/* port/amiga/ammx_blit.s -- nur im 68080-Bau gebunden. Ein 040/060-Bau
 * enthaelt damit keinen einzigen AMMX-Opcode, auch nicht ueber diesen
 * Umweg. */
extern "C" void zod_ammx_copy_row(unsigned char *dst, const unsigned char *src,
                                  unsigned long count);
#endif

/* ---- Langwörter: der Weg, der überall läuft ------------------------------
 *
 * Ab 68020 sind schiefe Langwortlesezugriffe erlaubt. Ausgerichtet wird
 * deshalb nur das ZIEL -- dorthin wird geschrieben, und das ist der teurere
 * Zugriff. */
static void copy_lang(unsigned char *d, const unsigned char *s, int n)
{
	if(n < 16)
	{
		while(n-- > 0) *d++ = *s++;

		return;
	}

	while(((unsigned long)d & 3) && n)
	{
		*d++ = *s++;
		n--;
	}

	unsigned long *dl = (unsigned long*)d;
	const unsigned long *sl = (const unsigned long*)s;
	int vier = n >> 4;

	while(vier--)
	{
		dl[0] = sl[0];
		dl[1] = sl[1];
		dl[2] = sl[2];
		dl[3] = sl[3];
		dl += 4;
		sl += 4;
	}

	n &= 15;

	while(n >= 4)
	{
		*dl++ = *sl++;
		n -= 4;
	}

	d = (unsigned char*)dl;
	s = (const unsigned char*)sl;

	while(n-- > 0) *d++ = *s++;
}

/* ---- AMMX ---------------------------------------------------------------- */
#ifdef ZOD_AMMX
static void copy_ammx(unsigned char *d, const unsigned char *s, int n)
{
	if(n > 0) zod_ammx_copy_row(d, s, (unsigned long)n);
}
#endif

void (*zod_copy_row)(unsigned char *, const unsigned char *, int) = copy_lang;

void zod_copy_init(void)
{
	static int geschehen = 0;

	if(geschehen) return;

	geschehen = 1;

	char wahl[16];
	const int hat_080 = (SysBase->AttnFlags & (1 << AFB_68080)) != 0;

	if(GetVar((CONST_STRPTR)"ZOD_COPY", (STRPTR)wahl, sizeof(wahl), 0) <= 0)
		wahl[0] = 0;

#ifdef ZOD_AMMX
	/* Vorgabe auf dem 68080. Nur "lang" schaltet ausdruecklich zurueck. */
	if(hat_080 && wahl[0] != 'l' && wahl[0] != 'L')
	{
		zod_copy_row = copy_ammx;
		ZLOG("Kopierweg: AMMX (8 Byte je Befehl)\n");

		return;
	}
#else
	(void)hat_080;
#endif

	zod_copy_row = copy_lang;
	ZLOG("Kopierweg: Langwoerter\n");
}

#endif
