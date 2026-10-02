/*
 * P0-Rauchtest fuer die Amiga-Toolchain: prueft die C++-Laufzeit (globale
 * Konstruktoren, vector, string, sort), FPU-Mathematik, Typgroessen und
 * Struct-Layout, die fuer den ZodEngine-Port relevant sind, und meldet das
 * Ergebnis ueber den seriellen Kanal.
 */
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include <exec/execbase.h>
#include <proto/exec.h>

#include "debug.h"

#ifndef AFB_68080
#define AFB_68080 10 /* Apollo-Erweiterung der AttnFlags */
#endif

extern struct ExecBase *SysBase;

/* Mit "version amiga_hello" pruefbar; bei jeder Aenderung hochzaehlen. */
static const char version_tag[] = "$VER: amiga_hello 0.3 (16.09.2026)";

/* Globaler Konstruktor: die Engine legt vor main() Mutexe und Strings an. */
struct GlobalProbe
{
    int value;
    std::string text;
    GlobalProbe() : value(42), text("zod") {}
};
static GlobalProbe global_probe;

/* Layout wie map_object in zmap_structures_old.h (nicht gepackt). */
struct map_object_probe
{
    unsigned short x, y;
    char owner;
    unsigned char object_type;
    unsigned char object_id;
    char blevel;
    unsigned short extra_links;
    int health_percent;
};

enum team_probe { TEAM_A, TEAM_B };

/* Fehler gehen auch auf die Konsole: auf echter Hardware (V1200) fehlt meist
 * ein Terminal am seriellen Port. Kein [FAIL]-Praefix auf Serial, damit
 * run.sh erst am Ende ueber das Gesamtergebnis entscheidet. */
static int check(bool cond, const char *what)
{
    if (!cond)
    {
        dbg_printf("check failed: %s\n", what);
        printf("  FEHLER: %s\n", what);
    }
    return cond ? 0 : 1;
}

/* Zaehlt die wirksamen Mantissenbits der FPU (IEEE double: 52). Die V2-FPU
 * (Core 2.17) rechnet laut AC68080PRM nur mit 8 Nibbles Mantisse. */
static int fpu_mantissa_bits()
{
    volatile double one = 1.0;
    volatile double eps = 1.0;
    volatile double sum;
    int bits = 0;
    for (;;)
    {
        eps = eps * 0.5;
        sum = one + eps;
        if (sum == one || bits > 80)
            break;
        bits++;
    }
    return bits;
}

int main()
{
    int errors = 0;
    UWORD attn = SysBase->AttnFlags;

    dbg_boot();
    printf("%s\n", version_tag + 6);

    const char *cpu = "68000/010";
    if (attn & (1 << AFB_68080))      cpu = "68080";
    else if (attn & AFF_68060)        cpu = "68060";
    else if (attn & AFF_68040)        cpu = "68040";
    else if (attn & AFF_68030)        cpu = "68030";
    else if (attn & AFF_68020)        cpu = "68020";
    bool fpu = (attn & (AFF_68881 | AFF_68882 | AFF_FPU40)) != 0;
    dbg_printf("cpu=%s fpu=%ld attn=0x%lx\n", cpu, (LONG)fpu, (ULONG)attn);
    printf("AttnFlags=0x%04x\n", (unsigned)attn);

    errors += check(global_probe.value == 42 && global_probe.text == "zod",
                    "globaler Konstruktor");

    std::vector<int> v;
    for (int i = 0; i < 1000; i++)
        v.push_back((i * 7919) % 1000);
    std::sort(v.begin(), v.end());
    errors += check(v.front() == 0 && v.back() == 999, "vector/sort");

    std::string s = "Zod";
    s += " Engine";
    errors += check(s.size() == 10 && s.find("Engine") == 4, "string");

    int mant = fpu_mantissa_bits();
    dbg_printf("fpu_mantissa_bits=%ld\n", (LONG)mant);
    printf("FPU-Mantissenbits: %d (IEEE double: 52)\n", mant);

    /* Toleranzen auf Spielbedarf (Winkel/Positionen), nicht auf volle
     * double-Genauigkeit: die V2-FPU erreicht nur ~9 Dezimalstellen. */
    volatile double a = 1.0, b = 1.0, c = 3.0, d = 4.0, e = M_PI / 6.0;
    double ang = std::atan2(a, b) * 180.0 / M_PI;
    double hyp = std::sqrt(c * c + d * d);
    double sn = std::sin(e);
    printf("atan2(1,1)=%.12f sqrt(25)=%.12f sin(pi/6)=%.12f\n", ang, hyp, sn);
    errors += check(std::fabs(ang - 45.0) < 1e-6, "atan2");
    errors += check(std::fabs(hyp - 5.0) < 1e-6, "sqrt");
    errors += check(std::fabs(sn - 0.5) < 1e-6, "sin");

    unsigned int one = 1;
    bool big_endian = *(unsigned char *)&one == 0;
    errors += check(big_endian, "Big-Endian erwartet");

    dbg_printf("sizeof int=%ld long=%ld bool=%ld enum=%ld double=%ld map_object=%ld\n",
               (LONG)sizeof(int), (LONG)sizeof(long), (LONG)sizeof(bool),
               (LONG)sizeof(team_probe), (LONG)sizeof(double),
               (LONG)sizeof(map_object_probe));
    errors += check(sizeof(int) == 4 && sizeof(bool) == 1 && sizeof(team_probe) == 4,
                    "Typgroessen fuer Drahtformat");

    printf("Zod Amiga hello: cpu=%s fpu=%d errors=%d\n", cpu, fpu ? 1 : 0, errors);

    if (errors)
    {
        dbg_fail("amiga_hello");
        return 20;
    }
    dbg_ok("amiga_hello");
    return 0;
}
