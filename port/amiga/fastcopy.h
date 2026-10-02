#ifndef ZOD_FASTCOPY_H
#define ZOD_FASTCOPY_H
/*
 * Zeilenkopie für die heissen Pfade -- Ersatz für `memcpy`.
 *
 * WARUM NICHT `memcpy`: Es ist in dieser Werkzeugkette kein Code, sondern ein
 * Stummel um `exec.library/CopyMem` (libnix `memcpy.o`: `jsr -624(a6)` =
 * `_LVOCopyMem`, selbst nachgeprüft). Zwei Folgen:
 *
 *  1. **Ein Bibliotheksaufruf je Zeile** -- rund 1000 Sprünge ins ROM je Bild.
 *  2. **CopyMem fällt bei ungleicher Parität auf BYTEWEISES Kopieren zurück.**
 *     Nachgeprüft im Kickstart 3.1 (40.68) ab $F82CF4:
 *
 *         move.l a0,d1 / btst #0,d1 / beq
 *         move.b (a0)+,(a1)+        ; Quelle ungerade -> ein Byte angleichen
 *         move.l a1,d1 / btst #0,d1
 *         bne  $F82D52              ; ZIEL danach ungerade -> ALLES byteweise
 *
 *     Genau das trifft den Kartenhintergrund: Die Quelle wird um `shift_x`
 *     versetzt gelesen, das Ziel beginnt bei 0. Bei **ungeradem Scrollstand**
 *     -- also etwa jedem zweiten Bild -- laufen 239 760 Byte byteweise.
 *
 * Welcher der drei Wege benutzt wird, entscheidet `zod_copy_init()` anhand von
 * `ZOD_COPY` und der CPU; Einzelheiten in `fastcopy.cpp`.
 */

#include <string.h>

#ifdef __amigaos__

/* Einmal vor dem ersten Bild aufrufen (SDL_SetVideoMode tut es). */
void zod_copy_init(void);

/* Zeiger auf den gewählten Weg. Der Umweg über einen Zeiger kostet je ZEILE
 * einen indirekten Sprung -- gegenüber 540 bis 640 kopierten Byte ist das
 * nichts, und er erlaubt den A/B-Vergleich mit derselben Binärdatei. */
extern void (*zod_copy_row)(unsigned char *dst, const unsigned char *src, int n);

/* Ein ganzes Rechteck in EINEM Aufruf -- die Zeilenschleife liegt im Modul,
 * das sie ausfuehrt. Begruendung und Messzahlen in fastcopy.cpp. */
extern void (*zod_copy_rect)(unsigned char *dst, const unsigned char *src,
                             int w, int h, int dpitch, int spitch);

extern unsigned long zod_rect_calls, zod_rect_rows, zod_rect_fast;

#else

/* Auf dem Host ist memcpy echter, optimierter Code. */
static inline void zod_copy_row_host(unsigned char *d, const unsigned char *s, int n)
{
	if(n > 0) memcpy(d, s, (size_t)n);
}

static inline void zod_copy_rect_host(unsigned char *d, const unsigned char *s,
                                     int w, int h, int dp, int sp)
{
	for(int y = 0; y < h; y++, d += dp, s += sp)
		zod_copy_row_host(d, s, w);
}
#define zod_copy_row  zod_copy_row_host
#define zod_copy_rect zod_copy_rect_host
#define zod_copy_init() ((void)0)

#endif

#endif
