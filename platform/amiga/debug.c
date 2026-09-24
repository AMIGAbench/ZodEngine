/*
 * Serieller Debug-Helper, uebernommen aus dem A1200-Harness.
 * KPrintF/KVPrintF erwarten RawDoFmt-Argumente; va_list ist auf m68k-gcc
 * ein Zeiger auf die Stack-Argumente und passt damit direkt.
 */
#include <exec/types.h>
#include <clib/debug_protos.h>
#include <stdarg.h>
#include "debug.h"

void dbg_boot(void)
{
    KPutStr((CONST_STRPTR)"[BOOT]\n");
}

void dbg_ok(const char *msg)
{
    KPrintF((CONST_STRPTR)"[OK] %s\n", msg);
}

void dbg_fail(const char *msg)
{
    KPrintF((CONST_STRPTR)"[FAIL] %s\n", msg);
}

void dbg_printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    KVPrintF((CONST_STRPTR)fmt, (CONST_APTR)ap);
    va_end(ap);
}
