/* Leser fuer die JMP2-Container der Z-CD. Format und Herleitung: jmp2.h */

#include "jmp2.h"

#include <string.h>

#ifdef __amigaos__
#include <proto/exec.h>
#endif

/* Little-Endian lesen. Die CD kommt aus der DOS-Welt; auf dem Amiga darf
 * dafuer NICHT einfach ein ULONG-Zeiger genommen werden -- weder die
 * Byte-Reihenfolge noch die Ausrichtung stimmen. */
static ULONG le32(const UBYTE *p)
{
	return (ULONG)p[0]
	     | ((ULONG)p[1] << 8)
	     | ((ULONG)p[2] << 16)
	     | ((ULONG)p[3] << 24);
}

LONG jmp2_verzeichnis(const UBYTE *datei, ULONG len,
                      struct Jmp2Eintrag *aus, LONG platz)
{
	ULONG anzahl, verz_ab;
	LONG i;

	if(!datei || !aus || platz <= 0) return -1;

	/* Fuss: u32 Anzahl + "JMP2". Kleiner als das kann kein Container sein. */
	if(len < 8 + 24) return -1;

	if(memcmp(datei + len - 4, "JMP2", 4) != 0) return -1;

	anzahl = le32(datei + len - 8);

	/* Vor dem Vertrauen pruefen: die Zahl kommt aus einer Datei.
	 * 24 Byte je Eintrag plus der 8-Byte-Fuss muessen hineinpassen. */
	if(anzahl == 0 || anzahl > (ULONG)platz) return -1;
	if(anzahl > (len - 8) / 24) return -1;

	verz_ab = len - 8 - anzahl * 24;

	for(i = 0; i < (LONG)anzahl; i++)
	{
		const UBYTE *e = datei + verz_ab + (ULONG)i * 24;
		LONG k;

		for(k = 0; k < 16; k++) aus[i].name[k] = (char)e[k];

		aus[i].name[16] = 0;

		/* Der Name ist mit Nullbytes aufgefuellt -- strlen genuegt,
		 * weil das 17. Byte oben gesetzt wurde. */
		aus[i].roh_ab = le32(e + 16);
		aus[i].soll   = le32(e + 20);
		aus[i].roh_len = 0;			/* gleich unten */
	}

	/* Die gepackten Daten laufen bis zum Versatz des NAECHSTEN Eintrags,
	 * beim letzten bis zum Verzeichnisanfang. Das steht nirgends in der
	 * Datei und ist der Grund, warum das Verzeichnis am Stueck gelesen
	 * werden muss statt Eintrag fuer Eintrag. */
	for(i = 0; i < (LONG)anzahl; i++)
	{
		ULONG ende = (i + 1 < (LONG)anzahl) ? aus[i + 1].roh_ab : verz_ab;

		if(aus[i].roh_ab >= ende || ende > len) return -1;

		aus[i].roh_len = ende - aus[i].roh_ab;
	}

	return (LONG)anzahl;
}

BOOL jmp2_entpacken(const UBYTE *quelle, ULONG quell_len,
                    UBYTE *ziel, ULONG soll)
{
	/* DER RING GEHOERT NICHT AUF DEN STAPEL.
	 *
	 * Hier stand `UBYTE ring[4096]` mit der Begruendung, 4 KB seien
	 * vertretbar. Das war falsch: Der Vorgabestapel einer AmigaOS-Shell
	 * ist traditionell GENAU 4096 Byte, und der Installer startet
	 * ZExtract ohne eigene Stapelangabe -- der Ring allein haette den
	 * ganzen Stapel verbraucht, samt allem, was darunter liegt.
	 *
	 * Im Emulator faellt das nie auf: tools/run.sh setzt 262144. Genau so
	 * entsteht ein Fehler, der nur auf der Maschine des Nutzers auftritt.
	 *
	 * Diese Laufzeit kennt kein __stack (nachgesehen: libnix hat nur
	 * __markstack/__checkstack, keine Stapelvergroesserung), also bleibt
	 * nur, den Speicher woanders zu holen. 4 KB je Aufruf sind gegen das
	 * Entpacken eines 64-KB-Bildes nichts. */
	UBYTE *ring;
	ULONG r = 1;			/* siehe jmp2.h: beginnt bei 1, nicht 0 */
	ULONG i = 0, aus = 0;
	UBYTE flags = 0;
	int cnt = 0;

	if(!quelle || !ziel) return FALSE;

	ring = (UBYTE *)AllocVec(4096, MEMF_ANY);

	if(!ring) return FALSE;

	memset(ring, 0x20, 4096);

	while(aus < soll && i < quell_len)
	{
		if(cnt == 0)
		{
			flags = quelle[i++];
			cnt = 8;

			if(i >= quell_len && aus < soll) break;
		}

		if(flags & 0x80)
		{
			UBYTE c;

			if(i >= quell_len) break;

			c = quelle[i++];
			ziel[aus++] = c;
			ring[r] = c;
			r = (r + 1) & 0x0FFF;
		}
		else
		{
			UBYTE a, b;
			ULONG off;
			int ln, k;

			if(i + 1 >= quell_len) break;

			a = quelle[i++];
			b = quelle[i++];

			off = ((ULONG)b << 4) | ((ULONG)a >> 4);
			ln  = (a & 0x0F) + 2;

			for(k = 0; k < ln; k++)
			{
				UBYTE c;

				/* Die Laenge kann ueber das Soll hinausgehen --
				 * der Packer denkt in Treffern, nicht in
				 * Dateigrenzen. Abschneiden, nicht ueberlaufen. */
				if(aus >= soll) break;

				c = ring[(off + (ULONG)k) & 0x0FFF];
				ziel[aus++] = c;
				ring[r] = c;
				r = (r + 1) & 0x0FFF;
			}
		}

		flags = (UBYTE)(flags << 1);
		cnt--;
	}

	FreeVec(ring);

	/* GENAU soll Byte. Weniger heisst abgebrochen, und mehr kann nicht
	 * entstehen (oben abgeschnitten). Diese Pruefung IST die Gegenprobe
	 * des Formats -- siehe jmp2.h. */
	return aus == soll;
}
