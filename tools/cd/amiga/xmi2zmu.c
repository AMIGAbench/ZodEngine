/*
 * xmi2zmu.c -- die Musik der Z-CD in den Abspielstrom ZMU1 umsetzen.
 *
 * Die 81 Musikstuecke liegen als XMI-Eintraege im z.pac, also in genau dem
 * Container, den ZExtract ohnehin oeffnet und entpackt. Der schwierige Teil
 * -- das Packverfahren -- ist damit schon erledigt; hier geht es nur noch
 * um das Format der entpackten Daten.
 *
 * DIE ABNAHME IST BYTEWEISE. tools/cd/xmi2zmu.py erzeugt dieselben Dateien
 * auf dem Host. Weichen sie voneinander ab, ist eine der beiden Fassungen
 * falsch -- genau so ist es bei den Bild- und Klangarchiven gemacht worden
 * (sieben Archive, cmp-gleich).
 *
 * DREI EIGENHEITEN VON XMI, die man nicht raten darf:
 *
 *  1. Die Wartezeit ist KEINE variable Laenge, sondern eine Folge von Bytes
 *     unter 0x80, deren Werte ADDIERT werden. Ein Byte ab 0x80 beendet sie
 *     und ist der Statuscode.
 *
 *  2. Es gibt KEIN Note-Off. Auf ein Note-On folgen Note, Anschlag und eine
 *     DAUER als variable Laenge; das Note-Off muss daraus erzeugt und
 *     zeitrichtig einsortiert werden.
 *
 *  3. Ein XMI-Schritt ist fest 1/120 Sekunde. Tempo-Ereignisse beschreiben
 *     nur, wie der Takt zu LESEN ist, und fallen hier weg.
 *
 * UND EINE EIGENHEIT DIESER UMSETZUNG, ohne die die Dateien NICHT gleich
 * werden: Die laufende Nummer, die bei gleicher Zeit die Reihenfolge
 * entscheidet, zaehlt AUCH fuer Ereignisse, die hier wegfallen (Meta,
 * Sysex, Aftertouch, Controller ausser Lautstaerke). Wer nur die
 * uebernommenen zaehlt, bekommt an Stellen mit gleichzeitigen Ereignissen
 * eine andere Reihenfolge.
 */
#ifdef __amigaos__
#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>

#include <proto/exec.h>
#include <proto/dos.h>
#else
#include "hostshim.h"		/* derselbe Code, auf dem Host pruefbar */
#endif

#include <stdio.h>      /* snprintf -- NICHT sprintf: das kommt hier
                        * aus libamiga.a und liest %d als 16 Bit */
#include <string.h>

#include "zextract.h"

/* Ereignisarten im Strom -- siehe tools/cd/xmi2zmu.py */
#define ZMU_AN		0
#define ZMU_AUS		1
#define ZMU_PROG	2
#define ZMU_VOL		3
#define ZMU_ENDE	4
#define ZMU_WARTEN	5

#define ZMU_TAKT	120

struct Ereignis
{
	ULONG	zeit;		/* Schritte zu 1/120 s */
	ULONG	nr;		/* Reihenfolge bei gleicher Zeit */
	UBYTE	art;
	UBYTE	n;		/* Zahl der Datenbytes */
	UBYTE	d[3];
};


static BOOL vlq_lesen(const UBYTE *p, ULONG *i, ULONG ende, ULONG *wert)
{
	ULONG v = 0;
	LONG k;

	for(k = 0; k < 4; k++)
	{
		UBYTE b;

		if(*i >= ende) return FALSE;

		b = p[(*i)++];
		v = (v << 7) | (ULONG)(b & 0x7F);

		if(!(b & 0x80)) { *wert = v; return TRUE; }
	}

	return FALSE;
}

static ULONG be32_lesen(const UBYTE *p)
{
	return ((ULONG)p[0] << 24) | ((ULONG)p[1] << 16) |
	       ((ULONG)p[2] << 8) | (ULONG)p[3];
}

/* Nach (zeit, nr) sortieren.
 *
 * Shellsort: kein Bibliotheksaufruf, keine Rekursion, kein zusaetzlicher
 * Speicher. Eine Einfuegesortierung waere hier quadratisch -- das laengste
 * Stueck hat ueber 30 000 Ereignisse, das waeren rund eine Milliarde
 * Schritte auf einem 68k. */
static void sortieren(struct Ereignis *v, LONG n)
{
	LONG luecke;

	for(luecke = n / 2; luecke > 0; luecke /= 2)
	{
		LONG i;

		for(i = luecke; i < n; i++)
		{
			struct Ereignis t = v[i];
			LONG j = i;

			while(j >= luecke &&
			      (v[j - luecke].zeit > t.zeit ||
			       (v[j - luecke].zeit == t.zeit &&
			        v[j - luecke].nr > t.nr)))
			{
				v[j] = v[j - luecke];
				j -= luecke;
			}

			v[j] = t;
		}
	}
}

/* Einen EVNT-Bereich in Ereignisse zerlegen.
 * Rueckgabe: Zahl der uebernommenen Ereignisse, -1 bei Formatfehler. */
static LONG evnt_lesen(const UBYTE *daten, ULONG a, ULONG e,
                       struct Ereignis *aus, LONG platz)
{
	ULONG p = a;
	ULONG zeit = 0;
	ULONG nr = 0;
	LONG n = 0;

	while(p < e)
	{
		UBYTE status;

		/* 1. Wartezeit: Folge von Bytes < 0x80, Werte werden ADDIERT */
		while(p < e && daten[p] < 0x80) zeit += (ULONG)daten[p++];

		if(p >= e) break;

		status = daten[p++];

		if(status == 0xFF)
		{
			UBYTE typ;
			ULONG laenge;

			if(p >= e) return -1;

			typ = daten[p++];

			if(!vlq_lesen(daten, &p, e, &laenge)) return -1;
			if(p + laenge > e) return -1;

			p += laenge;

			if(typ == 0x2F) break;		/* Spurende */

			nr++;				/* faellt weg, zaehlt mit */
		}
		else if(status == 0xF0 || status == 0xF7)
		{
			ULONG laenge;

			if(!vlq_lesen(daten, &p, e, &laenge)) return -1;
			if(p + laenge > e) return -1;

			p += laenge;
			nr++;				/* faellt weg, zaehlt mit */
		}
		else if((status & 0xF0) == 0x90)
		{
			UBYTE note, anschlag;
			ULONG dauer;

			if(p + 2 > e) return -1;

			note = daten[p];
			anschlag = daten[p + 1];
			p += 2;

			if(!vlq_lesen(daten, &p, e, &dauer)) return -1;

			if(n + 2 > platz) return -1;

			aus[n].zeit = zeit;
			aus[n].nr = nr++;
			aus[n].art = ZMU_AN;
			aus[n].n = 3;
			aus[n].d[0] = (UBYTE)(status & 0x0F);
			aus[n].d[1] = note;
			aus[n].d[2] = anschlag;
			n++;

			/* Das Note-Off aus der Dauer erzeugen. Es bekommt die
			 * NAECHSTE Nummer, auch wenn es zeitlich viel spaeter
			 * liegt -- das entscheidet nur Gleichstaende. */
			aus[n].zeit = zeit + dauer;
			aus[n].nr = nr++;
			aus[n].art = ZMU_AUS;
			aus[n].n = 2;
			aus[n].d[0] = (UBYTE)(status & 0x0F);
			aus[n].d[1] = note;
			n++;
		}
		else if((status & 0xF0) == 0xC0 || (status & 0xF0) == 0xD0)
		{
			if(p >= e) return -1;

			if((status & 0xF0) == 0xC0)
			{
				if(n >= platz) return -1;

				aus[n].zeit = zeit;
				aus[n].nr = nr;
				aus[n].art = ZMU_PROG;
				aus[n].n = 2;
				aus[n].d[0] = (UBYTE)(status & 0x0F);
				aus[n].d[1] = daten[p];
				n++;
			}

			nr++;
			p += 1;
		}
		else if((status & 0xF0) == 0x80 || (status & 0xF0) == 0xA0 ||
		        (status & 0xF0) == 0xB0 || (status & 0xF0) == 0xE0)
		{
			if(p + 2 > e) return -1;

			if((status & 0xF0) == 0x80)
			{
				if(n >= platz) return -1;

				aus[n].zeit = zeit;
				aus[n].nr = nr;
				aus[n].art = ZMU_AUS;
				aus[n].n = 2;
				aus[n].d[0] = (UBYTE)(status & 0x0F);
				aus[n].d[1] = daten[p];
				n++;
			}
			else if((status & 0xF0) == 0xB0 && daten[p] == 7)
			{
				if(n >= platz) return -1;

				aus[n].zeit = zeit;
				aus[n].nr = nr;
				aus[n].art = ZMU_VOL;
				aus[n].n = 2;
				aus[n].d[0] = (UBYTE)(status & 0x0F);
				aus[n].d[1] = daten[p + 1];
				n++;
			}

			nr++;
			p += 2;
		}
		else
		{
			return -1;			/* unbekannter Statuscode */
		}
	}

	return n;
}

/* Den Ereignisstrom als ZMU1 schreiben. */
static BOOL zmu_schreiben(const char *pfad, struct Ereignis *v, LONG n)
{
	UBYTE *strom;
	LONG platz;
	LONG i;
	LONG len = 0;
	ULONG letzte = 0;
	ULONG anzahl = 0;
	UBYTE kopf[16];
	BPTR f;
	BOOL ok;

	/* Hoechstens 8 Byte je Ereignis (Fluchtwert + Wartezeit + Art + 3
	 * Daten), dazu das Schlussereignis. */
	platz = n * 8 + 16;
	strom = AllocVec((ULONG)platz, MEMF_ANY);

	if(!strom) return FALSE;

	for(i = 0; i < n; i++)
	{
		ULONG delta = v[i].zeit - letzte;
		LONG k;

		/* Pausen ueber 65535 Schritte in Stuecke zerlegen, jedes mit
		 * einem reinen Warteereignis abgeschlossen. Kommt auf dieser CD
		 * nicht vor -- aber ein Format, das an einer Stelle einfach
		 * abbricht, ist kein Format. */
		while(delta > 0xFFFF)
		{
			strom[len++] = 255;
			strom[len++] = (UBYTE)(0xFFFF >> 8);
			strom[len++] = (UBYTE)(0xFFFF & 0xFF);
			strom[len++] = ZMU_WARTEN;
			delta -= 0xFFFF;
			anzahl++;
		}

		if(delta < 255)
		{
			strom[len++] = (UBYTE)delta;
		}
		else
		{
			strom[len++] = 255;
			strom[len++] = (UBYTE)(delta >> 8);
			strom[len++] = (UBYTE)(delta & 0xFF);
		}

		strom[len++] = v[i].art;

		for(k = 0; k < (LONG)v[i].n; k++) strom[len++] = v[i].d[k];

		letzte = v[i].zeit;
		anzahl++;
	}

	strom[len++] = 0;
	strom[len++] = ZMU_ENDE;
	anzahl++;

	kopf[0] = 'Z'; kopf[1] = 'M'; kopf[2] = 'U'; kopf[3] = '1';
	kopf[4] = (UBYTE)(ZMU_TAKT >> 8);
	kopf[5] = (UBYTE)(ZMU_TAKT & 0xFF);
	kopf[6] = 0; kopf[7] = 0;
	kopf[8]  = (UBYTE)(anzahl >> 24);
	kopf[9]  = (UBYTE)(anzahl >> 16);
	kopf[10] = (UBYTE)(anzahl >> 8);
	kopf[11] = (UBYTE)(anzahl);
	kopf[12] = (UBYTE)(letzte >> 24);
	kopf[13] = (UBYTE)(letzte >> 16);
	kopf[14] = (UBYTE)(letzte >> 8);
	kopf[15] = (UBYTE)(letzte);

	f = Open(pfad, MODE_NEWFILE);

	if(!f) { FreeVec(strom); return FALSE; }

	ok = (Write(f, kopf, 16) == 16) && (Write(f, strom, len) == len);

	Close(f);
	FreeVec(strom);

	return ok;
}

/* IFF durchgehen und jeden EVNT-Bereich umsetzen.
 *
 * IFF-Brocken sind auf GERADE Laenge aufgefuellt, und das Fuellbyte zaehlt
 * NICHT zur Laenge. Wer das vergisst, verrutscht ab dem ersten Brocken
 * ungerader Laenge -- und das faellt erst weit spaeter auf.
 *
 * Rueckgabe: Zahl der geschriebenen Stroeme, -1 bei Fehler. */
LONG xmi_nach_zmu(const UBYTE *xmi, ULONG len, const char *verzeichnis,
                  const char *basis)
{
	ULONG stapel_a[8], stapel_e[8];
	LONG tiefe = 0;
	LONG geschrieben = 0;
	ULONG p = 0, ende = len;

	if(len < 12 || memcmp(xmi, "FORM", 4) != 0) return -1;

	/* Ohne Rekursion: ein Stapel ueber die offenen FORM/CAT-Bereiche. */
	for(;;)
	{
		if(p + 8 > ende)
		{
			if(tiefe == 0) break;

			tiefe--;
			p = stapel_a[tiefe];
			ende = stapel_e[tiefe];

			continue;
		}

		{
			const UBYTE *k = xmi + p;
			ULONG laenge = be32_lesen(xmi + p + 4);
			ULONG inhalt = p + 8;

			if(inhalt + laenge > len) return -1;

			if(memcmp(k, "FORM", 4) == 0 || memcmp(k, "CAT ", 4) == 0)
			{
				if(tiefe >= 8) return -1;

				/* Weiter NACH diesem Brocken, wenn der Inhalt
				 * abgearbeitet ist. Hinter FORM/CAT steht noch
				 * eine Kennung (XDIR bzw. XMID). */
				stapel_a[tiefe] = inhalt + laenge + (laenge & 1);
				stapel_e[tiefe] = ende;
				tiefe++;

				p = inhalt + 4;
				ende = inhalt + laenge;

				continue;
			}

			if(memcmp(k, "EVNT", 4) == 0)
			{
				struct Ereignis *v;
				LONG platz = (LONG)(laenge + 16);
				LONG n;
				char pfad[256];

				v = AllocVec((ULONG)platz * sizeof(struct Ereignis),
				             MEMF_ANY);

				if(!v) return -1;

				n = evnt_lesen(xmi, inhalt, inhalt + laenge, v, platz);

				if(n < 0) { FreeVec(v); return -1; }

				sortieren(v, n);

				/* Erst ab dem zweiten Strom eine Nummer anhaengen
				 * -- genau wie die Python-Fassung. */
				if(geschrieben == 0)
					snprintf(pfad, sizeof(pfad), "%s/%s.zmu",
					         verzeichnis, basis);
				else
					snprintf(pfad, sizeof(pfad), "%s/%s_%02d.zmu",
					         verzeichnis, basis, (int)geschrieben);

				if(!zmu_schreiben(pfad, v, n)) { FreeVec(v); return -1; }

				FreeVec(v);
				geschrieben++;
			}

			p = inhalt + laenge + (laenge & 1);
		}
	}

	return geschrieben;
}
