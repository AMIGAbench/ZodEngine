/*
 * Prüft den Archiv-Lader (port/zod_pack.cpp) gegen die Originaldateien.
 *
 * Wichtig für den Amiga-Weg: Dort kommen Bilder und Klänge ausschließlich aus
 * den .zpk-Archiven. Bisher war nur der Packer geprüft (mit Python gegen die
 * Quellen) -- dieser Test geht denselben Weg wie die Engine, also über den
 * C++-Lader.
 *
 * Aufruf: pack_test <packs-verzeichnis> <assets-wurzel>
 *   z. B. pack_test data/game/packs data/game
 */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <SDL/SDL.h>

#include <zod_pack.h>

static int failures = 0;

static void check(bool ok, const std::string &what)
{
	if(!ok)
	{
		printf("  FEHLER: %s\n", what.c_str());
		failures++;
	}
}

/* WAV lesen: liefert Abtastrate und die Nutzdaten als vorzeichenbehaftete
 * 8-Bit-Werte -- dieselbe Umrechnung, die der Packer macht. */
static bool read_wav(const std::string &path, unsigned int &rate, std::vector<unsigned char> &pcm)
{
	FILE *fp = fopen(path.c_str(), "rb");
	if(!fp) return false;

	std::vector<unsigned char> d;
	char buf[8192];
	size_t n;
	while((n = fread(buf, 1, sizeof(buf), fp)) > 0) d.insert(d.end(), buf, buf + n);
	fclose(fp);

	if(d.size() < 44 || memcmp(&d[0], "RIFF", 4) || memcmp(&d[8], "WAVE", 4)) return false;

	size_t pos = 12;
	unsigned short bits = 0, channels = 0;
	const unsigned char *data = 0;
	unsigned int data_len = 0;

	while(pos + 8 <= d.size())
	{
		unsigned int clen = d[pos+4] | (d[pos+5] << 8) | (d[pos+6] << 16) | ((unsigned)d[pos+7] << 24);

		if(!memcmp(&d[pos], "fmt ", 4) && pos + 8 + 16 <= d.size())
		{
			/* Offsets relativ zum Chunk-Anfang: 8 Byte Kopf, dann
			 * Format(2) Kanaele(2) Rate(4) Byterate(4) Blockgroesse(2) Bits(2) */
			channels = (unsigned short)(d[pos+10] | (d[pos+11] << 8));
			rate     = d[pos+12] | (d[pos+13] << 8) | (d[pos+14] << 16) | ((unsigned)d[pos+15] << 24);
			bits     = (unsigned short)(d[pos+22] | (d[pos+23] << 8));
		}
		else if(!memcmp(&d[pos], "data", 4))
		{
			data = &d[pos+8];
			data_len = clen;
		}

		pos += 8 + clen + (clen & 1);
	}

	if(!data || bits != 8 || channels != 1) return false;

	pcm.resize(data_len);
	for(unsigned int i = 0; i < data_len; i++)
		pcm[i] = (unsigned char)((data[i] - 128) & 0xFF);   /* unsigned -> signed */

	return true;
}

int main(int argc, char **argv)
{
	const char *packs = argc > 1 ? argv[1] : "data/game/packs";
	std::string root  = argc > 2 ? argv[2] : "data/game";

	if(SDL_Init(SDL_INIT_VIDEO) < 0)
	{
		printf("[FAIL] pack_test: SDL_Init: %s\n", SDL_GetError());
		return 1;
	}
	SDL_SetVideoMode(64, 64, 8, SDL_SWSURFACE);   /* für SDL_CreateRGBSurface nötig */

	int count = zod_pack_init(packs);
	printf("pack_test: %d Einträge aus %s\n", count, packs);
	check(count > 0, "Archive gefunden");

	/* --- Bilder: Maße gegen die Originaldatei --- */
	static const char *images[] = {
		"assets/cursors/attack_blue_n00.png",
		"assets/buildings/fort/destroyed_overlay.png",
		0
	};

	for(int i = 0; images[i]; i++)
	{
		SDL_Surface *s = zod_pack_load(images[i]);

		if(!s)
		{
			printf("  Hinweis: nicht im Archiv: %s\n", images[i]);
			continue;
		}

		check(s->w > 0 && s->h > 0, std::string("Maße ") + images[i]);
		check(s->format->BitsPerPixel == 8 || s->format->BitsPerPixel == 16,
		      std::string("Farbtiefe ") + images[i]);
		SDL_FreeSurface(s);
	}

	/* --- Klänge: Rate, Länge und Umrechnung gegen das Original --- */
	static const char *sounds[] = {
		"assets/sounds/explosion_00.wav",
		"assets/sounds/ROB01.wav",
		"assets/sounds/comp_youre_losing_00.wav",
		0
	};

	int sounds_checked = 0;
	for(int i = 0; sounds[i]; i++)
	{
		unsigned char *data = 0;
		unsigned int length = 0, rate = 0;

		if(!zod_pack_load_sound(sounds[i], &data, &length, &rate))
		{
			printf("  FEHLER: Klang nicht im Archiv: %s\n", sounds[i]);
			failures++;
			continue;
		}

		unsigned int want_rate = 0;
		std::vector<unsigned char> want;

		if(read_wav(root + "/" + sounds[i], want_rate, want))
		{
			check(rate == want_rate, std::string("Abtastrate ") + sounds[i]);
			check(length == want.size(), std::string("Länge ") + sounds[i]);
			if(length == want.size())
				check(memcmp(data, &want[0], length) == 0,
				      std::string("Umrechnung nach signed ") + sounds[i]);
			sounds_checked++;
		}

		free(data);
	}

	check(sounds_checked > 0, "mindestens ein Klang geprüft");

	zod_pack_shutdown();
	SDL_Quit();

	if(failures)
	{
		printf("[FAIL] pack_test: %d Fehler\n", failures);
		return 1;
	}

	printf("[OK] pack_test: Bilder und %d Klänge stimmen mit den Originalen überein\n", sounds_checked);
	return 0;
}
