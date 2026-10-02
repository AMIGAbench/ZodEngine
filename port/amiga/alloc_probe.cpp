/*
 * DBG-Messsonde: malloc/free/realloc/calloc ueber den Linker abfangen
 * (-Wl,--wrap=malloc -Wl,--wrap=free -Wl,--wrap=realloc -Wl,--wrap=calloc).
 * Gehoert nach port/amiga/alloc_probe.cpp, dazu:
 *   Makefile (amiga-Zweig): ENGINE_SRCS += port/amiga/alloc_probe.cpp
 *                           LDFLAGS += -Wl,--wrap=malloc ... (siehe oben)
 *   main.cpp:    alle 15 s  zod_alloc_report("takt")
 *   zserver.cpp: LoadNextMap zod_alloc_report("kartenstart")
 *
 * Kein ZLOG/printf innerhalb der Wrapper -- das riefe wieder malloc.
 * realloc/calloc laufen bewusst ueber die eigenen Wrapper und NICHT ueber
 * __real_realloc/__real_calloc: sonst kaeme dort ein zweiter Kopf obendrauf
 * (die Bibliothek ruft intern das ebenfalls abgefangene malloc).
 */
#include <stdlib.h>
#include <string.h>

#include "zod_log.h"

#ifdef __amigaos__
#include <exec/memory.h>
#include <proto/exec.h>
#endif

extern "C" {

void *__real_malloc(size_t n);
void  __real_free(void *p);

void *__wrap_malloc(size_t n);
void  __wrap_free(void *p);
void *__wrap_realloc(void *p, size_t n);
void *__wrap_calloc(size_t n, size_t s);

void zod_alloc_report(const char *tag);

}

#define ZAP_MAGIC 0x5A4F4441UL   /* "ZODA" */
#define ZAP_HDR   8

static unsigned long zap_live_bytes;     /* lebende, angeforderte Bytes */
static unsigned long zap_live_count;     /* lebende Belegungen */
static unsigned long zap_n_malloc;       /* Aufrufe seit dem letzten Bericht */
static unsigned long zap_n_free;
static unsigned long zap_max_single;     /* groesste Einzelbelegung (je) */
static unsigned long zap_max_live;       /* hoechster Stand lebender Bytes */
static unsigned long zap_foreign_free;   /* free() auf fremdem Zeiger */
static unsigned long zap_fail;           /* fehlgeschlagene Belegungen */
/* Groessenklassen der Aufrufe seit dem letzten Bericht:
 * <=32, <=128, <=512, <=2K, <=8K, <=32K, <=128K, groesser */
static unsigned long zap_bucket[8];

/* Grosse Belegungen (>32 KB) seit dem letzten Bericht, nach Groesse gezaehlt.
 * Die Groesse verraet, was belegt wurde (eine 256x256-Flaeche in 16 Bit sind
 * genau 128 KB). Gesammelt werden bis zu 24 verschiedene Groessen. */
#define ZAP_BIG_SLOTS 24
#define ZAP_BIG_MIN   (32UL * 1024UL)

static unsigned long zap_big_size[ZAP_BIG_SLOTS];
static unsigned long zap_big_count[ZAP_BIG_SLOTS];
static unsigned long zap_big_other;

static void zap_note_big(unsigned long n)
{
	if(n < ZAP_BIG_MIN) return;

	for(int i = 0; i < ZAP_BIG_SLOTS; i++)
	{
		if(zap_big_count[i] && zap_big_size[i] != n) continue;

		zap_big_size[i]  = n;
		zap_big_count[i]++;
		return;
	}

	zap_big_other++;
}

static int zap_class(unsigned long n)
{
	if(n <= 32UL) return 0;
	if(n <= 128UL) return 1;
	if(n <= 512UL) return 2;
	if(n <= 2048UL) return 3;
	if(n <= 8192UL) return 4;
	if(n <= 32768UL) return 5;
	if(n <= 131072UL) return 6;
	return 7;
}

void *__wrap_malloc(size_t n)
{
	unsigned char *raw = (unsigned char *)__real_malloc((size_t)(n + ZAP_HDR));

	zap_note_big((unsigned long)n);

	if(!raw) { zap_fail++; return 0; }

	((unsigned long *)raw)[0] = ZAP_MAGIC;
	((unsigned long *)raw)[1] = (unsigned long)n;

	zap_live_bytes += (unsigned long)n;
	zap_live_count++;
	zap_n_malloc++;
	zap_bucket[zap_class((unsigned long)n)]++;

	if((unsigned long)n > zap_max_single) zap_max_single = (unsigned long)n;
	if(zap_live_bytes > zap_max_live) zap_max_live = zap_live_bytes;

	return raw + ZAP_HDR;
}

void __wrap_free(void *p)
{
	if(!p) return;

	unsigned char *raw = (unsigned char *)p - ZAP_HDR;

	if(((unsigned long *)raw)[0] != ZAP_MAGIC)
	{
		/* Nicht ueber unser malloc gekommen (Bibliotheksinterna) --
		 * unveraendert durchreichen, sonst zerstoert man den Haufen. */
		zap_foreign_free++;
		__real_free(p);
		return;
	}

	unsigned long n = ((unsigned long *)raw)[1];

	((unsigned long *)raw)[0] = 0;   /* Magie loeschen: doppeltes free faellt auf */

	if(zap_live_bytes >= n) zap_live_bytes -= n; else zap_live_bytes = 0;
	if(zap_live_count) zap_live_count--;
	zap_n_free++;

	__real_free(raw);
}

void *__wrap_calloc(size_t n, size_t s)
{
	size_t total = (size_t)(n * s);
	void *p = __wrap_malloc(total);

	if(p) memset(p, 0, total);

	return p;
}

void *__wrap_realloc(void *p, size_t n)
{
	if(!p) return __wrap_malloc(n);

	if(n == 0) { __wrap_free(p); return 0; }

	unsigned char *raw = (unsigned char *)p - ZAP_HDR;

	if(((unsigned long *)raw)[0] != ZAP_MAGIC)
	{
		zap_foreign_free++;
		return 0;
	}

	unsigned long old = ((unsigned long *)raw)[1];
	void *np = __wrap_malloc(n);

	if(!np) return 0;

	memcpy(np, p, (size_t)(old < (unsigned long)n ? old : (unsigned long)n));
	__wrap_free(p);

	return np;
}

extern "C" void zod_roto_stats(long *count, long *kb);

void zod_alloc_report(const char *tag)
{
	unsigned long freemem = 0, largest = 0;
	long roto_n = 0, roto_kb = 0;

	zod_roto_stats(&roto_n, &roto_kb);

#ifdef __amigaos__
	freemem = AvailMem(MEMF_ANY);
	largest = AvailMem(MEMF_ANY | MEMF_LARGEST);
#endif

	ZLOG("ALLOC %s leben=%ld KB n=%ld malloc=%ld free=%ld max1=%ld KB hoch=%ld KB fremd=%ld fehl=%ld frei=%ld KB blk=%ld KB\n",
	     tag ? tag : "",
	     (long)(zap_live_bytes / 1024), (long)zap_live_count,
	     (long)zap_n_malloc, (long)zap_n_free,
	     (long)(zap_max_single / 1024), (long)(zap_max_live / 1024),
	     (long)zap_foreign_free, (long)zap_fail,
	     (long)(freemem / 1024), (long)(largest / 1024));

	ZLOG("ALLOCR %s dreh=%ld Flaechen, %ld KB\n", tag ? tag : "", roto_n, roto_kb);

	ZLOG("ALLOCK %s 32=%ld 128=%ld 512=%ld 2K=%ld 8K=%ld 32K=%ld 128K=%ld gr=%ld\n",
	     tag ? tag : "",
	     (long)zap_bucket[0], (long)zap_bucket[1], (long)zap_bucket[2],
	     (long)zap_bucket[3], (long)zap_bucket[4], (long)zap_bucket[5],
	     (long)zap_bucket[6], (long)zap_bucket[7]);

	/* die grossen Belegungen des Abschnitts, groesste zuerst */
	{
		char line[256];
		int used = 0;

		line[0] = 0;

		for(int round = 0; round < 6; round++)
		{
			int best = -1;

			for(int i = 0; i < ZAP_BIG_SLOTS; i++)
				if(zap_big_count[i] && (best < 0 || zap_big_size[i] > zap_big_size[best]))
					best = i;

			if(best < 0) break;

			//eigene Zahlenausgabe: kein snprintf im Berichtsweg noetig
			unsigned long kb = zap_big_size[best] / 1024;
			unsigned long cnt = zap_big_count[best];

			if(used < 200)
			{
				char tmp[48];
				int  len = 0;

				//"<kb>Kx<anzahl> "
				unsigned long v = kb;
				char digits[12];
				int  d = 0;

				do { digits[d++] = (char)('0' + (v % 10)); v /= 10; } while(v);
				while(d) tmp[len++] = digits[--d];
				tmp[len++] = 'K';
				tmp[len++] = 'x';

				v = cnt; d = 0;
				do { digits[d++] = (char)('0' + (v % 10)); v /= 10; } while(v);
				while(d) tmp[len++] = digits[--d];
				tmp[len++] = ' ';
				tmp[len] = 0;

				for(int k = 0; k < len && used < 200; k++) line[used++] = tmp[k];

				line[used] = 0;
			}

			zap_big_count[best] = 0;
		}

		if(used) ZLOG("ALLOCB %s %s(rest=%ld)\n", tag ? tag : "", line, (long)zap_big_other);
	}

	zap_n_malloc = 0;
	zap_n_free = 0;
	zap_big_other = 0;

	for(int i = 0; i < ZAP_BIG_SLOTS; i++) { zap_big_count[i] = 0; zap_big_size[i] = 0; }

	for(int i = 0; i < 8; i++) zap_bucket[i] = 0;
}
