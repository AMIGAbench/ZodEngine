/*
 * mischer_test -- den Amiga-Musikmischer auf dem Host pruefen.
 *
 * Host-first, wie alles in diesem Projekt: Der Mischer ist gewoehnliche
 * Ganzzahlarithmetik und liest sein Eingabeformat ausdruecklich
 * big-endian. Er laeuft deshalb unveraendert auf x86 -- und dort kann man
 * sich das Ergebnis ANHOEREN, statt es auf dem Amiga zu erraten.
 *
 *   mischer_test <bank> <stueck.zmu> <ziel.wav> [sekunden] [rate] [stimmen]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "../../port/amiga/musik_mischer.h"

static unsigned char *datei(const char *name, unsigned long *len)
{
	FILE *f = fopen(name, "rb");
	if(!f) return NULL;
	fseek(f, 0, SEEK_END);
	long n = ftell(f);
	fseek(f, 0, SEEK_SET);
	unsigned char *p = malloc(n);
	if(!p || fread(p, 1, n, f) != (size_t)n) { free(p); fclose(f); return NULL; }
	fclose(f);
	*len = (unsigned long)n;
	return p;
}

static void le32(unsigned char *p, unsigned long v)
{ p[0]=v; p[1]=v>>8; p[2]=v>>16; p[3]=v>>24; }

/* ======================================================================
 * BALANCE MUSIK GEGEN EFFEKTE -- die Messung, die die Zahlen entscheidet
 *
 * Vom Nutzer gemeldet (02.10.): "SFX ist im Vergleich zur Musik sehr leise.
 * SFX muessten etwas lauter sein als die Musik."
 *
 * Das ist eine Aussage ueber PEGEL, und sie laesst sich messen. Gemessen
 * wird dreimal ueber dieselbe Stelle des Stueckes: Musik allein, Effekte
 * allein, beides. Je Durchgang Spitzenwert, RMS und die Zahl der Werte am
 * Anschlag.
 *
 * DER RMS IST DAS MASS, nicht die Spitze. Lautheit haengt an der Energie
 * ueber die Zeit; ein einzelner Spitzenwert sagt darueber nichts. Dieselbe
 * Lehre wie beim Skalierer ("Mittelwert statt Spitze"), nur umgekehrt
 * angewandt.
 *
 * STILLGELEGT WIRD UEBER DIE BALANCE, nicht durch Weglassen: `Effekte
 * allein` heisst `mischer_musik_balance(0)`. Damit laeuft genau der Weg,
 * um den es geht -- ein zweiter Codepfad "ohne Musik" wuerde etwas anderes
 * messen als das Spiel tut.
 *
 * DIE REGLER STEHEN AUF DEN WERTEN DER ENGINE: Musik 80, Effekte 128
 * (zplayer.cpp, InitSDL). Mit 128/128 gemessen waeren die Zahlen zwar
 * schoener, uebertragen sich aber nicht auf das Spiel.
 * ====================================================================== */

/* Eine Probe, wie die Engine sie spielt: kurzer Knall mit Abfall. Sie wird
 * SYNTHETISIERT, damit der Test keine Klangdatei braucht -- sonst haengt er
 * an data/game und ist genau dann nicht da, wenn man ihn braucht. */
#define PROBE_LEN 1800
static BYTE effekt_probe[PROBE_LEN];

static void probe_bauen(void)
{
	unsigned long z = 12345;
	int i;

	for(i = 0; i < PROBE_LEN; i++)
	{
		z = z * 1103515245UL + 12345UL;

		long rausch = (long)((z >> 16) & 0xFF) - 128;
		long huelle = (long)(PROBE_LEN - i) * 127 / PROBE_LEN;

		effekt_probe[i] = (BYTE)((rausch * huelle) / 127);
	}
}

typedef struct { long spitze; double rms; unsigned long anschlag; } Pegel;

/* Ein Durchgang. `bal_m`/`bal_e` sind die Balancewerte fuer DIESEN
 * Durchgang; 0 legt den jeweiligen Weg still. */
static void durchgang(const unsigned char *bank, unsigned long bl,
                      const unsigned char *zmu, unsigned long zl,
                      unsigned long rate, int stimmen, unsigned long sek,
                      int bal_m, int bal_e, int je_sek, Pegel *aus)
{
	const unsigned long block = 512;
	const unsigned long gesamt = rate * sek;
	const unsigned long takt = (je_sek > 0) ? (rate / (unsigned)je_sek) : rate;

	WORD *puf = malloc(block * 2 * sizeof(WORD));
	unsigned long getan = 0, naechster = 0;
	unsigned long z = 999;
	double summe = 0.0;

	aus->spitze = 0; aus->rms = 0.0; aus->anschlag = 0;

	if(!puf) return;

	/* VOLLSTAENDIG ZURUECKSETZEN, nicht nur das Stueck: `mischer_stueck`
	 * leert ausdruecklich nur die MUSIKstimmen, damit ein Kartenwechsel
	 * laufende Klaenge nicht abschneidet. Ohne mischer_init truege jeder
	 * Durchgang die Effekte des vorigen mit -- und die Zahlen haengen
	 * dann an der Reihenfolge der Messungen. */
	mischer_init(bank, bl, rate, stimmen);

	/* OHNE DAS SPIELT KEIN EINZIGER EFFEKT: mischer_effekt steigt bei
	 * effekt_stimmen < 1 sofort aus, und `effekt.rms` waere 0 -- was die
	 * Forderung "Effekte lauter als Musik" scheitern liesse, aber aus dem
	 * falschen Grund. sdl_addons.cpp setzt denselben Wert. */
	mischer_effektstimmen(8);

	mischer_musik_balance(bal_m);
	mischer_effekt_balance(bal_e);

	mischer_stueck(zmu, zl);

	while(getan < gesamt)
	{
		unsigned long r = gesamt - getan;
		unsigned long i;

		if(r > block) r = block;

		while(bal_e && naechster <= getan)
		{
			/* base_volume 40 + rand%20 -- genau wie ZSound::PlaySound
			 * (qzod_soundengine_old.cpp:89). */
			z = z * 1103515245UL + 12345UL;

			mischer_effekt(effekt_probe, PROBE_LEN, 11025,
			               40 + (int)((z >> 16) % 20), 64, 0);
			naechster += takt;
		}

		mischer_fuellen(puf, r);

		for(i = 0; i < r * 2; i++)
		{
			long v = puf[i];

			summe += (double)v * (double)v;

			if(v < 0) v = -v;
			if(v > aus->spitze) aus->spitze = v;
			if(puf[i] == 32767 || puf[i] == -32768) aus->anschlag++;
		}

		getan += r;
	}

	aus->rms = (gesamt ? sqrt(summe / (double)(gesamt * 2)) : 0.0);

	free(puf);
}

static int balance_messen(const unsigned char *bank, unsigned long bl,
                          const unsigned char *zmu, unsigned long zl,
                          unsigned long rate, int stimmen, int gefecht)
{
	const unsigned long sek = 20;     /* lang genug, dass der RMS traegt */
	int bm, be, em, ee, fehler = 0;
	Pegel musik, effekt, beides, schwall, gegen;

	probe_bauen();

	/* Die Regler der Engine nachstellen -- und zwar auf dem HOECHSTEN
	 * Stand, den der Nutzer waehlen kann: `ZPlayer::InitSDL` und der
	 * Menueeintrag SOUND_100 setzen beide 128 (zplayer.cpp:812, :6056).
	 *
	 * Mit dem kleineren Startwert zu messen waere der bequeme Fall und
	 * nicht der entscheidende: wer im Menue 100 % waehlt, bekommt diesen
	 * hier, und genau dort muss die Begrenzung noch halten. */
	mischer_musik_vol(128);
	mischer_effekt_vol_alle(128);

	mischer_lautstaerken(&bm, &be, &em, &ee);

	printf("\n--- Balance (je %lu s, Regler wie die Engine: Musik %d,"
	       " Effekte %d) ---\n", sek, em, ee);
	printf("    Vorgabe der Balance: Musik %d, Effekte %d\n", bm, be);

	durchgang(bank, bl, zmu, zl, rate, stimmen, sek, bm, 0,  4, &musik);
	durchgang(bank, bl, zmu, zl, rate, stimmen, sek, 0,  be, 4, &effekt);
	durchgang(bank, bl, zmu, zl, rate, stimmen, sek, bm, be, 4, &beides);

	printf("    Musik allein  : Spitze %6ld  RMS %8.1f  am Anschlag %lu\n",
	       musik.spitze, musik.rms, musik.anschlag);
	printf("    Effekte allein: Spitze %6ld  RMS %8.1f  am Anschlag %lu\n",
	       effekt.spitze, effekt.rms, effekt.anschlag);
	printf("    beides        : Spitze %6ld  RMS %8.1f  am Anschlag %lu\n",
	       beides.spitze, beides.rms, beides.anschlag);

	/* DIE FORDERUNG, als Zahl: Effekte lauter als Musik.
	 *
	 * Das Verhaeltnis wird IMMER gemeldet, geurteilt wird nur bei
	 * Gefechtsmusik. Grund: `AWIN`/`ALOSE` sind die Fanfaren am
	 * Rundenende -- dort faellt kein Schuss, es gibt also gar keine
	 * Effekte, gegen die sie zu vergleichen waeren. Gemessen liegt AWIN
	 * bei 0,89 des Effektpegels; den Musikhub deswegen zu senken wuerde
	 * die vier Spielstuecke leiser machen, damit eine Fanfare eine
	 * Prüfung besteht, die auf sie nicht zutrifft.
	 *
	 * Ausgeklammert, aber nicht verschwiegen: die Zeile steht da, und sie
	 * nennt den Grund. Eine stillschweigend uebersprungene Pruefung ist
	 * von einer bestandenen nicht zu unterscheiden. */
	printf("    Verhaeltnis Effekte/Musik: %.2f  (%s)\n",
	       musik.rms > 0.0 ? effekt.rms / musik.rms : 0.0,
	       gefecht ? "Gefechtsmusik -- wird gewertet"
	               : "Fanfare am Rundenende -- kein Urteil, es faellt"
	                 " kein Schuss dazu");

	if(gefecht && effekt.rms <= musik.rms)
	{
		printf("FEHLER: Effekte (RMS %.1f) sind NICHT lauter als die Musik"
		       " (RMS %.1f)\n", effekt.rms, musik.rms);
		fehler++;
	}

	/* DER ERNSTFALL, und er entscheidet den Gesamtpegel.
	 *
	 * Vier Effekte je Sekunde sind ein Gefecht. Ein sterbendes Fort wirft
	 * Dutzende Truemmer und Feuerbaelle in EIN Bild -- dann laufen alle
	 * acht Effektstimmen gleichzeitig. Ein Mittelwert ueber ruhige Musik
	 * sagt darueber nichts; dieselbe Lehre wie beim Skalierer
	 * ("Mittelwert statt Spitze") und bei der zaehen Folge.
	 *
	 * 30 Effekte je Sekunde saettigen die acht Plaetze sicher: eine Probe
	 * laeuft 1800/11025 = 163 ms, es ueberlappen also rund fuenf. */
	durchgang(bank, bl, zmu, zl, rate, stimmen, sek, bm, be, 30, &schwall);

	printf("    Effektschwall : Spitze %6ld  RMS %8.1f  am Anschlag %lu\n",
	       schwall.spitze, schwall.rms, schwall.anschlag);

	/* UEBERSTEUERN. Begrenzt wird HART, und das knackt.
	 *
	 * Gewertet wird der SCHWALL, nicht der ruhige Fall.
	 *
	 * UND GEWERTET WIRD DIE SPITZE, NICHT DIE STUECKZAHL. Das ist der
	 * zweite Versuch, und der erste war unbrauchbar: eine Schranke auf die
	 * Zahl geklemmter Werte (ich hatte 64 gesetzt) faengt nichts, weil
	 * Klemmen nur an der allerhoechsten Spitze auftritt. Die Gegenprobe hat
	 * es gezeigt -- bei 175 % Gesamtpegel reisst die Spitze die Klemme, und
	 * es sind trotzdem nur 6 Werte. Der Test waere gruen geblieben.
	 *
	 * Richtig ist: die harte Klemme hat KEINEN weichen Knick. Erreicht die
	 * Spitze die Grenze, wird abgeschnitten, und das knackt -- die
	 * Stueckzahl sagt nur, wie oft. Gefordert sind deshalb zwei Dinge, und
	 * beide nur bei Gefechtsmusik:
	 *
	 *   kein einziger geklemmter Wert, und
	 *   die Spitze bleibt unter 96 % der Vollaussteuerung.
	 *
	 * Die 4 % Abstand sind der Preis dafuer, dass das Schwallmodell eine
	 * Naeherung ist. Gemessener Stand: 88..92 %, also 4..8 % Luft.
	 *
	 * (Bei AWIN/ALOSE wird nicht geurteilt -- dort faellt kein Schuss, der
	 * Schwall kann gar nicht auftreten. Gemeldet wird er trotzdem.) */
	{
		const long grenze = (long)(32767.0 * 0.96);
		double anteil = (double)schwall.anschlag
		              / (double)(rate * sek * 2) * 100.0;

		printf("    Schwall am Anschlag: %lu Werte (%.4f %%),"
		       " %.0f %% Vollaussteuerung\n",
		       schwall.anschlag, anteil,
		       (double)schwall.spitze * 100.0 / 32767.0);

		if(gefecht && schwall.anschlag)
		{
			printf("FEHLER: im Schwall wird geklemmt (%lu Werte) -- die harte"
			       " Klemme knackt\n", schwall.anschlag);
			fehler++;
		}

		if(gefecht && schwall.spitze > grenze)
		{
			printf("FEHLER: Schwallspitze %ld ueber der Schranke %ld"
			       " (96 %% von 32767) -- zu wenig Abstand zur Klemme\n",
			       schwall.spitze, grenze);
			fehler++;
		}
	}

	/* GEGENPROBE. Ohne sie saehe ein Test, der nichts prueft, genauso aus
	 * wie ein bestandener: mit geviertelter Effektbalance MUSS die
	 * Forderung oben durchfallen.
	 *
	 * EFFEKTE ALLEIN, nicht gemischt. Mein erster Entwurf liess hier die
	 * Musik mitlaufen und verglich das Ergebnis mit "Musik allein" -- ein
	 * Mischlauf ist aber nie leiser als eine seiner Haelften, die
	 * Gegenprobe haette also NIE gegriffen. Genau die Sorte Pruefung, die
	 * bestanden aussieht und nichts prueft. */
	durchgang(bank, bl, zmu, zl, rate, stimmen, sek, 0, be / 4, 4, &gegen);

	printf("    Gegenprobe (Effektbalance %d): RMS %8.1f -- %s\n",
	       be / 4, gegen.rms,
	       gegen.rms < musik.rms ? "faellt durch, richtig"
	                             : "FAELLT NICHT DURCH");

	if(gegen.rms >= musik.rms)
	{
		printf("FEHLER: die Gegenprobe greift nicht -- der Test prueft"
		       " moeglicherweise nichts\n");
		fehler++;
	}

	/* Zustand wiederherstellen, damit ein spaeterer Aufruf nicht auf
	 * geviertelten Effekten laeuft. */
	mischer_musik_balance(bm);
	mischer_effekt_balance(be);

	return fehler;
}

int main(int argc, char **argv)
{
	if(argc < 4)
	{
		fprintf(stderr, "Aufruf: %s <bank> <stueck.zmu> <ziel.wav>"
		                " [sekunden] [rate] [stimmen] [ab] [gefecht]\n",
		                argv[0]);
		return 2;
	}

	unsigned long sek     = (argc > 4) ? strtoul(argv[4], NULL, 10) : 40;
	unsigned long rate    = (argc > 5) ? strtoul(argv[5], NULL, 10) : 22050;
	int           stimmen = (argc > 6) ? atoi(argv[6]) : 24;
	/* argv[8]: 0 = Fanfare (kein Urteil ueber die Balance), sonst
	 * Gefechtsmusik. Vorgabe 1 -- der strenge Fall. */
	int           gefecht = (argc > 8) ? atoi(argv[8]) : 1;

	unsigned long bl = 0, zl = 0;
	unsigned char *bank = datei(argv[1], &bl);
	unsigned char *zmu  = datei(argv[2], &zl);

	if(!bank || !zmu) { fprintf(stderr, "Datei nicht lesbar\n"); return 1; }

	int n = mischer_init(bank, bl, rate, stimmen);

	if(!n) { fprintf(stderr, "Bank unbrauchbar\n"); return 1; }

	if(!mischer_stueck(zmu, zl)) { fprintf(stderr, "Stueck unbrauchbar\n"); return 1; }

	/* Siebtes Argument: an diese Sekunde springen, bevor gemischt wird. */
	if(argc > 7)
	{
		unsigned long ab = strtoul(argv[7], NULL, 10);

		mischer_springen((ULONG)(ab * 120));
		printf("gesprungen auf %lu s\n", ab);
	}

	printf("Bank: %d Instrumente, %lu Hz, %d Stimmen\n", n, rate, stimmen);

	unsigned long rahmen_gesamt = rate * sek;
	WORD *puffer = malloc(rahmen_gesamt * 2 * sizeof(WORD));

	if(!puffer) { fprintf(stderr, "kein Speicher\n"); return 1; }

	/* In Bloecken fuellen, genau wie es die Ausgabe spaeter tut -- ein
	 * einziger Riesenaufruf wuerde den Blockweg nicht pruefen. */
	const unsigned long block = 512;
	unsigned long getan = 0;

	while(getan < rahmen_gesamt)
	{
		unsigned long r = rahmen_gesamt - getan;
		if(r > block) r = block;
		mischer_fuellen(puffer + getan * 2, r);
		getan += r;
	}

	ULONG noten, spitze, verdraengt, ohne;
	ULONG gemeldet_geklemmt;
	mischer_zahlen(&noten, &spitze, &verdraengt, &ohne, &gemeldet_geklemmt);

	printf("%lu Noten, Spitze %lu Stimmen, %lu Verdraengungen, %lu ohne Instrument, %lu begrenzt\n",
	       (unsigned long)noten, (unsigned long)spitze,
	       (unsigned long)verdraengt, (unsigned long)ohne,
	       (unsigned long)gemeldet_geklemmt);

	/* Spitzenwert melden -- Uebersteuerung waere hoerbar und muss auffallen */
	long spitzenwert = 0;
	unsigned long geklemmt = 0;

	for(unsigned long i = 0; i < rahmen_gesamt * 2; i++)
	{
		long v = puffer[i];
		if(v < 0) v = -v;
		if(v > spitzenwert) spitzenwert = v;
		if(puffer[i] == 32767 || puffer[i] == -32768) geklemmt++;
	}

	printf("Spitzenpegel %ld von 32767, %lu Werte am Anschlag\n",
	       spitzenwert, geklemmt);

	FILE *w = fopen(argv[3], "wb");
	if(!w) { fprintf(stderr, "Ziel nicht schreibbar\n"); return 1; }

	unsigned long daten = rahmen_gesamt * 2 * 2;
	unsigned char kopf[44];
	memcpy(kopf, "RIFF", 4);       le32(kopf+4, 36 + daten);
	memcpy(kopf+8, "WAVEfmt ", 8); le32(kopf+16, 16);
	kopf[20]=1; kopf[21]=0; kopf[22]=2; kopf[23]=0;
	le32(kopf+24, rate);           le32(kopf+28, rate*4);
	kopf[32]=4; kopf[33]=0; kopf[34]=16; kopf[35]=0;
	memcpy(kopf+36, "data", 4);    le32(kopf+40, daten);
	fwrite(kopf, 1, 44, w);
	fwrite(puffer, 1, daten, w);
	fclose(w);

	printf("geschrieben: %s (%lu s stereo)\n", argv[3], sek);

	/* Letztes Argument: spielt dieses Stueck WAEHREND des Gefechts?
	 * Vorgabe ja. Nur dann wird "Effekte lauter als Musik" geurteilt --
	 * siehe die Begruendung in balance_messen(). */
	int fehler = balance_messen(bank, bl, zmu, zl, rate, stimmen, gefecht);

	free(puffer);

	if(fehler) { printf("NICHT BESTANDEN: %d Fehler\n", fehler); return 1; }

	printf("BESTANDEN: Balance Musik/Effekte gemessen, Gegenprobe faellt durch\n");

	return 0;
}
