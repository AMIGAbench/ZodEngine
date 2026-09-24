/*
 * AMMX-Erkennung und Selbsttest (Apollo 68080).
 *
 * Warum ein Selbsttest im Programm statt einer getrennten Sonde: AMMX ist
 * hier nicht automatisiert prüfbar. FS-UAE läuft als A1200 mit einem
 * emulierten 68040 (`emu/zod-rtg-040.fs-uae`), es gibt keinen Apollo-Kern.
 * Jede AMMX-Zeile bleibt unbelegt, bis sie auf echter Hardware läuft.
 *
 * Zwei Dinge sind ausdrücklich NICHT belegt und werden hier geprüft:
 *
 *  1. Ob `storeilm` die Maske so deutet, wie die Referenz es beschreibt
 *     (schreiben, wenn das LSB des Maskenbytes 0 ist).
 *  2. Ob `load`/`storeilm` UNAUSGERICHTETE Adressen annehmen. Die Referenz
 *     sagt dazu nichts; der Hinweis „on the 080 it does not have to be
 *     aligned" steht bei MOVE16, nicht bei AMMX. Sprite-Zeilen beginnen auf
 *     beliebigen Bytes, die Routine setzt es also voraus.
 *
 * Stimmt etwas nicht, wird AMMX abgeschaltet und der C-Weg benutzt --
 * sichtbar im Protokoll, nicht als verfälschtes Bild mitten im Spiel.
 *
 * Gegen einen UNGÜLTIGEN Opcode hilft das nicht (dann steht ein Guru an).
 * Deshalb steht vor jedem Prüfschritt eine Protokollzeile: Auf AmigaOS ist
 * die serielle Ausgabe synchron, die letzte Zeile vor einem Guru benennt
 * also genau den Schritt, der ihn ausgelöst hat.
 */

#include "ammx.h"

#include "zod_log.h"

#ifdef __amigaos__
#include <exec/execbase.h>
#include <proto/exec.h>

extern struct ExecBase *SysBase;

/* Die Apollo-Erweiterung der AttnFlags. Das NDK kennt sie nicht; der Wert
 * ist über tests/amiga_hello/main.cpp belegt, und die auf der V1200 des
 * Nutzers gemessenen AttnFlags 0x847F haben Bit 10 gesetzt
 * (0x8000 + 0x400 + 0x7F). */
#ifndef AFB_68080
#define AFB_68080 10
#endif
#endif

#ifdef ZOD_AMMX
/* port/amiga/ammx_blit.s */
extern "C" void zod_ammx_blit_row(unsigned char *dst, const unsigned char *src,
                                  unsigned long count, const unsigned char *key8);
/* Sonderfall Schluessel 0 ueber STOREM3 im Bytemodus -- zwei Befehle je acht
 * Bildpunkte statt drei. */
extern "C" void zod_ammx_blit_rect(unsigned char *dst, const unsigned char *src,
                                   unsigned long w, unsigned long h,
                                   long dpitch, long spitch,
                                   const unsigned char *key8);
extern "C" void zod_ammx_blit_rect_k0(unsigned char *dst, const unsigned char *src,
                                      unsigned long w, unsigned long h,
                                      long dpitch, long spitch);

extern "C" void zod_ammx_blit_row_k0(unsigned char *dst, const unsigned char *src,
                                     unsigned long count);
#endif

static int ammx_ok = 0;
static int ammx_geprueft = 0;

/* Die Vergleichsfassung -- wortgleich mit der Schleife in sdl_video.cpp. */
static void blit_row_c(unsigned char *dst, const unsigned char *src,
                       unsigned long count, unsigned char key)
{
	for(unsigned long x = 0; x < count; x++)
	{
		const unsigned char v = src[x];

		if(v != key) dst[x] = v;
	}
}

int zod_ammx_available(void)
{
	return ammx_ok;
}

void zod_ammx_init(void)
{
	if(ammx_geprueft) return;

	ammx_geprueft = 1;
	ammx_ok = 0;

#ifndef ZOD_AMMX
	/* Nicht hineingebaut: 040/060-Bauten enthalten keinen einzigen
	 * AMMX-Opcode. Das ist stärker als eine Laufzeitprüfung. */
	ZLOG("AMMX: nicht eingebaut (nur im 68080-Bau), C-Weg\n");
#else
#ifndef __amigaos__
	ZLOG("AMMX: keine Amiga-Plattform, C-Weg\n");
#else
	const UWORD attn = SysBase->AttnFlags;

	if(!(attn & (1 << AFB_68080)))
	{
		/* Der Regelfall im Emulator: dort steht ein 68040. */
		ZLOG("AMMX: kein 68080 (AttnFlags 0x%lx), C-Weg\n", (ULONG)attn);
		return;
	}

	ZLOG("AMMX: 68080 erkannt (AttnFlags 0x%lx), Selbsttest laeuft\n", (ULONG)attn);

	/* Puffer mit Vorlauf, damit jede Ausrichtung 0..7 geprüft werden kann,
	 * und mit Nachlauf, damit ein Schreibzugriff über das Ende hinaus
	 * auffällt. */
	static unsigned char quelle[64];
	static unsigned char ziel_a[64];
	static unsigned char ziel_c[64];
	static unsigned char key8[8];

	const unsigned char key = 0;   /* Platz 0 ist der Farbschlüssel */
	int fehler = 0;

	for(int i = 0; i < 8; i++) key8[i] = key;

	/* Muster mit Schlüsselbytes an wechselnden Stellen, damit die Maske in
	 * jedem Byte einer Achtergruppe einmal greift. */
	for(int i = 0; i < 64; i++)
		quelle[i] = (unsigned char)((i % 5) == 0 ? key : (i + 1));

	for(int aus_s = 0; aus_s < 8 && !fehler; aus_s++)
		for(int aus_d = 0; aus_d < 8 && !fehler; aus_d++)
			for(unsigned long n = 1; n <= 17 && !fehler; n++)
			{
				int i;

				for(i = 0; i < 64; i++) ziel_a[i] = ziel_c[i] = 0xAA;

				blit_row_c(ziel_c + aus_d, quelle + aus_s, n, key);

				/* Ab hier kann es knallen. Die Zeile steht VOR dem
				 * Aufruf, damit ein Guru zuordenbar ist -- aber nur
				 * beim ersten Durchgang, sonst 1088 Zeilen. */
				if(aus_s == 0 && aus_d == 0 && n == 1)
					ZLOG("AMMX: erster storeilm-Aufruf (Ausrichtung 0/0, 1 Byte)\n");

				if(aus_s == 0 && aus_d == 1 && n == 9)
					ZLOG("AMMX: erster unausgerichteter Achterblock\n");

				/* Beide Wege pruefen: der allgemeine (pcmpeqb +
				 * storeilm) und der Sonderfall fuer Schluessel 0
				 * (storem3 im Bytemodus). Der Selbsttest laeuft
				 * abwechselnd, damit beide ueber alle Ausrichtungen
				 * und Laengen kommen. */
				if((aus_s + aus_d + (int)n) & 1)
					zod_ammx_blit_row(ziel_a + aus_d, quelle + aus_s, n, key8);
				else
					zod_ammx_blit_row_k0(ziel_a + aus_d, quelle + aus_s, n);

				for(i = 0; i < 64; i++)
					if(ziel_a[i] != ziel_c[i])
					{
						ZLOG("AMMX: FEHLER bei Ausrichtung %ld/%ld, %ld Byte: "
						     "Versatz %ld ist 0x%lx, erwartet 0x%lx\n",
						     (LONG)aus_s, (LONG)aus_d, (LONG)n, (LONG)i,
						     (ULONG)ziel_a[i], (ULONG)ziel_c[i]);
						fehler = 1;
						break;
					}
			}

	/* ---- Rechteckfassungen -------------------------------------------
	 *
	 * Sie tragen die Zeilenschleife selbst. Das ist der Teil, den KEIN
	 * Emulatorlauf je ausfuehrt (FS-UAE hat einen 68040-Kern, dort greift
	 * der C-Rueckfall) -- dieser Selbsttest auf der echten Maschine ist die
	 * einzige Pruefung, die es gibt. Deshalb mit verschiedenen und ungeraden
	 * Zeilenabstaenden fuer Quelle und Ziel: ein vertauschtes oder
	 * verschluckt es Register faellt sonst nicht auf. */
	if(!fehler)
	{
		static unsigned char q_r[256], a_r[256], c_r[256];

		const int ps = 21, pd = 23;   /* ungerade UND verschieden */

		for(int i = 0; i < 256; i++)
			q_r[i] = (unsigned char)((i % 5) == 0 ? key : (i + 3));

		for(int aus = 0; aus < 8 && !fehler; aus++)
			for(unsigned long n = 1; n <= 17 && !fehler; n++)
				for(int hh = 1; hh <= 5 && !fehler; hh++)
				{
					int i;

					for(i = 0; i < 256; i++) a_r[i] = c_r[i] = 0xAA;

					for(i = 0; i < hh; i++)
						blit_row_c(c_r + aus + i * pd, q_r + aus + i * ps, n, key);

					if(aus == 0 && n == 1 && hh == 1)
						ZLOG("AMMX: erster Rechteck-Aufruf\n");

					if((aus + (int)n + hh) & 1)
						zod_ammx_blit_rect(a_r + aus, q_r + aus, n,
						                   (unsigned long)hh, pd, ps, key8);
					else
						zod_ammx_blit_rect_k0(a_r + aus, q_r + aus, n,
						                      (unsigned long)hh, pd, ps);

					for(i = 0; i < 256; i++)
						if(a_r[i] != c_r[i])
						{
							ZLOG("AMMX: FEHLER Rechteck bei Ausrichtung %ld, "
							     "%ld Byte, %ld Zeilen: Versatz %ld ist 0x%lx, "
							     "erwartet 0x%lx\n",
							     (LONG)aus, (LONG)n, (LONG)hh, (LONG)i,
							     (ULONG)a_r[i], (ULONG)c_r[i]);
							fehler = 1;
							break;
						}
				}
	}

	if(fehler)
	{
		ZLOG("AMMX: Selbsttest NICHT bestanden -- AMMX bleibt aus, C-Weg\n");
		return;
	}

	ammx_ok = 1;
	ZLOG("AMMX: Selbsttest bestanden (Zeile und Rechteck, 8x8 Ausrichtungen, "
	     "1..17 Byte, 1..5 Zeilen), aktiv\n");
#endif
#endif
}
