#ifndef ZOD_AMIGA_AUDIO_H
#define ZOD_AMIGA_AUDIO_H
/*
 * Tonausgabe über AHI.
 *
 * Ersetzt die SDL_mixer-Attrappe aus P3. Die Klänge kommen als rohes
 * vorzeichenbehaftetes 8-Bit-Mono aus dem Archiv (port/zod_pack.cpp,
 * Format 3) -- genau das Format, das AHI erwartet; es wird also weder
 * dekodiert noch umgerechnet.
 *
 * Aufbau wie in der geprüften Vorlage des MarioKart-Ports:
 * CreateMsgPort + OpenDevice("ahi.device"), AHIBase aus dem IO-Request,
 * dann AHI_AllocAudioA / AHI_LoadSound / AHI_PlayA. Keine zusätzliche
 * Bibliothek nötig, nur -lamiga.
 *
 * Auf anderen Plattformen sind alle Funktionen wirkungslos.
 */

#ifdef __cplusplus
extern "C" {
#endif

/* Lautstaerke aller Kanaele auf einmal (SDL_mixer: Mix_Volume(-1, v)). */
void zod_audio_set_master(int volume);

/* Klangeffekte ueber den eigenen Mischer statt ueber AHIs Mischer.
 *
 * Wird gesetzt, sobald die Ausgabe (AHI-Strom oder Paula) steht. Danach
 * sind zod_audio_add_sound/play/stop/playing/set_volume reine
 * Weiterleitungen an musik_mischer -- ein Weg fuer alles, wie es sein
 * muss: AHI und Paula wollen dieselbe Hardware und koennen nicht
 * nebeneinander laufen. */
void zod_audio_mischerweg(int an);

/* ------------------------------------------------------------------------
 * Fortlaufender Stereo-Strom auf dem reservierten Musikkanal.
 *
 * WARUM HIER UND NICHT IM MUSIKMODUL: Ein Treiber laesst nur EINEN
 * AHI_AllocAudio zu. Die Engine hat den Kontext fuer die Klangeffekte
 * bereits offen; ein zweiter aus dem Musikmodul scheitert bei JEDEM Modus.
 * Genau das ist im Emulatorlauf vom 22.09. passiert
 * ("kein AHI-Echtzeitmodus zu oeffnen", obwohl zwei Zeilen darueber
 * "AHI: 8 Kanaele, 11025 Hz" steht).
 *
 * Deshalb reicht das Musikmodul seine fertig gemischten Puffer hier
 * herein. Zwei Puffer wechseln sich ab; ist einer durchgespielt, ruft AHI
 * den Hook, und der weckt den Mischprocess.
 * ------------------------------------------------------------------------ */

/* Beide Puffer anmelden und den Strom anwerfen. rahmen = Rahmen JE Puffer,
 * die Daten sind 16 Bit stereo verschraenkt.
 * fertig() laeuft im INTERRUPT -- dort nur ein Signal setzen. */
int  zod_audio_stream_open(unsigned int rate, unsigned int rahmen,
                           void *puffer0, void *puffer1,
                           void (*fertig)(void));

/* Den gerade gefuellten Puffer als naechsten anmelden. */
void zod_audio_stream_queue(int nr);

void zod_audio_stream_volume(int volume);
void zod_audio_stream_close(void);

/* Audiomodus vorgeben, VOR zod_audio_open. 0 = Vorgabe (AHI_DEFAULT_ID).
 *
 * AHI kennt hier keine "Unit": Die Tiefschnittstelle waehlt einen MODUS. Units
 * 0-3 der Voreinstellungen gehoeren zur Geraeteschnittstelle, die diese Engine
 * nicht benutzt. Der Launcher setzt den Modus ueber -A. */
void zod_audio_set_mode(unsigned long mode_id);

/* Gerät öffnen und Kanäle bereitstellen. 1 = Ton verfügbar. */
int zod_audio_open(int channels, int mix_frequency);
void zod_audio_close(void);
int zod_audio_available(void);

/* Klang anmelden: Daten wie im Archiv (signed 8 Bit mono).
 * Der Puffer muss bis zum Schließen gültig bleiben (AHI spielt daraus).
 * Rückgabe: Klang-Nummer oder -1. */
int zod_audio_add_sound(unsigned char *data, unsigned int length, unsigned int rate);

/* Auf einem freien Kanal abspielen; Lautstärke 0..128, Rückgabe: Kanal oder -1. */
int zod_audio_play(int sound_id, int volume, int loop);
void zod_audio_stop(int channel);
int zod_audio_playing(int channel);
void zod_audio_set_volume(int channel, int volume);

/* ---- Musik ----------------------------------------------------------
 *
 * Getrennt von den Effekten, aus zwei Gruenden:
 *
 *  - Sie braucht einen EIGENEN Kanal. zod_audio_play vergibt die Kanaele
 *    reihum; ohne Reservierung nimmt der naechste Schuss der Musik den
 *    Kanal weg.
 *  - Sie ist zweikanalig. AHI kennt dafuer AHIST_S8S, und ein Stereostueck
 *    laeuft damit auf EINEM Mischkanal -- nicht auf zweien.
 *
 * Springen loest AHI nicht von sich aus. Dafuer wird ein zweiter Klangplatz
 * mit verschobener Anfangsadresse angemeldet: gespielt wird ab der neuen
 * Stelle, und als Schleifenklang dient das ganze Stueck. So laeuft es nach
 * dem Ende wieder von vorn, nicht ab der Sprungstelle.
 */

#ifdef __cplusplus
}
#endif

#endif
