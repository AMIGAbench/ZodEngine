#include "zod_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __amigaos__
#include <clib/debug_protos.h>
#include <proto/dos.h>
#endif

static int log_target = ZOD_LOG_STDOUT;
static FILE *log_fp = 0;

void zod_log_set(int target, const char *filename)
{
	zod_log_close();

	log_target = target;

	if(target == ZOD_LOG_FILE && filename && *filename)
	{
		log_fp = fopen(filename, "w");
		if(!log_fp) log_target = ZOD_LOG_NONE;
	}
}

void zod_log_open_name(const char *target)
{
	if(!target || !*target || !strcmp(target, "stdout"))
	{
		zod_log_set(ZOD_LOG_STDOUT, 0);
		return;
	}

	if(!strcmp(target, "none"))
	{
		zod_log_set(ZOD_LOG_NONE, 0);
		return;
	}

	if(!strcmp(target, "serial"))
	{
		zod_log_set(ZOD_LOG_SERIAL, 0);
		return;
	}

	zod_log_set(ZOD_LOG_FILE, target);
}

void zod_log_open(void)
{
	const char *env = getenv("ZOD_LOG");

#ifdef __amigaos__
	/* libnix liefert ueber getenv keine per SetEnv gesetzten ENV:-Variablen,
	 * deshalb zusaetzlich GetVar der dos.library abfragen. */
	char var[128];

	if((!env || !*env) && GetVar((CONST_STRPTR)"ZOD_LOG", (STRPTR)var, sizeof(var), 0) > 0)
		env = var;
#endif

	zod_log_open_name(env);
}

void zod_log_close(void)
{
	if(log_fp)
	{
		fclose(log_fp);
		log_fp = 0;
	}
}

void zod_logv(const char *fmt, va_list ap)
{
	switch(log_target)
	{
	case ZOD_LOG_STDOUT:
		vprintf(fmt, ap);
		break;

	case ZOD_LOG_FILE:
		if(log_fp)
		{
			vfprintf(log_fp, fmt, ap);
			fflush(log_fp);
		}
		break;

	case ZOD_LOG_SERIAL:
#ifdef __amigaos__
		/* Bewusst NICHT KVPrintF: RawDoFmt liest %d als 16 Bit, waehrend der
		 * Engine-Code durchweg 32-Bit-Werte uebergibt -- die Ausgabe waere
		 * verschoben (erst als "0 Bilder aus 6951 Dateien" aufgefallen).
		 * Deshalb hier mit der C-Bibliothek formatieren und den fertigen
		 * Text ausgeben. */
		{
			char buf[512];

			vsnprintf(buf, sizeof(buf), fmt, ap);
			buf[sizeof(buf) - 1] = 0;
			KPutStr((CONST_STRPTR)buf);
		}
#else
		vprintf(fmt, ap);
#endif
		break;

	default:
		break;
	}
}

void zod_log(const char *fmt, ...)
{
	va_list ap;

	if(log_target == ZOD_LOG_NONE) return;

	va_start(ap, fmt);
	zod_logv(fmt, ap);
	va_end(ap);
}
