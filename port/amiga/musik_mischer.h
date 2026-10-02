#ifndef ZOD_MUSIK_MISCHER_H
#define ZOD_MUSIK_MISCHER_H
/*
 * Musikmischer -- erzeugt aus einem ZMU1-Strom und einer Instrumentenbank
 * fertige Stereo-Rahmen. AUSGABEWEG-UNABHAENGIG: weder AHI noch Paula
 * kommen hier vor. Beide Ausgaben rufen nur mischer_fuellen() und reichen
 * das Ergebnis an ihre Hardware weiter.
 *
 * Warum selbst mischen statt AHI je Note einen Kanal zu geben:
 *
 *   - AHI mischt dann N Kanaele mit einem allgemeinen Mischer (Panorama,
 *     16 Bit, Begrenzung). Auf der V1200 gemessen: 638 Promille Rechenlast
 *     bei fuenf gleichzeitigen Stimmen -- das kann nicht die Musik sein.
 *   - Der Songfortschritt haengt dann an AHIA_PlayerFunc, also an einem
 *     Interrupt mit 120 Hz. Paula hat so etwas nicht.
 *
 * Hier rueckt das Stueck INNERHALB der Mischschleife vor. Die Zeitaufloesung
 * ist damit abtastgenau, nicht an einen Interrupt gebunden, und derselbe
 * Code traegt beide Ausgabewege.
 *
 * ALLES IST GANZZAHLIG. Der Mischer laeuft spaeter aus einem Interrupt oder
 * einem eigenen Task; Fliesskomma hat dort nichts verloren (und auf dem
 * 68080 muesste sonst der FPU-Zustand gesichert werden).
 */
/* Auf dem Host gibt es kein exec/types.h. Die Typen muessen dort GENAU
 * so breit sein wie auf dem Amiga -- ULONG als "unsigned long" waere auf
 * x86-64 64 Bit, und die ganze 16.16-Rechnung liefe anders. */
#ifdef __amigaos__
#include <exec/types.h>
#else
#include <stdint.h>
typedef uint8_t  UBYTE;
typedef int8_t   BYTE;
typedef uint16_t UWORD;
typedef int16_t  WORD;
typedef uint32_t ULONG;
typedef int32_t  LONG;
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define MISCHER_MAX_STIMMEN  32
#define MISCHER_TAKT_HZ     120      /* ein ZMU1-Schritt = 1/120 s */

/* Bank anmelden und Mischfrequenz festlegen.
 * bank/banklen zeigen auf die eingelesene Instrumentendatei; der Mischer
 * kopiert nichts, der Puffer muss also leben, solange er laeuft.
 * Rueckgabe: Zahl der gefundenen Instrumente, 0 = Bank unbrauchbar. */
int mischer_init(const UBYTE *bank, ULONG banklen, ULONG rate, int stimmen);

/* Ein Stueck auflegen (ZMU1). Setzt den Abspielstand zurueck. */
int mischer_stueck(const UBYTE *zmu, ULONG len);

/* An eine Stelle springen (absolut, in Schritten zu 1/120 s).
 *
 * Die Engine benutzt das fuer ihre Gefahrenstufen: sie setzt die Musik auf
 * eine vorberechnete Stelle des Stueckes (ZMusicEngine::d_level_start).
 * Ohne diese Funktion liefe das Stueck einfach durch -- eine stille
 * Verhaltensaenderung, und die soll es nicht geben.
 *
 * Vorgespult wird OHNE Klang: Programmwechsel und Lautstaerken werden
 * mitgenommen, Noten nicht angeschlagen. Sonst klaenge beim Sprung der
 * ganze uebersprungene Abschnitt auf einmal. */
void mischer_springen(ULONG schritte);

/* Rahmen erzeugen: verschraenkt links/rechts, 16 Bit mit Vorzeichen.
 * ziel muss 2*rahmen Woerter fassen. Der Songfortschritt passiert hier. */
void mischer_fuellen(WORD *ziel, ULONG rahmen);

/* Laeuft das Stueck noch? 0, sobald das Ende erreicht und alles verklungen ist. */
int mischer_laeuft(void);

/* Selbsttest des Assemblerkerns: vergleicht ihn BYTEWEISE mit der
 * C-Fassung ueber alle Zweige (mitten in der Probe, am Ende, ueber das Ende
 * hinaus, mit und ohne Schleife). Rueckgabe: Zahl der geprueften Faelle,
 * negativ = Fall N ungleich, 0 = ohne Assemblerkern gebaut. Erst nach
 * bestandenem Test wird der Kern benutzt. */
int mischer_selbsttest(void);
int mischer_asm_laeuft(void);

/* ------------------------------------------------------------------------
 * KLANGEFFEKTE ueber denselben Mischer.
 *
 * WARUM: AHI und Paula wollen dieselbe Hardware. Laeuft AHI, bekommt
 * audio.device keine Kanaele mehr -- auf der V1200 belegt mit
 * "Musik: audio.device gibt keine Kanaele her", waehrend zwei Zeilen
 * darueber "AHI: 8 Kanaele" stand. "Effekte ueber AHI, Musik ueber Paula"
 * ist deshalb kein Entwurf, sondern ein Widerspruch.
 *
 * Also mischt dieser Mischer ALLES, und der Ausgabeweg bekommt einen
 * einzigen Stereo-Strom. Fuer den Mischer ist ein Klangeffekt dasselbe
 * wie ein Schlaginstrument: eine Probe, ungestimmt, meist einmalig.
 *
 * Die Stimmen sind getrennt: die Musik nimmt die unteren Plaetze, die
 * Effekte die oberen. Sonst wuerde ein Schwall Explosionen der Musik die
 * Stimmen wegnehmen -- oder umgekehrt.
 * ------------------------------------------------------------------------ */

/* Einen Klang anstossen. proben muss leben, solange er spielt.
 * rate = Abtastrate der Probe, vol 0..128, pan 0..127 (64 = Mitte).
 * Rueckgabe: Platz (>= 0) oder -1, wenn keiner frei ist. */
int  mischer_effekt(const BYTE *proben, ULONG laenge, ULONG rate,
                    int vol, int pan, int schleife);

void mischer_effekt_stop(int platz);
int  mischer_effekt_laeuft(int platz);
void mischer_effekt_vol(int platz, int vol);

/* Wie viele der Stimmen den Effekten gehoeren (Rest: Musik). */
void mischer_effektstimmen(int anzahl);

/* ------------------------------------------------------------------------
 * LAUTSTAERKE: ZWEI FAKTOREN JE WEG, nicht einer fuer alles.
 *
 * Vom Nutzer gemeldet (02.10.): "SFX ist im Vergleich zur Musik sehr leise.
 * SFX muessten etwas lauter sein als die Musik. Und generell koennte die
 * Lautstaerke hoeher sein."
 *
 * Nachgerechnet war die Ursache nicht eine Zahl, sondern der Entwurf. In
 * echtem SDL_mixer regelt `Mix_VolumeMusic` NUR die Musik und
 * `Mix_Volume(-1,·)` NUR die Effektkanaele. In diesem Port ging
 * `Mix_VolumeMusic` an die Lautstaerke des GANZEN Mischstroms (Paula-Kanal
 * bzw. AHI) -- also an Musik UND Effekte. `Mix_VolumeMusic(80)` daempfte
 * damit beides auf 62 %, die Effekte zusaetzlich zu ihrer eigenen
 * Daempfung.
 *
 * Und die war schon erheblich: die Engine laedt 97 von rund 107 Klaengen
 * mit `base_volume` 40 von 128 (qzod_soundengine_old.h:58), einzelne mit
 * 5, 10, 20, 25, 30. Eine Effektstimme kommt damit auf 80..118 von 256,
 * eine Musiknote auf bis zu 252 -- und Musik ist POLYPHON (gemessene
 * Spitze 13 bis 32 Stimmen), ein Effekt ist eine Stimme.
 *
 * Deshalb hier vier Werte statt einem:
 *
 *   Balance   `-U` / `-E` bzw. die beiden Regler in ZodLaunch. Das ist die
 *             Grundabstimmung, mit gemessener Vorgabe.
 *   Regler    was die Engine selbst stellt (Menue SOUND_25..100).
 *
 * Der Mischer multipliziert sie, der Aufrufer muss nichts verrechnen --
 * sonst gaebe es drei Stellen, die sich gegenseitig ueberschreiben, und
 * genau diese Klasse Fehler sucht hinterher niemand.
 *
 * Angewandt wird im MISCHBLOCK, nicht bei Notenbeginn: eine gehaltene Note
 * oder ein laufender Klang muss einem Reglerdreh folgen. Kosten: zwei
 * Multiplikationen je Stimme je Block von 256 Rahmen. Die Schnittstelle
 * des Assemblerkerns bleibt unberuehrt -- er nimmt `vl`/`vr` ohnehin als
 * Argumente.
 *
 * Alle vier: 0..128, 128 = unveraendert.
 * ------------------------------------------------------------------------ */
void mischer_musik_balance(int v);
void mischer_effekt_balance(int v);
void mischer_musik_vol(int v);        /* Mix_VolumeMusic      */
void mischer_effekt_vol_alle(int v);  /* Mix_Volume(-1, v)    */

/* Alle vier Werte lesen -- fuer die Protokollzeile beim Start. Ein Regler,
 * der sich nicht meldet, ist von einem wirkungslosen nicht zu
 * unterscheiden. */
void mischer_lautstaerken(int *bal_m, int *bal_e, int *eng_m, int *eng_e);

/* Die beiden gemessenen Hubwerte (1/128). */
void mischer_hub(int *hub_m, int *hub_e);

/* Zaehlwerte fuer den Bericht -- keine Messung im Mischer selbst. */
void mischer_zahlen(ULONG *noten, ULONG *spitze, ULONG *verdraengt,
                    ULONG *ohne_instrument, ULONG *geklemmt);

#ifdef __cplusplus
}
#endif

#endif
