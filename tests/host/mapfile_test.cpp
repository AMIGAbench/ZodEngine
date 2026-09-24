/*
 * Liest alle Originalkarten, schreibt sie aus den Strukturen zurueck und
 * vergleicht Byte fuer Byte. Damit ist belegt, dass die feldweise Lese- und
 * Schreibschicht (port/zod_mapfile.cpp) das x86-Dateiformat exakt trifft --
 * die Voraussetzung dafuer, dass Amiga und PC dieselben Karten benutzen.
 *
 * Aufruf: mapfile_test <verzeichnis> [verzeichnis...]
 */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <dirent.h>
#include <sys/stat.h>

#include <zod_mapfile.h>
#include <zod_wire.h>

static int checked = 0;
static int failures = 0;
static size_t padding_bytes = 0;

static bool read_file(const std::string &path, std::vector<char> &out)
{
	FILE *fp = fopen(path.c_str(), "rb");
	if(!fp) return false;

	fseek(fp, 0, SEEK_END);
	long n = ftell(fp);
	fseek(fp, 0, SEEK_SET);

	out.resize(n > 0 ? (size_t)n : 0);
	size_t got = n > 0 ? fread(&out[0], 1, (size_t)n, fp) : 0;
	fclose(fp);

	return got == out.size();
}

static void check_map(const std::string &path)
{
	std::vector<char> raw;
	map_basics basics;
	std::vector<map_zone> zones;
	std::vector<map_object> objects;
	std::vector<map_tile> tiles;

	if(!read_file(path, raw) || raw.empty())
	{
		printf("  FEHLER: nicht lesbar: %s\n", path.c_str());
		failures++;
		return;
	}

	if(!zod_mapfile_parse(&raw[0], (int)raw.size(), basics, zones, objects, tiles))
	{
		printf("  FEHLER: parse: %s (%d Bytes)\n", path.c_str(), (int)raw.size());
		failures++;
		return;
	}

	int expect = zod_mapfile_size(basics);
	if(expect != (int)raw.size())
	{
		printf("  FEHLER: Groesse %s: Datei %d, erwartet %d (%ux%u, %u Objekte, %u Zonen)\n",
		       path.c_str(), (int)raw.size(), expect, basics.width, basics.height,
		       basics.object_count, basics.zone_count);
		failures++;
		return;
	}

	std::vector<char> back(raw.size());
	int written = zod_mapfile_serialize(basics, zones, objects, tiles, &back[0], (int)back.size());

	if(written != (int)raw.size())
	{
		printf("  FEHLER: geschrieben %d statt %d: %s\n", written, (int)raw.size(), path.c_str());
		failures++;
		return;
	}

	/* Die Originaldateien enthalten in den Fuellbytes Stack-Muell (0xCC des
	 * MSVC-Builds, dazu wechselnde Werte). Diese Bytes liest kein Code; sie
	 * werden deshalb nur gezaehlt, nicht als Fehler gewertet. Alles andere
	 * muss byte-identisch sein. */
	std::vector<bool> padding(raw.size(), false);
	{
		size_t i, off;

		padding[55] = true;   /* hinter player_count */
		padding[59] = true;   /* hinter terrain_type */

		/* Namensfeld hinter dem Stringende */
		for(i = 4; i < 54; i++)
			if(raw[i] == 0) { for(size_t j = i; j < 54; j++) padding[j] = true; break; }

		off = ZOD_MAPFILE_BASICS_SIZE + (size_t)basics.zone_count * ZOD_MAPFILE_ZONE_SIZE;
		for(i = 0; i < basics.object_count; i++)
		{
			padding[off + i * ZOD_MAPFILE_OBJECT_SIZE + 10] = true;
			padding[off + i * ZOD_MAPFILE_OBJECT_SIZE + 11] = true;
		}
	}

	size_t diff_payload = 0, diff_padding = 0, first_diff = 0;
	for(size_t i = 0; i < raw.size(); i++)
		if(raw[i] != back[i])
		{
			if(padding[i]) diff_padding++;
			else
			{
				if(!diff_payload) first_diff = i;
				diff_payload++;
			}
		}

	if(diff_payload)
	{
		printf("  FEHLER: %u Nutzbytes unterschiedlich (erster ab %u) in %s\n",
		       (unsigned)diff_payload, (unsigned)first_diff, path.c_str());
		failures++;
		return;
	}

	padding_bytes += diff_padding;
	checked++;
}

static void walk(const std::string &dir)
{
	DIR *d = opendir(dir.c_str());
	if(!d) return;

	struct dirent *e;
	while((e = readdir(d)))
	{
		std::string name = e->d_name;
		if(name == "." || name == "..") continue;

		std::string full = dir + "/" + name;
		struct stat st;
		if(stat(full.c_str(), &st)) continue;

		if(S_ISDIR(st.st_mode))
			walk(full);
		else if(name.size() > 4 && name.compare(name.size() - 4, 4, ".map") == 0)
			check_map(full);
	}

	closedir(d);
}

int main(int argc, char **argv)
{
	int i;

	printf("mapfile Test, Tauschpfad %s\n",
	       zod_wire_swaps() ? "aktiv (Big-Endian-Simulation)" : "inaktiv");

	if(argc < 2)
	{
		printf("Aufruf: %s <verzeichnis> [verzeichnis...]\n", argv[0]);
		return 2;
	}

	for(i = 1; i < argc; i++)
		walk(argv[i]);

	if(!checked)
	{
		printf("[FAIL] mapfile_test: keine Karten gefunden\n");
		return 1;
	}

	if(failures)
	{
		printf("[FAIL] mapfile_test: %d von %d Karten fehlerhaft\n", failures, checked + failures);
		return 1;
	}

	printf("[OK] mapfile_test: %d Karten inhaltsgleich zurueckgeschrieben (%u Fuellbytes normalisiert)\n",
	       checked, (unsigned)padding_bytes);
	return 0;
}
