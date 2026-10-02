/* Vollbilder aus z/main.pac ins Archiv. Begruendung: schirme.h */

#include "schirme.h"
#include "jmp2.h"
#include "lbm.h"

#include <string.h>

#ifdef __amigaos__
#include <proto/exec.h>
#include <proto/dos.h>
#else
#include <stdio.h>
#endif

#define SCHIRM_W 320
#define SCHIRM_H 200
#define SCHIRM_PIX (SCHIRM_W * SCHIRM_H)
#define SCHIRM_LEN (768 + SCHIRM_PIX)

/* Planeten in der Reihenfolge von planet_type (constants.h der Engine).
 * Der Buchstabe ist der Praefix in main.pac. */
static const struct { char c; const char *name; } planeten[5] =
{
	{ 'D', "desert" },
	{ 'V', "volcanic" },
	{ 'A', "arctic" },
	{ 'J', "jungle" },
	{ 'C', "city" }
};

/* Teamfarben. Rot ist im Original die Grundfassung ohne Farbkuerzel --
 * DLOAD1 statt DMRLOAD --, die drei anderen tragen eines. Genau diese
 * Unregelmaessigkeit ist der Grund fuer die Tabelle: sie laesst sich
 * nicht rechnen. */
static const struct
{
	const char *name;
	const char *laden;	/* %c = Planetenbuchstabe */
	const char *sieg;
	const char *verlust;
}
farben[4] =
{
	{ "red",    "%cLOAD1.LBM",  "%cMRSTAT1.LBM", "%cMLOSE1.LBM" },
	{ "blue",   "%cMBLOAD.LBM", "%cMBSTAT1.LBM", "%cMBLOSE.LBM" },
	{ "green",  "%cMGLOAD.LBM", "%cMGSTAT1.LBM", "%cMGLOSE.LBM" },
	{ "yellow", "%cMYLOAD.LBM", "%cMYSTAT1.LBM", "%cMYLOSE.LBM" }
};

static const char *anlaesse[3] = { "load", "win", "lose" };

/* 5 Planeten x 4 Farben x 3 Anlaesse = 60, dazu 5 x 4 teamneutrale
 * Niederlagenbilder und der Originalzeichensatz. */
#define SCHIRM_MAX 81

/* Ein Archiveintrag. `quelle` ist der Name in main.pac; `w`/`h`/`len`
 * stehen getrennt, weil NICHT alle Eintraege Vollbilder sind: der
 * Originalzeichensatz CHARS.BIN kommt als 1024x8-Bild mit, damit sich
 * Text in der Palette des jeweiligen Bildes zeichnen laesst. Die
 * Engine-Schriften sind in die GEMEINSAME Palette indiziert und kaemen
 * auf einem Vollbild falschfarbig heraus. */
struct Schirm
{
	char	name[48];
	char	quelle[20];
	UWORD	w;
	UWORD	h;
	ULONG	len;		/* 768 + w*h */
	UBYTE	schrift;	/* 1 = CHARS.BIN statt LBM */
};

static void be16w(UBYTE *p, UWORD v)
{
	p[0] = (UBYTE)(v >> 8); p[1] = (UBYTE)v;
}

static void be32w(UBYTE *p, ULONG v)
{
	p[0] = (UBYTE)(v >> 24); p[1] = (UBYTE)(v >> 16);
	p[2] = (UBYTE)(v >> 8);  p[3] = (UBYTE)v;
}

/* Namen mit EINEM %c fuellen. sprintf waere hier gefaehrlich: die
 * Amiga-Laufzeit liest %d als 16 Bit, und wer sich einmal
 * daran gewoehnt, benutzt es auch dort, wo es beisst. Von Hand ist es
 * drei Zeilen. */
static void namen_bauen(char *aus, LONG platz, const char *muster, char c)
{
	LONG i = 0;

	while(*muster && i < platz - 1)
	{
		if(muster[0] == '%' && muster[1] == 'c')
		{
			aus[i++] = c;
			muster += 2;
		}
		else aus[i++] = *muster++;
	}

	aus[i] = 0;
}

static void ziel_bauen(char *aus, LONG platz, const char *anlass,
                       const char *planet, const char *farbe)
{
	LONG i = 0;
	const char *teile[6];
	LONG n = 0, k;

	teile[n++] = "assets/screens/";
	teile[n++] = anlass;
	teile[n++] = "_";
	teile[n++] = planet;
	teile[n++] = "_";
	teile[n++] = farbe;

	for(k = 0; k < n; k++)
	{
		const char *s = teile[k];

		while(*s && i < platz - 1) aus[i++] = *s++;
	}

	aus[i] = 0;
}

static UBYTE *datei_lesen(const char *pfad, ULONG *len)
{
#ifdef __amigaos__
	BPTR f = Open((CONST_STRPTR)pfad, MODE_OLDFILE);
	UBYTE *p;
	LONG n;

	if(!f) return NULL;

	Seek(f, 0, OFFSET_END);
	n = Seek(f, 0, OFFSET_BEGINNING);

	/* Seek liefert die VORIGE Position -- nach dem Sprung ans Ende ist
	 * das die Dateilaenge. */
	if(n <= 0) { Close(f); return NULL; }

	p = AllocVec((ULONG)n, MEMF_ANY);

	if(!p) { Close(f); return NULL; }

	if(Read(f, p, n) != n) { FreeVec(p); Close(f); return NULL; }

	Close(f);
	*len = (ULONG)n;

	return p;
#else
	FILE *f = fopen(pfad, "rb");
	UBYTE *p;
	long n;

	if(!f) return NULL;

	fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);

	if(n <= 0) { fclose(f); return NULL; }

	p = (UBYTE *)AllocVec((ULONG)n, MEMF_ANY);

	if(!p) { fclose(f); return NULL; }

	if(fread(p, 1, (size_t)n, f) != (size_t)n)
	{ FreeVec(p); fclose(f); return NULL; }

	fclose(f);
	*len = (ULONG)n;

	return p;
#endif
}

LONG schirme_extrahieren(const char *cd, const char *ziel, ULONG *bytes)
{
	ULONG geschriebene_byte = 0;
	struct Schirm *liste;
	struct Jmp2Eintrag *verz;
	UBYTE *pac = NULL;
	UBYTE *pixel = NULL;
	UBYTE *roh = NULL;
	UBYTE *kopf = NULL;
	/* struct LbmBild traegt die 768-Byte-Palette. Als lokale Variable in
	 * der Schleife laege sie auf dem Stapel -- und ZExtract laeuft mit
	 * dem VORGABESTAPEL der Shell (traditionell 4096 Byte), weil der
	 * Installer keinen eigenen setzt. Auf den Haufen damit, aus demselben
	 * Grund wie der Ring in jmp2.c. */
	struct LbmBild *b = NULL;
	char pfad[256];
	ULONG pac_len = 0;
	LONG n_verz, n = 0, i, geschrieben = 0;
	ULONG groesster = 0;
#ifdef __amigaos__
	BPTR f = 0;
#else
	FILE *f = NULL;
#endif

	/* main.pac liegt im Unterverzeichnis z/ der CD. */
	{
		LONG k = 0;

		while(cd[k] && k < (LONG)sizeof(pfad) - 16) { pfad[k] = cd[k]; k++; }

		if(k > 0 && pfad[k-1] != ':' && pfad[k-1] != '/') pfad[k++] = '/';

		strcpy(pfad + k, "z/main.pac");
	}

	pac = datei_lesen(pfad, &pac_len);

	if(!pac)
	{
		/* KEIN Fehler. Wer eine aeltere CD oder eine unvollstaendige
		 * Kopie hat, soll das Spiel trotzdem bekommen -- dann eben
		 * mit dem bisherigen Ladebild. */
		return 0;
	}

	liste = (struct Schirm *)AllocVec(sizeof(struct Schirm) * SCHIRM_MAX,
	                                  MEMF_ANY);
	verz  = (struct Jmp2Eintrag *)AllocVec(sizeof(struct Jmp2Eintrag) * 256,
	                                       MEMF_ANY);
	/* Gross genug fuer das groesste Bild UND fuer den Zeichensatz
	 * (1024x8). Ein Puffer fuer alles. */
	pixel = (UBYTE *)AllocVec(SCHIRM_PIX > 128 * 8 * 8
	                          ? SCHIRM_PIX : 128 * 8 * 8, MEMF_ANY);
	b = (struct LbmBild *)AllocVec(sizeof(struct LbmBild), MEMF_ANY);

	if(!liste || !verz || !pixel || !b) goto schief;

	n_verz = jmp2_verzeichnis(pac, pac_len, verz, 256);

	if(n_verz < 0) goto schief;

	/* Die Wunschliste aufstellen. */
	for(i = 0; i < 5; i++)
	{
		LONG fa;

		for(fa = 0; fa < 4; fa++)
		{
			LONG an;
			const char *muster[3];

			muster[0] = farben[fa].laden;
			muster[1] = farben[fa].sieg;
			muster[2] = farben[fa].verlust;

			for(an = 0; an < 3; an++)
			{
				if(n >= SCHIRM_MAX) break;

				namen_bauen(liste[n].quelle,
				            (LONG)sizeof(liste[n].quelle),
				            muster[an], planeten[i].c);

				ziel_bauen(liste[n].name,
				           (LONG)sizeof(liste[n].name),
				           anlaesse[an], planeten[i].name,
				           farben[fa].name);
				liste[n].w = SCHIRM_W;
				liste[n].h = SCHIRM_H;
				liste[n].len = SCHIRM_LEN;
				liste[n].schrift = 0;
				n++;
			}
		}
	}

	/* ZUSAETZLICHE NIEDERLAGENBILDER, teamneutral.
	 *
	 * Im Original ist das Bild beim Verlieren ZUFAELLIG -- der Nutzer hat
	 * auf derselben Karte zweimal verloren und zwei verschiedene bekommen.
	 * Der Satz ?LOSE1..4 zeigt keine Teamfarbe (General Zod, ein Wrack mit
	 * Geiern, das brennende Fort) und passt deshalb zu jeder Partei; die
	 * teamfarbigen aus der Liste oben kommen als fuenfte Moeglichkeit
	 * dazu.
	 *
	 * Vier je Planet, also 20 Bilder und 1,28 MB mehr im Archiv. */
	for(i = 0; i < 5 && n + 4 <= SCHIRM_MAX; i++)
	{
		LONG v;

		for(v = 1; v <= 4; v++)
		{
			char muster[20];
			char anlass[12];

			muster[0] = '%'; muster[1] = 'c';
			muster[2] = 'L'; muster[3] = 'O'; muster[4] = 'S';
			muster[5] = 'E'; muster[6] = (char)('0' + v);
			strcpy(muster + 7, ".LBM");

			namen_bauen(liste[n].quelle, (LONG)sizeof(liste[n].quelle),
			            muster, planeten[i].c);

			/* "lose" + Ziffer, damit die Engine sie der Reihe nach
			 * durchprobieren kann: lose1_desert, lose2_desert ... */
			anlass[0] = 'l'; anlass[1] = 'o'; anlass[2] = 's';
			anlass[3] = 'e'; anlass[4] = (char)('0' + v); anlass[5] = 0;

			{
				LONG k = 0;
				const char *t[4];
				LONG q, w = 0;

				t[w++] = "assets/screens/";
				t[w++] = anlass;
				t[w++] = "_";
				t[w++] = planeten[i].name;

				for(q = 0; q < w; q++)
				{
					const char *z = t[q];

					while(*z && k < (LONG)sizeof(liste[n].name) - 1)
						liste[n].name[k++] = *z++;
				}

				liste[n].name[k] = 0;
			}

			liste[n].w = SCHIRM_W;
			liste[n].h = SCHIRM_H;
			liste[n].len = SCHIRM_LEN;
			liste[n].schrift = 0;
			n++;
		}
	}

	/* Der Originalzeichensatz: 128 Zeichen zu 8x8, ein Bit je Punkt.
	 * Er kommt als gewoehnliches Bild ins Archiv (1024x8, ein Byte je
	 * Punkt), damit der Lader unveraendert bleibt -- 8 KB statt 1 KB,
	 * dafuer keine Sonderbehandlung. */
	if(n < SCHIRM_MAX)
	{
		strcpy(liste[n].quelle, "CHARS.BIN");
		strcpy(liste[n].name, "assets/screens/font");
		liste[n].w = 128 * 8;
		liste[n].h = 8;
		liste[n].len = 768 + 128 * 8 * 8;
		liste[n].schrift = 1;
		n++;
	}

	/* Kopf, Verzeichnis und Namen. Jeder Eintrag kennt seine Laenge, die
	 * Versaetze stehen also vorher fest -- es muss nichts gepuffert
	 * werden. */
	{
		ULONG namen_len = 0, namen_ab, daten_ab;
		LONG k;

		for(i = 0; i < n; i++) namen_len += (ULONG)strlen(liste[i].name);

		namen_ab = 16 + 24 * (ULONG)n;
		daten_ab = namen_ab + namen_len;

		kopf = (UBYTE *)AllocVec(16 + 24 * (ULONG)n + namen_len, MEMF_ANY);

		if(!kopf) goto schief;

		memcpy(kopf, "ZPK1", 4);
		be32w(kopf + 4, (ULONG)n);
		be32w(kopf + 8, namen_ab);
		be32w(kopf + 12, namen_len);

		namen_len = 0;

		{
			ULONG lauf = 0;

			for(i = 0; i < n; i++)
			{
				UBYTE *z = kopf + 16 + 24 * i;
				ULONG nl = (ULONG)strlen(liste[i].name);

				be32w(z + 0, namen_len);
				be16w(z + 4, (UWORD)nl);
				be16w(z + 6, liste[i].w);
				be16w(z + 8, liste[i].h);
				z[10] = 1;		/* Format 1: eigene Palette */
				z[11] = 255;		/* kein Farbschluessel */
				be16w(z + 12, 256);	/* Palettenfarben */
				be32w(z + 14, daten_ab + lauf);
				be32w(z + 18, liste[i].len);
				be16w(z + 22, 0);

				lauf += liste[i].len;
				namen_len += nl;
			}
		}

		k = 16 + 24 * n;

		for(i = 0; i < n; i++)
		{
			ULONG nl = (ULONG)strlen(liste[i].name);

			memcpy(kopf + k, liste[i].name, nl);
			k += (LONG)nl;
		}

		{
			LONG p = 0;

			while(ziel[p] && p < (LONG)sizeof(pfad) - 20) { pfad[p] = ziel[p]; p++; }

			if(p > 0 && pfad[p-1] != ':' && pfad[p-1] != '/') pfad[p++] = '/';

			strcpy(pfad + p, "zod_screens.zpk");
		}

#ifdef __amigaos__
		f = Open((CONST_STRPTR)pfad, MODE_NEWFILE);
		if(!f) goto schief;
		if(Write(f, kopf, k) != k) goto schief;
#else
		f = fopen(pfad, "wb");
		if(!f) goto schief;
		if((LONG)fwrite(kopf, 1, (size_t)k, f) != k) goto schief;
#endif
	}

	/* Groessten gepackten Eintrag ermitteln -- EIN Puffer fuer alle. */
	for(i = 0; i < n_verz; i++)
		if(verz[i].soll > groesster) groesster = verz[i].soll;

	roh = (UBYTE *)AllocVec(groesster ? groesster : 1, MEMF_ANY);

	if(!roh) goto schief;

	for(i = 0; i < n; i++)
	{
		ULONG pix_len = liste[i].len - 768;
		LONG q;
		int gut = 0;

		memset(b, 0, sizeof(*b));

		for(q = 0; q < n_verz; q++)
			if(strcmp(verz[q].name, liste[i].quelle) == 0) break;

		if(q < n_verz
		&& jmp2_entpacken(pac + verz[q].roh_ab, verz[q].roh_len,
		                  roh, verz[q].soll))
		{
			if(liste[i].schrift)
			{
				/* CHARS.BIN: 128 Zeichen zu 8x8, ein BIT je
				 * Punkt, acht Byte je Zeichen. Hier wird es auf
				 * ein Byte je Punkt aufgezogen und nebeneinander
				 * gelegt -- Zeichen c beginnt bei x = c * 8.
				 *
				 * Die Palette hat genau zwei benutzte Plaetze:
				 * 0 = nichts, 1 = Strich. Welche Farbe daraus
				 * wird, entscheidet die Engine je Bild, nicht
				 * diese Datei. */
				ULONG c, y, x;

				if(verz[q].soll >= 128 * 8)
				{
					memset(pixel, 0, pix_len);

					for(c = 0; c < 128; c++)
						for(y = 0; y < 8; y++)
						{
							UBYTE m = roh[c * 8 + y];

							for(x = 0; x < 8; x++)
								if(m & (0x80u >> x))
									pixel[y * (128 * 8) + c * 8 + x] = 1;
						}

					b->palette[3] = 255;
					b->palette[4] = 255;
					b->palette[5] = 255;
					gut = 1;
				}
			}
			else if(lbm_lesen(roh, verz[q].soll, b, pixel, pix_len)
			     && b->w == SCHIRM_W && b->h == SCHIRM_H)
				gut = 1;
		}

		if(!gut)
		{
			/* Fehlt eines, wird der Platz mit Schwarz gefuellt --
			 * die Versaetze im Verzeichnis stehen schon fest. Die
			 * Engine sieht ein schwarzes Bild statt eines
			 * verschobenen Archivs. */
			memset(b, 0, sizeof(*b));
			memset(pixel, 0, pix_len);
		}
		else geschrieben++;

#ifdef __amigaos__
		if(Write(f, b->palette, 768) != 768) goto schief;
		if(Write(f, pixel, (LONG)pix_len) != (LONG)pix_len) goto schief;
#else
		if(fwrite(b->palette, 1, 768, f) != 768) goto schief;
		if(fwrite(pixel, 1, pix_len, f) != pix_len) goto schief;
#endif
		geschriebene_byte += liste[i].len;
	}

#ifdef __amigaos__
	Close(f);
#else
	fclose(f);
#endif

	FreeVec(b); FreeVec(roh); FreeVec(kopf); FreeVec(pixel);
	FreeVec(verz); FreeVec(liste); FreeVec(pac);

	if(bytes) *bytes = geschriebene_byte;

	return geschrieben;

schief:
#ifdef __amigaos__
	if(f) Close(f);
#else
	if(f) fclose(f);
#endif
	if(b)     FreeVec(b);
	if(roh)   FreeVec(roh);
	if(kopf)  FreeVec(kopf);
	if(pixel) FreeVec(pixel);
	if(verz)  FreeVec(verz);
	if(liste) FreeVec(liste);
	if(pac)   FreeVec(pac);

	return -1;
}


/* ---- Levelbezeichnungen ------------------------------------------- */

#define LN_LEVEL   40
#define LN_BREITE  20

LONG levelnamen_extrahieren(const char *cd, const char *ziel)
{
	UBYTE *dat;
	UBYTE *aus;
	char pfad[256];
	ULONG len = 0;
	LONG k, mit_namen = 0;
#ifdef __amigaos__
	BPTR f;
#else
	FILE *f;
#endif

	{
		LONG i = 0;

		while(cd[i] && i < (LONG)sizeof(pfad) - 20) { pfad[i] = cd[i]; i++; }

		if(i > 0 && pfad[i-1] != ':' && pfad[i-1] != '/') pfad[i++] = '/';

		strcpy(pfad + i, "z/levels.dat");
	}

	dat = datei_lesen(pfad, &len);

	if(!dat) return 0;		/* kein Fehler -- dann eben ohne Namen */

	aus = (UBYTE *)AllocVec(LN_LEVEL * LN_BREITE, MEMF_ANY);

	if(!aus) { FreeVec(dat); return -1; }

	memset(aus, 0, LN_LEVEL * LN_BREITE);

	for(k = 1; k <= LN_LEVEL; k++)
	{
		ULONG ab = (ULONG)k * 240;
		LONG i;

		if(ab + LN_BREITE > len) break;

		for(i = 0; i < LN_BREITE; i++)
		{
			UBYTE c = dat[ab + i];

			if(c == 0) break;

			/* Nur Druckbares uebernehmen. Ein Eintrag ohne Namen
			 * (Level 36 auf dieser CD) bleibt leer, und die Engine
			 * zeigt dann nur die Nummer. */
			aus[(k - 1) * LN_BREITE + i] = (c >= 32 && c < 127) ? c : (UBYTE)' ';
		}

		if(aus[(k - 1) * LN_BREITE]) mit_namen++;
	}

	{
		LONG i = 0;

		while(ziel[i] && i < (LONG)sizeof(pfad) - 20) { pfad[i] = ziel[i]; i++; }

		if(i > 0 && pfad[i-1] != ':' && pfad[i-1] != '/') pfad[i++] = '/';

		strcpy(pfad + i, "levelnames.dat");
	}

#ifdef __amigaos__
	f = Open((CONST_STRPTR)pfad, MODE_NEWFILE);

	if(!f) { FreeVec(aus); FreeVec(dat); return -1; }

	if(Write(f, aus, LN_LEVEL * LN_BREITE) != LN_LEVEL * LN_BREITE)
	{ Close(f); FreeVec(aus); FreeVec(dat); return -1; }

	Close(f);
#else
	f = fopen(pfad, "wb");

	if(!f) { FreeVec(aus); FreeVec(dat); return -1; }

	if(fwrite(aus, 1, LN_LEVEL * LN_BREITE, f) != LN_LEVEL * LN_BREITE)
	{ fclose(f); FreeVec(aus); FreeVec(dat); return -1; }

	fclose(f);
#endif

	FreeVec(aus);
	FreeVec(dat);

	return mit_namen;
}
