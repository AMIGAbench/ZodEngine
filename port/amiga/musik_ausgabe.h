#ifndef ZOD_MUSIK_AUSGABE_H
#define ZOD_MUSIK_AUSGABE_H
/*
 * Musikausgabe -- gibt die Rahmen des Mischers an die Hardware.
 *
 * ZWEI WEGE, EIN MISCHER. Der Mischer (musik_mischer.c) kennt weder AHI
 * noch Paula; hier wird nur entschieden, wohin seine Rahmen gehen:
 *
 *   MUSIK_AHI    ein einziger Stereo-Strom an AHI. Laeuft auf jeder
 *                Soundkarte und auf SAGA, 16 Bit.
 *   MUSIK_PAULA  audio.device zur Arbitrierung, danach die Kanaele
 *                unmittelbar. 8 Bit, ch0+ch3 links, ch1+ch2 rechts --
 *                nach dem Vorbild aus ADooms amiga_music.s.
 *
 * WARUM EIN EIGENER PROCESS: Der Mischer kostet auf der V1200 gemessen
 * 38 Promille. Das in einem Interrupt zu tun hiesse, dort Millisekunden zu
 * verbringen. Stattdessen macht der Interrupt (bzw. AHIs SoundFunc) nur
 * eines -- ein Signal setzen --, und ein eigener Process fuellt den
 * freien Puffer. Das Spiel merkt davon nichts und bleibt multitaskingfaehig.
 *
 * Das ist zugleich der Grund, warum ein Ruckler im Spiel die Musik NICHT
 * stoert: Der Process laeuft weiter, auch wenn die Hauptschleife 350 ms in
 * der Bot-Wegsuche haengt.
 */
#include <exec/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MUSIK_AHI    0
#define MUSIK_PAULA  1

/* Bank anmelden und den Ausgabeweg oeffnen. bank muss leben, solange die
 * Musik laeuft -- der Mischer kopiert die Proben nicht.
 * Rueckgabe: 1 = laeuft, 0 = kein Ton (Grund steht im Protokoll). */
int musik_start(int weg, const UBYTE *bank, ULONG banklen,
                ULONG rate, int stimmen);

/* Stueck auflegen. Der Puffer muss leben, bis ein anderes Stueck kommt
 * oder musik_stop() gerufen wird. */
int musik_spiele(const UBYTE *zmu, ULONG len, int schleife);

/* An eine Stelle springen, in Schritten zu 1/120 s. Der Auftrag geht ueber
 * den Mischprocess -- die Engine ruft aus dem Hauptweg, der Mischer laeuft
 * nebenher, und beide gleichzeitig im Abspielstand zu ruehren waere ein
 * Wettlauf. */
void musik_springen(ULONG schritte);

void musik_pause(int an);
void musik_lautstaerke(int v);      /* 0..128 */
int  musik_laeuft(void);
void musik_stop(void);

/* Welcher Weg laeuft wirklich? Kann von der Anforderung abweichen, wenn
 * der gewuenschte nicht zu oeffnen war. -1 = keiner. */
int  musik_weg(void);

/* Zaehlwerte fuer den Bericht: wie oft der Nachschub zu spaet kam. Eine
 * Null hier ist erst dann ein Befund, wenn der Zaehler im Gegenfall
 * nachweislich zaehlt -- deshalb meldet die Sonde auch die Zahl der
 * gefuellten Puffer. */
void musik_zahlen(ULONG *puffer, ULONG *zu_spaet);

#ifdef __cplusplus
}
#endif

#endif
