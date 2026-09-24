#ifndef ZOD_AMIGA_DEBUG_H
#define ZOD_AMIGA_DEBUG_H
/*
 * Serieller Debug-Kanal (KPrintF aus libdebug, Link mit -ldebug).
 * FS-UAE spiegelt die serielle Schnittstelle per serial_port=tcp://,
 * tools/run.sh schreibt sie nach logs/serial.log und wertet die Marken aus:
 *   [BOOT]         Programmstart
 *   [OK] <text>    Erfolg  -> run.sh Exit 0
 *   [FAIL] <text>  Fehler  -> run.sh Exit 1
 * dbg_printf nutzt RawDoFmt-Format: %ld fuer 32-Bit-Werte, %s fuer Strings.
 */
#ifdef __cplusplus
extern "C" {
#endif

void dbg_boot(void);
void dbg_ok(const char *msg);
void dbg_fail(const char *msg);
void dbg_printf(const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif
