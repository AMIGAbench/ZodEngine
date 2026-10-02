#ifndef ZOD_LOG_H
#define ZOD_LOG_H
/*
 * Textausgabe der Engine.
 *
 * Das Original schreibt mit printf direkt nach stdout. Unter AmigaOS gibt es
 * beim Start von der Workbench keine Konsole; ausserdem soll die Ausgabe im
 * Emulator auf die serielle Schnittstelle gehen koennen. Deshalb laufen alle
 * Meldungen ueber ZLOG.
 *
 * Ziel zur Laufzeit ueber zod_log_open() bzw. die Umgebungsvariable
 * ZOD_LOG (Dateiname, "stdout", "none"; auf dem Amiga zusaetzlich "serial").
 */
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

enum zod_log_target
{
	ZOD_LOG_NONE = 0,
	ZOD_LOG_STDOUT,
	ZOD_LOG_FILE,
	ZOD_LOG_SERIAL
};

void zod_log_open(void);           /* wertet ZOD_LOG aus, Standard: stdout */
/* Ziel als Name: "stdout", "none", "serial" oder ein Dateiname */
void zod_log_open_name(const char *target);
void zod_log_set(int target, const char *filename);
void zod_log_close(void);
void zod_log(const char *fmt, ...);
void zod_logv(const char *fmt, va_list ap);

#ifdef __cplusplus
}
#endif

#define ZLOG zod_log

#endif
