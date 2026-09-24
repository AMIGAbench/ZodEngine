#ifndef ZOD_FINECLOCK_H
#define ZOD_FINECLOCK_H
/*
 * Feine Uhr für Messungen je Bild.
 *
 * WARUM NICHT `current_time()`: Das ist `gettimeofday`, und die libnix-Fassung
 * holt die Zeit über `DateStamp()` -- also in **Ticks zu 20 ms**. Im
 * Disassemblat steht wörtlich `muls.l #20000,d2`, `tv_usec` ist damit immer
 * ein Vielfaches von 20 000. Ein Bild dauert bei 24 fps 41 ms, bei 60 fps aber
 * nur 16 ms; letzteres liegt **unter** der Auflösung. Eine Messung je Bild ist
 * mit dieser Uhr nicht möglich.
 *
 * `ReadEClock` aus der timer.device liefert stattdessen die E-Clock:
 * PAL 709 379 Hz, also rund **1,41 µs** je Schritt -- etwa 14 000-mal feiner.
 * `TimerBase` ist ohnehin schon offen (port/amiga/amiga_startup.cpp).
 *
 * Es wird bewusst nur das UNTERE Langwort zurückgegeben. Der Zähler läuft
 * damit nach rund 6050 s über, was für Differenzen zwischen zwei Bildern
 * gleichgültig ist: Die Subtraktion vorzeichenloser 32-Bit-Werte liefert auch
 * über den Überlauf hinweg den richtigen Abstand, solange er kleiner als der
 * halbe Bereich ist. 64-Bit-Arithmetik wäre hier nur teurer (auf m68k gibt es
 * keine 64-Bit-Division in Hardware).
 */

#ifdef __amigaos__

#include <exec/types.h>
#include <devices/timer.h>
#include <proto/timer.h>

extern struct Device *TimerBase;

/* Ticks der E-Clock, unteres Langwort. 0, wenn die timer.device fehlt. */
static inline unsigned long zod_fineclock_ticks(void)
{
	struct EClockVal ev;

	if(!TimerBase) return 0;

	ReadEClock(&ev);

	return (unsigned long)ev.ev_lo;
}

/* Schritte je Sekunde (PAL rund 709379). 0, wenn die timer.device fehlt. */
static inline unsigned long zod_fineclock_freq(void)
{
	struct EClockVal ev;

	if(!TimerBase) return 0;

	return (unsigned long)ReadEClock(&ev);
}

#else

/* Auf dem Host genügt gettimeofday -- dort löst es im Mikrosekundenbereich
 * auf. Umgerechnet auf dieselbe Schnittstelle: "Ticks" sind hier µs. */
#include <sys/time.h>

static inline unsigned long zod_fineclock_ticks(void)
{
	struct timeval t;

	gettimeofday(&t, nullptr);

	return (unsigned long)(t.tv_sec * 1000000UL + t.tv_usec);
}

static inline unsigned long zod_fineclock_freq(void)
{
	return 1000000UL;
}

#endif

#endif
