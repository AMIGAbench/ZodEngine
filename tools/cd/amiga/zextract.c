/*
 * ZExtract -- holt die Spieldaten der Zod-Engine von der Original-CD von *Z*
 * (The Bitmap Brothers, 1996) und schreibt die Archive, die die Engine laedt.
 *
 *     ZExtract <quelle> [ziel]
 *
 *     <quelle>  CD0:  oder ein Verzeichnis, in dem z.pac liegt
 *     [ziel]    Vorgabe PROGDIR:packs (das Verzeichnis neben ZExtract)
 *
 * Warum es das gibt: Ausgeliefert wird nur die Engine. Wer sie spielen will,
 * braucht seine eigene CD. Und er soll dafuer **keinen PC brauchen** -- dieses
 * Programm leistet Zug um Zug dasselbe wie `tools/cd/extract.py` unter Linux
 * und erzeugt bitgleiche Archive.
 *
 * Was hier geschieht
 * ------------------
 *   1. z.pac oeffnen, sein Verzeichnis lesen (343 Eintraege, 29 Byte je
 *      Eintrag, Little-Endian -- die CD kommt aus der DOS-Welt).
 *   2. SPRITES.RSC entpacken (3 920 557 Byte). Das Packverfahren ist keines
 *      der bekannten; es wurde fuer dieses Projekt erschlossen, die
 *      Beschreibung steht in tools/cd/nicecomp.py.
 *   3. Jeden gebrauchten Satz aus der Ebenenfolge des VGA-Modus X in ein
 *      lineares Bild zurueckrechnen und die Farbindizes ueber eine
 *      256-Byte-Tabelle auf die gemeinsame Palette der Engine bringen.
 *   4. Die Klaenge entpacken und von vorzeichenlosen auf vorzeichenbehaftete
 *      8 Bit bringen, wie AHI sie erwartet.
 *   5. Alles in .zpk-Archive schreiben (Big-Endian, also die Reihenfolge
 *      dieser Maschine -- hier faellt kein Byte-Tausch an).
 *
 * Speicher: Es liegt nie alles gleichzeitig im Speicher. Gross ist allein
 * SPRITES.RSC mit knapp 4 MB; die Archive werden im Strom geschrieben, weil
 * alle Groessen vorher feststehen. Grosse Bloecke ueber AllocVec, nicht ueber
 * malloc -- libnix gibt Freies nicht ans System zurueck.
 *
 * Fallen dieser Werkzeugkette, die hier bewusst beachtet sind:
 *   - **kein sprintf.** Es liest %d als 16 Bit. Ausgaben laufen ueber Printf
 *     der dos.library (%ld fuer 32 Bit), Pfade werden zusammengesetzt.
 *   - keine Kommazahl laeuft durch Text, also ist __decimalpoint hier ohne
 *     Belang.
 *   - Elternverzeichnis auf AmigaDOS ist "/", nicht "../".
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

#include <string.h>

#include "zextract.h"
#include "schirme.h"

/* Zusaetzlich zur Konsole geht das Ergebnis auf den seriellen Kanal, damit
 * tools/run.sh einen Lauf im Emulator selbst auswerten kann. Auf dem Host
 * ist das ein leerer Platzhalter. */
#ifdef __amigaos__
#include "debug.h"
#else
static void dbg_boot(void) { }
static void dbg_ok(const char *m) { (void)m; }
static void dbg_fail(const char *m) { (void)m; }
#endif

#define VERZ_START	0x81		/* Verzeichnis von z.pac */
#define EINTRAG_GROESSE	29
#define LETZTER_EINTRAG	21		/* ohne Endfeld, siehe verzeichnis_lesen */
#define MAX_EINTRAEGE	512
#define BLOCK_KLARTEXT	65000L		/* ein Block entpackt zu so vielen Byte */

#define BLOCK_ROH	1		/* unkomprimiert */

#define FMT_PCM8	3
#define FMT_SHARED8	4

#define GRUPPE_KLANG	8		/* Index von "sounds" in zx_gruppen */

struct PacEintrag
{
	UBYTE	name[12];
	ULONG	groesse;		/* entpackt */
	ULONG	anfang;			/* erstes Byte der gepackten Daten */
	ULONG	ende;			/* erstes Byte dahinter */
};

static BPTR			pac;
static struct PacEintrag	*verz;
static LONG			n_verz;

static UBYTE			*rsc;		/* SPRITES.RSC, entpackt */
static LONG			rsc_len;
static LONG			rsc_saetze;

static LONG			fehler;


/* ---- kleine Helfer ------------------------------------------------- */

static ULONG le16(const UBYTE *p)
{
	return (ULONG)p[0] | ((ULONG)p[1] << 8);
}

static ULONG le32(const UBYTE *p)
{
	return (ULONG)p[0] | ((ULONG)p[1] << 8)
	     | ((ULONG)p[2] << 16) | ((ULONG)p[3] << 24);
}

static void be16(UBYTE *p, ULONG v)
{
	p[0] = (UBYTE)(v >> 8);
	p[1] = (UBYTE)v;
}

static void be32(UBYTE *p, ULONG v)
{
	p[0] = (UBYTE)(v >> 24);
	p[1] = (UBYTE)(v >> 16);
	p[2] = (UBYTE)(v >> 8);
	p[3] = (UBYTE)v;
}

static BOOL lies_bei(BPTR f, LONG pos, void *puffer, LONG anzahl)
{
	if(Seek(f, pos, OFFSET_BEGINNING) < 0) return FALSE;

	return Read(f, puffer, anzahl) == anzahl;
}

/* Pfad zusammensetzen -- AmigaDOS trennt mit "/", nach einem ":" aber nicht. */
static void pfad_bauen(char *ziel, LONG platz, const char *verzeichnis,
                       const char *name)
{
	LONG n = (LONG)strlen(verzeichnis);

	if(n >= platz - 2) n = platz - 2;

	memcpy(ziel, verzeichnis, n);

	if(n > 0 && ziel[n - 1] != ':' && ziel[n - 1] != '/')
		ziel[n++] = '/';

	strncpy(ziel + n, name, platz - n - 1);
	ziel[platz - 1] = 0;
}


/* ---- z.pac: Verzeichnis -------------------------------------------- */

/* Die Zahl der Eintraege steht NICHT im Kopf. Sie wird abgetastet: Name
 * druckbar und Ende monoton steigend, bis es bricht. Hinter dem Verzeichnis
 * beginnen die gepackten Daten, dort trifft beides nicht mehr zu. */
static BOOL verzeichnis_lesen(void)
{
	UBYTE roh[EINTRAG_GROESSE];
	UBYTE kopf[8];
	ULONG dateiende;
	ULONG letztes = 0;
	LONG i;

	if(!lies_bei(pac, 0, kopf, 8)) return FALSE;

	if(kopf[0] != 'N' || kopf[1] != 'I')
	{
		Printf("z.pac hat keine NI-Kennung -- ist das die Z-CD?\n");

		return FALSE;
	}

	if(Seek(pac, 0, OFFSET_END) < 0) return FALSE;

	dateiende = (ULONG)Seek(pac, 0, OFFSET_CURRENT);

	verz = AllocVec(sizeof(struct PacEintrag) * MAX_EINTRAEGE, MEMF_ANY);

	if(!verz) return FALSE;

	n_verz = 0;

	for(i = 0; i < MAX_EINTRAEGE; i++)
	{
		ULONG ende;
		LONG k;
		BOOL druckbar = TRUE;

		if(!lies_bei(pac, VERZ_START + i * EINTRAG_GROESSE, roh,
		             EINTRAG_GROESSE))
			break;

		for(k = 0; k < 12; k++)
			if(roh[k] < 32 || roh[k] > 126) druckbar = FALSE;

		if(!druckbar) break;

		ende = le32(roh + 21);

		/* Das Ende des LETZTEN Eintrags ist unbrauchbar; seine Daten
		 * laufen bis zum Dateiende. Erkannt daran, dass danach kein
		 * druckbarer Name mehr folgt -- das prueft die naechste Runde,
		 * hier wird erst einmal gekappt. */
		if(ende < letztes || ende > dateiende) ende = dateiende;

		memcpy(verz[n_verz].name, roh, 12);
		verz[n_verz].groesse = le32(roh + 16);
		verz[n_verz].ende = ende;
		verz[n_verz].anfang = (n_verz == 0)
			? (ULONG)(VERZ_START + 0) /* wird unten berichtigt */
			: verz[n_verz - 1].ende;
		n_verz++;
		letztes = ende;
	}

	if(n_verz == 0) return FALSE;

	/* Der Datenbeginn steht erst fest, wenn die Eintragszahl bekannt ist.
	 *
	 * Der LETZTE Eintrag ist verkuerzt: Er traegt Name, Zeitstempel, Groesse
	 * und Kennbyte, aber kein Endfeld -- seine Daten laufen bis zum
	 * Dateiende. Die Daten beginnen deshalb 8 Byte frueher, als die einfache
	 * Rechnung sagt. Probe: Nur so bekommt der erste Eintrag (SAMPLE.AD)
	 * 2770 Byte, und der Block dort meldet 2765 + 5 = 2770. */
	verz[0].anfang = VERZ_START + (n_verz - 1) * EINTRAG_GROESSE
	               + LETZTER_EINTRAG;

	return TRUE;
}

static LONG verz_suche(const char *dos12)
{
	LONG i;

	for(i = 0; i < n_verz; i++)
		if(memcmp(verz[i].name, dos12, 12) == 0) return i;

	return -1;
}


/* ---- z.pac: entpacken ---------------------------------------------- */

/* Bitstrom, LSB zuerst, fortlaufend ueber die Bytes. Beschreibung des
 * Verfahrens: tools/cd/nicecomp.py. */
struct Bits
{
	const UBYTE	*d;
	ULONG		i;
	ULONG		n;
};

static LONG bit(struct Bits *s)
{
	LONG b;

	if(s->i >= s->n) return -1;

	b = (s->d[s->i >> 3] >> (s->i & 7)) & 1;
	s->i++;

	return b;
}

static LONG lies_bits(struct Bits *s, LONG anzahl)
{
	ULONG wert = 0;
	LONG k;

	for(k = 0; k < anzahl; k++)
	{
		LONG b = bit(s);

		if(b < 0) return -1;

		if(b) wert |= (1UL << k);
	}

	return (LONG)wert;
}

/* Zahl der Nullbits bis zur naechsten Eins. */
static LONG praefix(struct Bits *s)
{
	LONG k = 0;
	LONG b;

	while((b = bit(s)) == 0)
	{
		k++;

		if(k > 24) return -1;
	}

	return (b < 0) ? -1 : k;
}

static LONG entpacke_block(const UBYTE *ein, LONG ein_len, UBYTE *aus,
                           LONG soll)
{
	struct Bits s;
	LONG n = 0;

	s.d = ein;
	s.i = 3;			/* drei Bit Vorspann */
	s.n = (ULONG)ein_len * 8;

	while(n < soll)
	{
		LONG laenge, abstand, k, v, b;

		b = bit(&s);

		if(b < 0) return -1;

		if(!b)
		{
			v = lies_bits(&s, 8);

			if(v < 0) return -1;

			aus[n++] = (UBYTE)v;

			continue;
		}

		k = praefix(&s);

		if(k < 0) return -1;

		if(k == 0)
		{
			laenge = 2;
			abstand = lies_bits(&s, 8);
		}
		else
		{
			v = lies_bits(&s, k + 1);

			if(v < 0) return -1;

			if(v >= (1L << k))
			{
				laenge = v + 1;
				abstand = lies_bits(&s, 8);
			}
			else
			{
				/* Fluchtwert: Abstand ueber 255. Im
				 * Normalfall ist das oberste Bit des
				 * Laengenfeldes immer gesetzt, ein kleineres v
				 * ist also frei fuer diese Ankuendigung. */
				LONG k2, w, nieder;

				laenge = v + (1L << k) + 1;
				k2 = praefix(&s);

				if(k2 < 0) return -1;

				w = lies_bits(&s, k2 + 1);
				nieder = lies_bits(&s, 8);

				if(w < 0 || nieder < 0) return -1;

				abstand = (w + (1L << (k2 + 1)) - 1) * 256
				        + nieder;
			}
		}

		if(abstand <= 0 || abstand > n) return -1;

		while(laenge-- > 0 && n < soll)
		{
			aus[n] = aus[n - abstand];
			n++;
		}
	}

	return n;
}

/* Vollstaendiger Inhalt eines Eintrags. Der Puffer kommt von AllocVec und
 * gehoert dem Aufrufer. */
static UBYTE *entpacke_eintrag(LONG idx)
{
	struct PacEintrag *e = &verz[idx];
	UBYTE *aus;
	UBYTE *roh;
	ULONG pos;
	LONG n = 0;
	LONG roh_platz;

	aus = AllocVec(e->groesse ? e->groesse : 1, MEMF_ANY);

	if(!aus) return NULL;

	roh_platz = (LONG)(e->ende - e->anfang);

	if(roh_platz <= 0)
	{
		FreeVec(aus);

		return NULL;
	}

	roh = AllocVec(roh_platz, MEMF_ANY);

	if(!roh)
	{
		FreeVec(aus);

		return NULL;
	}

	if(!lies_bei(pac, (LONG)e->anfang, roh, roh_platz))
	{
		FreeVec(roh);
		FreeVec(aus);

		return NULL;
	}

	/* Bloecke: 1 Byte unklar, 1 Byte Verfahren, 2 Byte Laenge, Nutzlast,
	 * 1 Byte dahinter. Lueckenlos hintereinander. */
	pos = 0;

	while(pos + 4 <= (ULONG)roh_platz && n < (LONG)e->groesse)
	{
		ULONG art = roh[pos + 1];
		ULONG laenge = le16(roh + pos + 2);
		const UBYTE *nutz = roh + pos + 4;
		LONG rest = (LONG)e->groesse - n;
		LONG soll = (rest < BLOCK_KLARTEXT) ? rest : BLOCK_KLARTEXT;

		if(pos + 4 + laenge > (ULONG)roh_platz) break;

		if(art == BLOCK_ROH)
		{
			LONG k = (LONG)laenge;

			if(k > rest) k = rest;

			memcpy(aus + n, nutz, k);
			n += k;
		}
		else
		{
			LONG got = entpacke_block(nutz, (LONG)laenge,
			                          aus + n, soll);

			if(got < 0)
			{
				FreeVec(roh);
				FreeVec(aus);

				return NULL;
			}

			n += got;
		}

		pos += 4 + laenge + 1;
	}

	FreeVec(roh);

	if(n != (LONG)e->groesse)
	{
		FreeVec(aus);

		return NULL;
	}

	return aus;
}


/* ---- SPRITES.RSC --------------------------------------------------- */

static ULONG satz_versatz(LONG r)
{
	if(r >= rsc_saetze) return (ULONG)rsc_len;

	return le32(rsc + 2 + r * 4);
}

/* Einen Satz entflechten und dabei die Farben umsetzen.
 *
 * Die Pixel liegen in der Ebenenfolge des VGA-Modus X: Ebene p haelt alle
 * Spalten mit x mod 4 == p, je Zeile w/4 Byte. */
static BOOL satz_holen(LONG r, UWORD w, UWORD h, const UBYTE *tab, UBYTE *aus)
{
	ULONG ab = satz_versatz(r);
	ULONG bis = satz_versatz(r + 1);
	const UBYTE *roh = rsc + ab;
	LONG len = (LONG)(bis - ab);
	LONG sp, p, y, i;
	const UBYTE *daten;

	if(ab >= (ULONG)rsc_len || len < 4) return FALSE;

	if(roh[0] != h || (ULONG)roh[1] * 8 != w) return FALSE;

	if(len < 2 + h + (LONG)w * h) return FALSE;

	/* Die Pixel sind die LETZTEN w*h Byte des Satzes; die Fuellung liegt
	 * VORN, zwischen Kopf und Pixeln. Eine fruehere Fassung nahm einen
	 * festen Kopf von h+2 bzw. h+4 an und las damit 1028 der 7211 Saetze um
	 * genau ein Byte verschoben -- sichtbar wurde das nicht, weil in der
	 * Ebenenfolge ein Byte Versatz die Spalten nur umsortiert. */
	daten = roh + (len - (LONG)w * h);
	sp = w / 4;

	for(p = 0; p < 4; p++)
	{
		const UBYTE *q = daten + p * sp * h;

		for(y = 0; y < h; y++)
		{
			UBYTE *z = aus + y * w + p;

			for(i = 0; i < sp; i++)
			{
				*z = tab[*q++];
				z += 4;
			}
		}
	}

	return TRUE;
}


/* ---- Archive schreiben --------------------------------------------- */

/* Ein Eintrag im Archiv, so weit er vorher feststeht. */
struct Aus
{
	const char	*name;
	ULONG		quelle;		/* Bild: Index in zx_bilder,
					 * Klang: Index in verz */
	ULONG		laenge;
	UWORD		w;
	UWORD		h;
	UWORD		reserve;	/* Klaenge: Abtastrate */
	UBYTE		fmt;
	UBYTE		xlat;
};

static BOOL schreibe_alles(BPTR f, const void *p, LONG n)
{
	return Write(f, (APTR)p, n) == n;
}

/* Kopf, Verzeichnis und Namenstabelle schreiben. Die Daten folgen danach im
 * Strom -- alle Groessen stehen vorher fest, es muss also nichts gepuffert
 * werden. */
static BOOL rahmen_schreiben(BPTR f, struct Aus *e, LONG n)
{
	UBYTE kopf[16];
	UBYTE zeile[24];
	ULONG namen_len = 0;
	ULONG namen_ab, daten_ab, lauf;
	LONG i;

	for(i = 0; i < n; i++) namen_len += (ULONG)strlen(e[i].name);

	namen_ab = 16 + 24 * (ULONG)n;
	daten_ab = namen_ab + namen_len;

	memcpy(kopf, "ZPK1", 4);
	be32(kopf + 4, (ULONG)n);
	be32(kopf + 8, namen_ab);
	be32(kopf + 12, namen_len);

	if(!schreibe_alles(f, kopf, 16)) return FALSE;

	lauf = 0;
	namen_len = 0;

	for(i = 0; i < n; i++)
	{
		ULONG nl = (ULONG)strlen(e[i].name);

		be32(zeile + 0, namen_len);
		be16(zeile + 4, nl);
		be16(zeile + 6, e[i].w);
		be16(zeile + 8, e[i].h);
		zeile[10] = e[i].fmt;
		zeile[11] = (e[i].fmt == FMT_PCM8) ? 255 : 0;
		be16(zeile + 12, 0);			/* Palettenfarben */
		be32(zeile + 14, daten_ab + lauf);
		be32(zeile + 18, e[i].laenge);
		be16(zeile + 22, e[i].reserve);

		if(!schreibe_alles(f, zeile, 24)) return FALSE;

		lauf += e[i].laenge;
		namen_len += nl;
	}

	for(i = 0; i < n; i++)
		if(!schreibe_alles(f, e[i].name, (LONG)strlen(e[i].name)))
			return FALSE;

	return TRUE;
}

static LONG archiv_schreiben(const char *ziel, const char *gruppe,
                             struct Aus *e, LONG n)
{
	char pfad[256];
	char datei[64];
	BPTR f;
	UBYTE *puffer = NULL;
	ULONG groesster = 0;
	LONG i;
	LONG gesamt = 0;

	if(n == 0) return 0;

	strcpy(datei, "zod_");
	strncpy(datei + 4, gruppe, sizeof(datei) - 10);
	datei[sizeof(datei) - 10] = 0;
	strcat(datei, ".zpk");

	pfad_bauen(pfad, sizeof(pfad), ziel, datei);

	f = Open(pfad, MODE_NEWFILE);

	if(!f)
	{
		Printf("%s laesst sich nicht anlegen\n", (LONG)pfad);
		fehler++;

		return 0;
	}

	if(!rahmen_schreiben(f, e, n))
	{
		Printf("%s: Schreibfehler im Kopf\n", (LONG)pfad);
		Close(f);
		fehler++;

		return 0;
	}

	for(i = 0; i < n; i++)
		if(e[i].laenge > groesster) groesster = e[i].laenge;

	puffer = AllocVec(groesster ? groesster : 1, MEMF_ANY);

	if(!puffer)
	{
		Printf("%s: Speicher fuer %ld Byte fehlt\n", (LONG)pfad,
		       (LONG)groesster);
		Close(f);
		fehler++;

		return 0;
	}

	for(i = 0; i < n; i++)
	{
		if(e[i].fmt == FMT_SHARED8)
		{
			const struct ZxBild *b = &zx_bilder[e[i].quelle];

			if(!satz_holen((LONG)b->satz, b->w, b->h,
			               zx_xlat[e[i].xlat], puffer))
			{
				Printf("  Satz %ld passt nicht (%s)\n",
				       (LONG)b->satz, (LONG)e[i].name);
				memset(puffer, 0, e[i].laenge);
				fehler++;
			}
		}
		else
		{
			UBYTE *roh = entpacke_eintrag((LONG)e[i].quelle);
			ULONG k;

			if(!roh)
			{
				Printf("  Klang %s laesst sich nicht "
				       "entpacken\n", (LONG)e[i].name);
				memset(puffer, 0, e[i].laenge);
				fehler++;
			}
			else
			{
				/* vorzeichenlos -> vorzeichenbehaftet */
				for(k = 0; k < e[i].laenge; k++)
					puffer[k] = (UBYTE)(roh[k] - 128);

				FreeVec(roh);
			}
		}

		if(!schreibe_alles(f, puffer, (LONG)e[i].laenge))
		{
			Printf("%s: Schreibfehler\n", (LONG)pfad);
			fehler++;

			break;
		}

		gesamt += (LONG)e[i].laenge;
	}

	FreeVec(puffer);
	Close(f);

	Printf("  %s: %ld Eintraege, %ld KB\n", (LONG)datei, (LONG)n,
	       (LONG)((gesamt + 1023) / 1024));

	return gesamt;
}


/* ---- Hauptteil ------------------------------------------------------ */

static LONG bilder_gruppe(const char *ziel, LONG gruppe)
{
	struct Aus *e;
	LONG n = 0;
	LONG i;
	LONG gesamt;

	for(i = 0; i < zx_n_bilder; i++)
		if(zx_bilder[i].gruppe == gruppe) n++;

	if(n == 0) return 0;

	e = AllocVec(sizeof(struct Aus) * n, MEMF_ANY);

	if(!e)
	{
		Printf("Speicher fuer %ld Eintraege fehlt\n", (LONG)n);
		fehler++;

		return 0;
	}

	n = 0;

	for(i = 0; i < zx_n_bilder; i++)
	{
		const struct ZxBild *b = &zx_bilder[i];

		if(b->gruppe != gruppe) continue;

		e[n].name = zx_namen + b->name;
		e[n].quelle = (ULONG)i;
		e[n].laenge = (ULONG)b->w * b->h;
		e[n].w = b->w;
		e[n].h = b->h;
		e[n].reserve = 0;
		e[n].fmt = FMT_SHARED8;
		e[n].xlat = b->xlat;
		n++;
	}

	gesamt = archiv_schreiben(ziel, zx_gruppen[gruppe], e, n);
	FreeVec(e);

	return gesamt;
}

static LONG klaenge_schreiben(const char *ziel)
{
	struct Aus *e;
	LONG n = 0;
	LONG i;
	LONG gesamt;

	e = AllocVec(sizeof(struct Aus) * zx_n_klaenge, MEMF_ANY);

	if(!e)
	{
		Printf("Speicher fuer die Klaenge fehlt\n");
		fehler++;

		return 0;
	}

	for(i = 0; i < zx_n_klaenge; i++)
	{
		LONG k = verz_suche(zx_klaenge[i].dos);

		if(k < 0)
		{
			Printf("  %s nicht in z.pac\n",
			       (LONG)(zx_namen + zx_klaenge[i].name));
			fehler++;

			continue;
		}

		e[n].name = zx_namen + zx_klaenge[i].name;
		e[n].quelle = (ULONG)k;
		e[n].laenge = verz[k].groesse;
		e[n].w = 0;
		e[n].h = 0;
		e[n].reserve = (UWORD)zx_klaenge[i].rate;
		e[n].fmt = FMT_PCM8;
		e[n].xlat = 0;
		n++;
	}

	gesamt = archiv_schreiben(ziel, zx_gruppen[GRUPPE_KLANG], e, n);
	FreeVec(e);

	return gesamt;
}

/* Zwei Dateien kommen NICHT von der CD, sondern mit der Engine:
 *
 *   palette.zpl     die gemeinsame Palette (Grundpalette, Planetenbaenke,
 *                   Team-Umsetztabellen) -- eine reine Engine-Tabelle
 *   zod_engine.zpk  alles, was auf der CD nicht steht: HUD samt Portraits,
 *                   Menues, Startbild, Echtfarb-Zeiger, Fabrik- und
 *                   Produktionsfenster, Forts, Karten-Kleinobjekte
 *
 * Fehlt eines davon, startet das Spiel zwar, zeigt aber kein HUD und keine
 * Menues -- und das sieht nach einem Fehler des Extraktors aus, obwohl er
 * seine Arbeit getan hat. Deshalb wird es hier geprueft und benannt. */
static BOOL mitgeliefertes_da(const char *ziel)
{
	static const char *noetig[] = { "palette.zpl", "zod_engine.zpk", 0 };
	char pfad[256];
	BOOL alles = TRUE;
	LONG i;

	for(i = 0; noetig[i]; i++)
	{
		BPTR f;

		pfad_bauen(pfad, sizeof(pfad), ziel, noetig[i]);
		f = Open(pfad, MODE_OLDFILE);

		if(f)
		{
			Close(f);

			continue;
		}

		if(alles)
			Printf("\nACHTUNG: Es fehlen Dateien, die mit der ENGINE "
			       "ausgeliefert werden --\n"
			       "nicht von der CD. Bitte das Engine-Archiv "
			       "vollstaendig entpacken.\n");

		Printf("  fehlt: %s\n", (LONG)pfad);
		alles = FALSE;
	}

	return alles;
}


/* ---- Musik: die XMI des z.pac nach ZMU1 --------------------------- */
/*
 * Auf der CD liegen 81 XMI: ZEHN Stuecke in ACHT Fassungen (Praefix A bis
 * H) plus TEST. Die acht Fassungen sind die Treiberfassungen fuer die
 * Soundkarten von 1996, nicht acht verschiedene Soundtracks -- gebraucht
 * wird genau eine. Genommen wird die A-Fassung: dieselbe, die Nighsoft
 * seinerzeit als .MID mitgeliefert hat.
 *
 * Was hier NICHT herkommen kann, ist die Instrumentenbank. Auf der CD
 * stehen nur die FM-Parameter (SAMPLE.AD/SAMPLE.OPL, je 3762 Byte) --
 * Einstellungen fuer einen OPL-Chip, keine Klangproben.
 */
static LONG musik_extrahieren(const char *ziel)
{
	LONG i;
	LONG stuecke = 0;
	LONG misslungen = 0;

	for(i = 0; i < n_verz; i++)
	{
		UBYTE *roh;
		char basis[12];
		LONG k, n;
		LONG res;

		if(memcmp(verz[i].name + 8, "XMI", 3) != 0) continue;
		if(verz[i].name[0] != 'A') continue;

		/* 8.3-Namen im z.pac sind mit Leerzeichen aufgefuellt. */
		n = 0;

		for(k = 0; k < 8; k++)
		{
			if(verz[i].name[k] == ' ') break;

			basis[n++] = (char)verz[i].name[k];
		}

		basis[n] = 0;

		roh = entpacke_eintrag(i);

		if(!roh)
		{
			Printf("  %s: nicht zu entpacken\n", (LONG)basis);
			misslungen++;

			continue;
		}

		res = xmi_nach_zmu(roh, verz[i].groesse, ziel, basis);

		FreeVec(roh);

		if(res < 0)
		{
			Printf("  %s: kein gueltiges XMI\n", (LONG)basis);
			misslungen++;

			continue;
		}

		stuecke += res;
	}

	if(misslungen) fehler += misslungen;

	return stuecke;
}

int main(int argc, char **argv)
{
	char pfad[256];
	const char *quelle;
	/* PROGDIR: statt eines festen Volume-Namens.
	 *
	 * Vorher stand hier "ZOD:game/packs" -- das setzte zweierlei voraus, was
	 * nicht gesichert ist: dass das Volume ZOD: heisst und dass das Spiel in
	 * einem Unterverzeichnis game/ liegt. Seit das Paket flach ist, stimmt das
	 * zweite ohnehin nicht mehr. ZExtract liegt IMMER neben dem Spiel, also
	 * ist PROGDIR: genau das richtige Bezugsverzeichnis. */
	const char *ziel = "PROGDIR:packs";
	/* Die Musik gehoert NEBEN das Spiel, nicht in die Archive: die Engine
	 * sucht sie unter music/ im Arbeitsverzeichnis. */
	const char *musikziel = "PROGDIR:music";
	LONG idx;
	LONG gesamt = 0;
	LONG g;

	dbg_boot();
	Printf("ZExtract -- Spieldaten von der Original-CD von Z\n");

	if(argc < 2)
	{
		Printf("\nAufruf: ZExtract <quelle> [ziel] [musikziel]\n"
		       "  <quelle>     CD0: oder ein Verzeichnis mit z.pac\n"
		       "  [ziel]       Vorgabe PROGDIR:packs (neben ZExtract)\n"
		       "  [musikziel]  Vorgabe PROGDIR:music\n");

		return RETURN_WARN;
	}

	quelle = argv[1];

	if(argc > 2) ziel = argv[2];
	if(argc > 3) musikziel = argv[3];

	pfad_bauen(pfad, sizeof(pfad), quelle, "z.pac");
	pac = Open(pfad, MODE_OLDFILE);

	if(!pac)
	{
		Printf("%s nicht gefunden -- CD eingelegt?\n", (LONG)pfad);
		dbg_fail("z.pac nicht gefunden");

		return RETURN_FAIL;
	}

	Printf("Quelle: %s\n", (LONG)pfad);

	if(!verzeichnis_lesen())
	{
		Printf("Verzeichnis von z.pac nicht lesbar\n");
		dbg_fail("Verzeichnis unlesbar");
		Close(pac);

		return RETURN_FAIL;
	}

	Printf("z.pac: %ld Eintraege\n", (LONG)n_verz);

	idx = verz_suche("SPRITES RSC ");

	if(idx < 0)
	{
		Printf("SPRITES.RSC fehlt -- ist das die Z-CD?\n");
		dbg_fail("SPRITES.RSC fehlt");
		Close(pac);

		return RETURN_FAIL;
	}

	Printf("SPRITES.RSC entpacken (%ld KB) ...\n",
	       (LONG)(verz[idx].groesse / 1024));

	rsc = entpacke_eintrag(idx);

	if(!rsc)
	{
		Printf("SPRITES.RSC laesst sich nicht entpacken\n");
		dbg_fail("SPRITES.RSC nicht entpackbar");
		Close(pac);

		return RETURN_FAIL;
	}

	rsc_len = (LONG)verz[idx].groesse;
	rsc_saetze = (LONG)le16(rsc);
	Printf("  %ld Saetze\n", (LONG)rsc_saetze);

	if(!CreateDir(ziel) && IoErr() != ERROR_OBJECT_EXISTS)
	{
		/* Kein Abbruch: Das Verzeichnis kann tiefer liegen und schon
		 * da sein; ein echter Fehler faellt beim Open auf. */
	}

	Printf("Archive schreiben nach %s\n", (LONG)ziel);

	for(g = 0; g < zx_n_gruppen; g++)
	{
		if(g == GRUPPE_KLANG) continue;

		gesamt += bilder_gruppe(ziel, g);
	}

	gesamt += klaenge_schreiben(ziel);

	/* Musik. Sie geht NICHT in die Archive, sondern als einzelne .zmu in
	 * ein eigenes Verzeichnis -- die Engine sucht sie unter music/ neben
	 * dem Spiel, nicht in packs/. */
	if(!CreateDir(musikziel) && IoErr() != ERROR_OBJECT_EXISTS)
	{
		/* siehe oben: ein echter Fehler faellt beim Schreiben auf */
	}

	Printf("Musik umsetzen nach %s ...\n", (LONG)musikziel);

	{
		LONG stuecke = musik_extrahieren(musikziel);

		if(stuecke > 0) Printf("  %ld Stuecke\n", (LONG)stuecke);
		else            Printf("  keine Musik gefunden\n");
	}

	/* Die Vollbilder (Laden, Sieg, Niederlage) kommen aus einem ANDEREN
	 * Container: z/main.pac, Format JMP2. Sie gehen in ein eigenes Archiv,
	 * weil jedes Bild seine EIGENE Palette braucht -- rund 250 Farben je
	 * Stueck, die sich nicht in die gemeinsame Palette bringen lassen.
	 *
	 * Fehlt main.pac, ist das kein Fehler: das Spiel laeuft dann mit dem
	 * bisherigen Ladebild weiter. */
	Printf("Vollbilder aus z/main.pac ...\n");

	{
		ULONG schirm_byte = 0;
		LONG schirme = schirme_extrahieren(quelle, ziel, &schirm_byte);

		if(schirme > 0)
		{
			Printf("  %ld Bilder (Laden, Sieg, Niederlage), %ld KB\n",
			       (LONG)schirme,
			       (LONG)((schirm_byte + 1023) / 1024));

			/* MITZAEHLEN. Sonst meldet die Schlusszeile nur, was
			 * ueber den alten Archivschreiber lief -- und das ist
			 * seit diesen Bildern weniger als die Haelfte. */
			gesamt += (LONG)schirm_byte;
		}
		else if(schirme == 0)
			Printf("  z/main.pac nicht gefunden -- ohne "
			       "Originalbildschirme\n");
		else
		{
			Printf("  z/main.pac liess sich nicht auswerten\n");
			fehler++;
		}
	}

	/* Die Levelbezeichnungen ("Virgin Soldiers", "Death Valley" ...) aus
	 * z/levels.dat. Sie stehen im Original auf dem Ladebildschirm und
	 * haben mit dem Dateinamen der Karte nichts zu tun. */
	{
		LONG namen = levelnamen_extrahieren(quelle, ziel);

		if(namen > 0)
			Printf("Levelnamen: %ld aus z/levels.dat\n", (LONG)namen);
		else if(namen == 0)
			Printf("Levelnamen: z/levels.dat nicht gefunden\n");
		else
		{
			Printf("Levelnamen: z/levels.dat liess sich nicht lesen\n");
			fehler++;
		}
	}

	FreeVec(rsc);
	FreeVec(verz);
	Close(pac);

	Printf("\nFertig: %ld KB.\n", (LONG)((gesamt + 1023) / 1024));

	if(fehler)
	{
		Printf("%ld Fehler -- die Archive sind unvollstaendig.\n",
		       (LONG)fehler);
		dbg_fail("Archive unvollstaendig");

		return RETURN_ERROR;
	}

	if(!mitgeliefertes_da(ziel))
	{
		dbg_fail("mitgelieferte Dateien fehlen");

		return RETURN_WARN;
	}

	Printf("Alles da. Das Spiel kann gestartet werden.\n");
	dbg_ok("extract");

	return RETURN_OK;
}
