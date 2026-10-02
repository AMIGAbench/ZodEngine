/*
 * Das y-BAND der Vorzeichner: beweist, dass es nur verwirft, was ohnehin
 * nichts zeichnet.
 *
 * WARUM EIN EIGENER TEST UND KEIN BILDVERGLEICH: Ein Bildvergleich zweier
 * Laeufe ist hier blind. Das ist in diesem Projekt belegt -- beim
 * Kartenzuschnitt ergab "alt gegen neu" 26 953 abweichende Punkte und die
 * Kontrolle (zweimal dasselbe Binary) 26 957. Die Abweichung war
 * vollstaendig Spielsituation. Geprueft wird deshalb die AUSWAHL selbst,
 * ueber einen gefegten Parameterraum.
 *
 * Nachgebildet wird genau die Rechnung aus ZPlayer::RenderObjects:
 * Sortierschluessel ist `loc.y + height_pix`, das Band ist
 * `key >= oben` und `key - height_pix <= unten`.
 */
#include <stdio.h>
#include <stdlib.h>
#include <vector>
#include <algorithm>

struct Obj { int y, h; };

/* Die Fassung aus der Engine: zwei binaere Suchen auf der sortierten Liste. */
static void band(const std::vector<Obj> &v, int oben, int unten,
                 size_t &a, size_t &b)
{
	size_t lo = 0, hi = v.size();

	while(lo < hi)
	{
		size_t m = (lo + hi) / 2;

		if(v[m].y + v[m].h < oben) lo = m + 1; else hi = m;
	}

	a = lo;

	lo = a; hi = v.size();

	while(lo < hi)
	{
		size_t m = (lo + hi) / 2;

		if(v[m].y <= unten) lo = m + 1; else hi = m;
	}

	b = lo;
}

int main(void)
{
	const int HOEHE = 48;          /* Stein: 3 Kacheln */
	int fehler = 0;
	unsigned long faelle = 0, drin = 0, besucht = 0;

	/* Ein Gitter von Steinen wie auf einer echten Karte: kachelbuendig. */
	std::vector<Obj> v;

	for(int ty = 0; ty < 86; ty++)
		for(int k = 0; k < 2; k++)
			v.push_back(Obj{ ty * 16, HOEHE });

	std::sort(v.begin(), v.end(),
	          [](const Obj &p, const Obj &q){ return p.y + p.h < q.y + q.h; });

	for(int oben = 0; oben <= 1376 - 444; oben += 7)
	{
		const int unten = oben + 444;
		size_t a, b;

		band(v, oben, unten, a, b);

		besucht += (unsigned long)(b - a);

		for(size_t i = 0; i < v.size(); i++)
		{
			/* Was WUERDE gezeichnet? Der Schatten belegt y .. y+h. */
			const bool sichtbar = (v[i].y + v[i].h >= oben) && (v[i].y <= unten);
			const bool im_band  = (i >= a && i < b);

			faelle++;

			if(sichtbar) drin++;

			/* DIE EINE FORDERUNG: nichts Sichtbares darf herausfallen.
			 * Dass das Band ein paar Unsichtbare mitnimmt, ist in Ordnung
			 * -- es ist ein zusammenhaengender Streifen. */
			if(sichtbar && !im_band)
			{
				if(fehler < 8)
					printf("FEHLER: sichtbarer Eintrag %u (y=%d) faellt aus dem"
					       " Band [%u,%u) bei oben=%d\n",
					       (unsigned)i, v[i].y, (unsigned)a, (unsigned)b, oben);
				fehler++;
			}

		}
	}

	printf("Vorzeichen-Band: %lu Faelle, %lu sichtbar, %lu besucht (%lu %%)\n",
	       faelle, drin, besucht, 100UL * besucht / (faelle ? faelle : 1));

	/* SELBSTSCHUTZ, und er musste nachgebessert werden: Ein Band, das die
	 * ganze Liste nimmt, besteht die Hauptforderung immer -- es faellt ja
	 * nichts heraus. Der erste Entwurf zaehlte die AUSSORTIERTEN und liess
	 * deshalb eine Gegenprobe durch, in der `b = v.size()` gesetzt war.
	 *
	 * Auch "besucht unter 75 % der Liste" war noch zu lax: mit
	 * `b = v.size()` bleibt die untere Grenze `a` erhalten, und ueber den
	 * ganzen Fegedurchgang liegt der Mittelwert dann bei 68 %. Die
	 * Gegenprobe kam zweimal durch.
	 *
	 * Gewertet wird deshalb gegen die WIRKLICH SICHTBAREN: ein scharfes Band
	 * besucht hoechstens das 1,5-fache davon. Das ist die Groesse, auf die
	 * es ankommt, und sie laesst beide Gegenproben durchfallen. */
	if(besucht * 2 > drin * 3)
	{
		printf("FEHLER: das Band besucht %lu Eintraege fuer %lu sichtbare"
		       " (%lu %%) -- es beschneidet nicht scharf genug, der Test"
		       " prueft damit zu wenig\n",
		       besucht, drin, 100UL * besucht / (drin ? drin : 1));
		fehler++;
	}

	if(fehler) { printf("NICHT BESTANDEN: %d Fehler\n", fehler); return 1; }

	printf("BESTANDEN: kein sichtbarer Vorzeichner faellt aus dem Band,"
	       " und das Band wirkt\n");

	return 0;
}
