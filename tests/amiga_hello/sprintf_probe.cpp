/*
 * Minimaltest für den Verdacht hinter den kaputten Dateinamen auf dem Amiga.
 *
 * Im Spiel entstehen Namen wie
 *   sprintf(buf, "...debri_large%d_%s_n%02d.png", k, planet_type_string[i].c_str(), j);
 * und im Serial-Log erschienen sie abgeschnitten, mit rohen Steuerbytes statt
 * des Planetennamens. Auf dem Host stimmen dieselben Namen.
 *
 * Geprüft wird deshalb genau diese Kombination: %d und %s gemischt, der
 * String einmal als Literal, einmal aus std::string::c_str().
 * Ergebnis geht über den seriellen Kanal, damit tools/run.sh es auswerten kann.
 */
#include <cstdio>
#include <cstring>
#include <string>

#include "debug.h"

static int failures = 0;

static void expect(const char *got, const char *want, const char *what)
{
	if(strcmp(got, want))
	{
		dbg_printf("FEHLER %s: '%s' statt '%s'\n", what, got, want);
		printf("FEHLER %s: '%s' statt '%s'\n", what, got, want);
		failures++;
	}
	else
	{
		dbg_printf("ok %s: %s\n", what, got);
	}
}

int main()
{
	char buf[500];
	std::string planet = "volcanic";

	dbg_boot();

	/* 1. nur Zahlen */
	sprintf(buf, "n%02d_%d", 7, 3);
	expect(buf, "n07_3", "nur Zahlen");

	/* 2. %s mit Literal */
	sprintf(buf, "a_%s_b", "volcanic");
	expect(buf, "a_volcanic_b", "%s mit Literal");

	/* 3. %s aus std::string (der Verdachtsfall) */
	sprintf(buf, "a_%s_b", planet.c_str());
	expect(buf, "a_volcanic_b", "%s aus std::string");

	/* 4. die Mischung wie im Spiel */
	sprintf(buf, "assets/planets/rock_effects/debri_large%d_%s_n%02d.png",
	        0, planet.c_str(), 5);
	expect(buf, "assets/planets/rock_effects/debri_large0_volcanic_n05.png",
	       "Mischung wie im Spiel");

	/* 5. dasselbe über snprintf */
	snprintf(buf, sizeof(buf), "debri_large%d_%s_n%02d.png", 1, planet.c_str(), 11);
	expect(buf, "debri_large1_volcanic_n11.png", "snprintf");

	/* 6. std::string-Verkettung als Gegenprobe (kommt ohne varargs aus) */
	std::string joined = std::string("debri_large0_") + planet + "_n05.png";
	expect(joined.c_str(), "debri_large0_volcanic_n05.png", "String-Verkettung");

	/* 7. fprintf: schreibt die Engine Einstellungen und Karten korrekt?
	 * Dieselbe Frage wie bei sprintf, aber fuer Dateiausgaben (P8). */
	{
		const char *path = "T:zod_fprintf_probe.txt";
		FILE *fp = fopen(path, "w");

		if(!fp)
		{
			dbg_printf("Hinweis: %s nicht schreibbar, fprintf ungeprueft\n", path);
		}
		else
		{
			fprintf(fp, "wert=%d name=%s zahl=%02d\n", 7, planet.c_str(), 5);
			fclose(fp);

			fp = fopen(path, "r");
			if(fp)
			{
				char line[200];

				if(fgets(line, sizeof(line), fp))
				{
					char *nl = strchr(line, '\n');
					if(nl) *nl = 0;
					expect(line, "wert=7 name=volcanic zahl=05", "fprintf in Datei");
				}
				fclose(fp);
			}
		}
	}

	printf("sprintf_probe: %d Fehler\n", failures);

	if(failures)
	{
		dbg_fail("sprintf_probe");
		return 20;
	}

	dbg_ok("sprintf_probe");
	return 0;
}
