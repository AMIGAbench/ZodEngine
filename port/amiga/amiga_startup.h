#ifndef ZOD_AMIGA_STARTUP_H
#define ZOD_AMIGA_STARTUP_H
/*
 * Start und Ende unter AmigaOS.
 *
 * - Arbeitsverzeichnis auf PROGDIR: setzen, damit die relativen Pfade der
 *   Engine ("assets/...") unabhaengig davon stimmen, von wo aus gestartet
 *   wurde (Shell, Workbench, Autostart).
 * - Beim Beenden das urspruengliche Verzeichnis wiederherstellen.
 * - Strg-C abfragen, damit das Spiel sich sauber beenden laesst.
 *
 * Auf anderen Plattformen sind alle Funktionen wirkungslos.
 */

#ifdef __cplusplus
extern "C" {
#endif

void zod_amiga_startup(void);
void zod_amiga_shutdown(void);

/* 1, wenn seit dem letzten Aufruf Strg-C gedrueckt wurde */
int zod_amiga_break(void);

/* Freien Speicher melden (AmigaOS: AvailMem). Der Vergleich von Start und Ende
 * zeigt, ob beim Beenden wirklich alles freigegeben wurde. Auf anderen
 * Plattformen wirkungslos. */
void zod_mem_report(const char *when);
void zod_mem_report_exit(void);

/* Grosse, langlebige Bloecke (Kartenflaeche) direkt beim System belegen.
 * libnix-malloc gibt freigewordene Bereiche erst zurueck, wenn sie ganz leer
 * sind; kleine Belegungen setzen sich in die Loecher und halten sie fest.
 * Ueber AllocVec/FreeVec kommt der Block beim Freigeben sofort zurueck und
 * verschmilzt mit seinen Nachbarn. Auf anderen Systemen malloc/free. */
void *zod_big_alloc(unsigned long size);
void zod_big_free(void *block);

/* Umgebungsvariable lesen -- auf AmigaOS ueber GetVar der dos.library.
 *
 * `getenv` sieht unter libnix die per `SetEnv` gesetzten Variablen NICHT;
 * jede Messschalter-Stelle dieses Projekts hat den GetVar-Tanz bisher selbst
 * gemacht. Hier einmal, damit Aufrufer nicht `proto/dos.h` einbinden muessen:
 * dessen Makros `Read` und `Write` kollidieren mit `ZMap::Read`/`ZMap::Write`
 * und erzeugen die irrefuehrende Meldung
 * `macro "Write" requires 3 arguments, but only 1 given`.
 *
 * Liefert `buf` oder 0, wenn die Variable nicht gesetzt ist. */
const char *zod_env(const char *name, char *buf, int size);

#ifdef __cplusplus
}
#endif

#endif
