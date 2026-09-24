/*
 * Prueft die FPU-Befehle, die gcc fuer -m68040/-m68060 erzeugt, einzeln
 * gegen ihr Sollergebnis.
 *
 * Anlass war ein Verdacht, der sich NICHT bestaetigt hat: Die Lebensbalken
 * waren auf der V2 (und im Emulator) bis an den Schirmrand gezogen, und der
 * 68080-Bau rechnet die Breite mit FDMUL. Das sind Befehlsformen der
 * 80-Bit-FPU von 68040/68060 (Ergebnis auf double/single gerundet); die FPU
 * des 68080 rechnet mit 64 Bit und ihre Referenz fuehrt diese Formen nicht.
 * Die eigentliche Ursache war ein fehlendes fintrz (Bebbo-Optimierung "e",
 * siehe Makefile). Die Probe bleibt als Werkzeug: Im Emulator (040-Logik)
 * rechnen alle Formen richtig; auf der V2 ist das noch ungeprueft.
 *
 * Ausgabe seriell und auf der Konsole (V2: fpu_probe >RAM:fpu.txt).
 */
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "debug.h"

extern "C" char *__decimalpoint;

static void report(const char *fmt, ...)
{
	char buf[256];
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);

	dbg_printf("%s", buf);
	fputs(buf, stdout);
	fflush(stdout);
}

/* Jede Probe: void f(const double *a, const void *b, double *out)
 * fp0 = *a; fp0 = fp0 <op> *b; *out = fp0.  b zeigt je nach Form auf
 * long, short, float, double oder (Form x) auf ein double, das vorher nach
 * fp1 geladen wird. */
#define PROBE_MEM(name, op)                                  \
	"	.globl _" name "\n"                                   \
	"_" name ":\n"                                            \
	"	movel 4(sp),a0\n"                                     \
	"	fmoved (a0),fp0\n"                                    \
	"	movel 8(sp),a0\n"                                     \
	"	" op " (a0),fp0\n"                                    \
	"	movel 12(sp),a0\n"                                    \
	"	fmoved fp0,(a0)\n"                                    \
	"	rts\n"

#define PROBE_REG(name, op)                                  \
	"	.globl _" name "\n"                                   \
	"_" name ":\n"                                            \
	"	fmovemx fp1,-(sp)\n"                                  \
	"	movel 16(sp),a0\n"                                    \
	"	fmoved (a0),fp0\n"                                    \
	"	movel 20(sp),a0\n"                                    \
	"	fmoved (a0),fp1\n"                                    \
	"	" op " fp1,fp0\n"                                     \
	"	movel 24(sp),a0\n"                                    \
	"	fmoved fp0,(a0)\n"                                    \
	"	fmovemx (sp)+,fp1\n"                                  \
	"	rts\n"

/* einstellige Befehle: fp0 = op(*b) */
#define PROBE_UN(name, op)                                   \
	"	.globl _" name "\n"                                   \
	"_" name ":\n"                                            \
	"	fmovemx fp1,-(sp)\n"                                  \
	"	movel 20(sp),a0\n"                                    \
	"	fmoved (a0),fp1\n"                                    \
	"	" op " fp1,fp0\n"                                     \
	"	movel 24(sp),a0\n"                                    \
	"	fmoved fp0,(a0)\n"                                    \
	"	fmovemx (sp)+,fp1\n"                                  \
	"	rts\n"

/* genau die Folge aus ZObject::RenderHealth im 68080-Bau:
 * int f(const long *health) -> fmove.l ohne fintrz, dazu die fintrz-Fassung */
__asm__(
"	.text\n"
"	.even\n"
PROBE_MEM("p_fmul_l",  "fmull")
PROBE_MEM("p_fdmul_l", "fdmull")
PROBE_MEM("p_fsmul_l", "fsmull")
PROBE_MEM("p_fdmul_w", "fdmulw")
PROBE_MEM("p_fdmul_s", "fdmuls")
PROBE_MEM("p_fdmul_d", "fdmuld")
PROBE_MEM("p_fsmul_s", "fsmuls")
PROBE_MEM("p_fadd_l",  "faddl")
PROBE_MEM("p_fdadd_l", "fdaddl")
PROBE_MEM("p_fdadd_w", "fdaddw")
PROBE_MEM("p_fdadd_s", "fdadds")
PROBE_MEM("p_fdadd_d", "fdaddd")
PROBE_MEM("p_fsadd_l", "fsaddl")
PROBE_MEM("p_fdsub_l", "fdsubl")
PROBE_MEM("p_fdsub_w", "fdsubw")
PROBE_MEM("p_fdsub_d", "fdsubd")
PROBE_MEM("p_fddiv_l", "fddivl")
PROBE_MEM("p_fddiv_d", "fddivd")
PROBE_MEM("p_fsdiv_l", "fsdivl")
PROBE_MEM("p_fdmove_l", "fdmovel")
PROBE_MEM("p_fdmove_s", "fdmoves")
PROBE_MEM("p_fdmove_d", "fdmoved")
PROBE_MEM("p_fsmove_l", "fsmovel")
PROBE_MEM("p_fsmove_s", "fsmoves")
PROBE_REG("p_fmul_x",  "fmulx")
PROBE_REG("p_fdmul_x", "fdmulx")
PROBE_REG("p_fsmul_x", "fsmulx")
PROBE_REG("p_fdadd_x", "fdaddx")
PROBE_REG("p_fsadd_x", "fsaddx")
PROBE_REG("p_fdsub_x", "fdsubx")
PROBE_REG("p_fssub_x", "fssubx")
PROBE_REG("p_fddiv_x", "fddivx")
PROBE_REG("p_fsdiv_x", "fsdivx")
PROBE_REG("p_fdmove_x", "fdmovex")
PROBE_REG("p_fsmove_x", "fsmovex")
PROBE_UN("p_fdsqrt_x", "fdsqrtx")
PROBE_UN("p_fssqrt_x", "fssqrtx")
PROBE_UN("p_fdabs_x", "fdabsx")
PROBE_UN("p_fsabs_x", "fsabsx")
PROBE_UN("p_fdneg_x", "fdnegx")
"	.globl _p_health_trunc\n"
"_p_health_trunc:\n"
"	fmovemx fp2,-(sp)\n"
"	movel 16(sp),a0\n"
"	fmoved #0r0.0036,fp2\n"
"	fdmull (a0),fp2\n"
"	fmovel fp2,d0\n"
"	fmovemx (sp)+,fp2\n"
"	rts\n"
"	.globl _p_health_fintrz\n"
"_p_health_fintrz:\n"
"	fmoved #0r0.0036,fp0\n"
"	movel 4(sp),a0\n"
"	fdmull (a0),fp0\n"
"	fintrzx fp0,fp0\n"
"	fmovel fp0,d0\n"
"	rts\n"
);

extern "C" {
typedef void probe_fn(const double *a, const void *b, double *out);
probe_fn p_fmul_l, p_fdmul_l, p_fsmul_l, p_fdmul_w, p_fdmul_s, p_fdmul_d, p_fsmul_s;
probe_fn p_fadd_l, p_fdadd_l, p_fdadd_w, p_fdadd_s, p_fdadd_d, p_fsadd_l;
probe_fn p_fdsub_l, p_fdsub_w, p_fdsub_d, p_fddiv_l, p_fddiv_d, p_fsdiv_l;
probe_fn p_fdmove_l, p_fdmove_s, p_fdmove_d, p_fsmove_l, p_fsmove_s;
probe_fn p_fmul_x, p_fdmul_x, p_fsmul_x, p_fdadd_x, p_fsadd_x, p_fdsub_x, p_fssub_x;
probe_fn p_fddiv_x, p_fsdiv_x, p_fdmove_x, p_fsmove_x;
probe_fn p_fdsqrt_x, p_fssqrt_x, p_fdabs_x, p_fsabs_x, p_fdneg_x;
long p_health_trunc(const long *health);
long p_health_fintrz(const long *health);
}

static const double A = 0.0036;
static const long   L = 3378;
static const short  W = 3378;
static const float  S = 2.5f;
static const double D = 3378.0;
static const double X = -6.25;

static int failures = 0;

/* Vergleich mit relativer Toleranz 1e-6 (V2: 8 Nibbles Mantisse) */
static void check(const char *name, probe_fn *fn, const void *arg, double expect)
{
	double out = 12345.678;

	fn(&A, arg, &out);

	double diff = out - expect;
	double mag = expect < 0 ? -expect : expect;
	if(diff < 0) diff = -diff;

	const bool ok = (mag > 1e-12) ? (diff / mag < 1e-6) : (diff < 1e-12);

	if(!ok) failures++;

	//ganzzahlig ausgeben (x1000), dazu die relative Abweichung in ppb;
	//x1e6 liefe bei Werten ueber 2147 ueber
	long ppb = (mag > 1e-12) ? (long)(diff / mag * 1e9) : (long)(diff * 1e9);
	if(diff / (mag > 1e-12 ? mag : 1.0) > 2.0) ppb = 2000000000L;

	report("FPU %-11s ist %12ld soll %12ld (x1000) abw %10ld ppb %s\n", name,
	       (long)(out * 1000), (long)(expect * 1000), ppb, ok ? "ok" : "FALSCH");
}

int main()
{
	__decimalpoint = (char *)".";

	dbg_boot();

	check("fmul.l",   p_fmul_l,   &L, A * L);
	check("fdmul.l",  p_fdmul_l,  &L, A * L);
	check("fsmul.l",  p_fsmul_l,  &L, A * L);
	check("fdmul.w",  p_fdmul_w,  &W, A * W);
	check("fdmul.s",  p_fdmul_s,  &S, A * S);
	check("fdmul.d",  p_fdmul_d,  &D, A * D);
	check("fsmul.s",  p_fsmul_s,  &S, A * S);
	check("fmul.x",   p_fmul_x,   &X, A * X);
	check("fdmul.x",  p_fdmul_x,  &X, A * X);
	check("fsmul.x",  p_fsmul_x,  &X, A * X);

	check("fadd.l",   p_fadd_l,   &L, A + L);
	check("fdadd.l",  p_fdadd_l,  &L, A + L);
	check("fdadd.w",  p_fdadd_w,  &W, A + W);
	check("fdadd.s",  p_fdadd_s,  &S, A + S);
	check("fdadd.d",  p_fdadd_d,  &D, A + D);
	check("fsadd.l",  p_fsadd_l,  &L, A + L);
	check("fdadd.x",  p_fdadd_x,  &X, A + X);
	check("fsadd.x",  p_fsadd_x,  &X, A + X);

	check("fdsub.l",  p_fdsub_l,  &L, A - L);
	check("fdsub.w",  p_fdsub_w,  &W, A - W);
	check("fdsub.d",  p_fdsub_d,  &D, A - D);
	check("fdsub.x",  p_fdsub_x,  &X, A - X);
	check("fssub.x",  p_fssub_x,  &X, A - X);

	check("fddiv.l",  p_fddiv_l,  &L, A / L);
	check("fddiv.d",  p_fddiv_d,  &D, A / D);
	check("fsdiv.l",  p_fsdiv_l,  &L, A / L);
	check("fddiv.x",  p_fddiv_x,  &X, A / X);
	check("fsdiv.x",  p_fsdiv_x,  &X, A / X);

	check("fdmove.l", p_fdmove_l, &L, (double)L);
	check("fdmove.s", p_fdmove_s, &S, (double)S);
	check("fdmove.d", p_fdmove_d, &D, D);
	check("fsmove.l", p_fsmove_l, &L, (double)L);
	check("fsmove.s", p_fsmove_s, &S, (double)S);
	check("fdmove.x", p_fdmove_x, &X, X);
	check("fsmove.x", p_fsmove_x, &X, X);

	static const double Q = 6.25;
	check("fdsqrt.x", p_fdsqrt_x, &Q, 2.5);
	check("fssqrt.x", p_fssqrt_x, &Q, 2.5);
	check("fdabs.x",  p_fdabs_x,  &X, 6.25);
	check("fsabs.x",  p_fsabs_x,  &X, 6.25);
	check("fdneg.x",  p_fdneg_x,  &X, 6.25);

	//die Folge aus RenderHealth: 0.0036 * 3378 = 12,16 -> 12
	long t1 = p_health_trunc(&L);
	long t2 = p_health_fintrz(&L);
	const bool hok = (t1 == 12 || t1 == 13) && t2 == 12;
	if(!hok) failures++;
	report("FPU Balkenbreite (health 3378): fmove.l=%ld fintrz=%ld, soll 12 %s\n",
	       t1, t2, hok ? "ok" : "FALSCH");

	report("FPU Fehler: %ld\n", (long)failures);

	if(failures)
		dbg_fail("fpu_probe: Abweichungen");
	else
		dbg_ok("fpu_probe: alle Befehle richtig");

	return 0;
}
