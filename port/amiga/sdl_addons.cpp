/*
 * Ersatz fuer SDL_image, SDL_ttf und SDL_mixer auf dem Amiga.
 *
 * SDL_image: PNG ueber stb_image (public domain, port/third_party), BMP ueber
 *            SDL_LoadBMP. Ergebnis ist immer eine 32-Bit-Surface mit Alpha, wie
 *            sie die Engine erwartet.
 * SDL_ttf:   Attrappe (die Engine nutzt Bitmap-Schriften; Aufrufer pruefen auf 0).
 * SDL_mixer: stumm in P3; AHI folgt in P5.
 */
#include <stdio.h>
#include <string.h>

#include <stdlib.h>

#include <SDL/SDL.h>
#include <SDL/SDL_image.h>
#include <SDL/SDL_ttf.h>
#include <SDL/SDL_mixer.h>

#include "amiga_audio.h"
#include "zod_log.h"
#include "musik_ausgabe.h"
#include "musik_mischer.h"
#include "amiga_audio.h"
#include "zod_pack.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO_WRITE
#define STBI_NO_JPEG
#define STBI_NO_PSD
#define STBI_NO_TGA
#define STBI_NO_GIF
#define STBI_NO_HDR
#define STBI_NO_PIC
#define STBI_NO_PNM
#define STBI_ONLY_PNG
#define STBI_ONLY_BMP
#define STBI_NO_LINEAR
#define STBI_NO_SIMD
#include "stb_image.h"

static char addon_error[256] = "";

static void set_error(const char *msg)
{
	strncpy(addon_error, msg ? msg : "", sizeof(addon_error) - 1);
	addon_error[sizeof(addon_error) - 1] = 0;
}

/* ------------------------------------------------------------------ */
/* SDL_image                                                           */
/* ------------------------------------------------------------------ */

static int has_suffix(const char *name, const char *suffix)
{
	size_t n = strlen(name), s = strlen(suffix);
	size_t i;

	if(n < s) return 0;

	for(i = 0; i < s; i++)
	{
		char a = name[n - s + i], b = suffix[i];

		if(a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
		if(a != b) return 0;
	}

	return 1;
}

SDL_Surface *IMG_Load(const char *file)
{
	SDL_Surface *surface;
	unsigned char *pixels;
	int w = 0, h = 0, comp = 0;
	Uint32 rmask, gmask, bmask, amask;
	int y;

	if(!file) return 0;

	/* ZUERST im Archiv nachsehen.
	 *
	 * Mehrere Stellen der Engine rufen IMG_Load direkt auf und gehen damit am
	 * Archiv vorbei (zmap_crater_graphics, cgatling, die Fahrzeuge, das
	 * Programmsymbol). Auf dem Amiga ist jede Dateioeffnung teuer, und bis
	 * zum 18.09. kam von dort ausserdem eine 32-Bit-Flaeche, die der Blitter
	 * gar nicht zeichnen konnte -- die Krater fehlten deshalb vollstaendig,
	 * ohne eine einzige Fehlanzeige im Log (die meldet nur zod_pack_load,
	 * und das wurde hier nie gefragt).
	 *
	 * Der Schluessel des Archivs IST der Originalpfad, genau damit solche
	 * Aufrufer unveraendert bleiben koennen. Also hier nachschlagen. */
	{
		SDL_Surface *aus_archiv = zod_pack_load_quiet(file);

		if(aus_archiv) return aus_archiv;
	}

	/* BMP kann SDL selbst (Planeten-Kacheln, Splash) */
	if(has_suffix(file, ".bmp"))
	{
		surface = SDL_LoadBMP(file);
		if(!surface) set_error(SDL_GetError());
		return surface;
	}

	pixels = stbi_load(file, &w, &h, &comp, 4);

	if(!pixels)
	{
		set_error(stbi_failure_reason());
		return 0;
	}

	/* Im SCHIRMFORMAT liefern, nicht in 32 Bit.
	 *
	 * Diese Funktion ist der Rueckfall fuer Dateien, die NICHT im Archiv
	 * liegen. Sie lieferte bisher immer 32 Bit -- und genau daran ist am
	 * 18.09. die Gebirgskette gescheitert: Der Blitter hat keinen Weg 32 -> 8,
	 * er kehrt zurueck, ohne zu zeichnen. Im Log sah man nur
	 * "32-Bit-Flaeche angefordert: 16x16" und danach die Blit-Warnung.
	 *
	 * Ein Bild von der Platte ist auf dem Amiga ohnehin der Ausnahmefall
	 * (jede Dateioeffnung ist teuer), deshalb wird der Name gemeldet: was hier
	 * auftaucht, gehoert ins Archiv. */
	{
		static int gemeldet = 0;

		if(gemeldet < 8)
		{
			gemeldet++;
			ZLOG("Bild von der Platte statt aus dem Archiv: %s (%ldx%ld)\n",
			     file, (long)w, (long)h);
		}
	}

	SDL_Surface *schirm = SDL_GetVideoSurface();
	const int tiefe = (schirm && schirm->format) ? schirm->format->BitsPerPixel : 32;

	if(tiefe == 8)
	{
		surface = SDL_CreateRGBSurface(SDL_SWSURFACE, w, h, 8, 0, 0, 0, 0);

		if(!surface)
		{
			set_error(SDL_GetError());
			stbi_image_free(pixels);
			return 0;
		}

		/* Platz 0 ist durchgehend der Farbschluessel (siehe zod_palette). */
		for(y = 0; y < h; y++)
		{
			Uint8 *dst = (Uint8*)surface->pixels + y * surface->pitch;

			for(int x = 0; x < w; x++)
			{
				const unsigned char *p = pixels + ((size_t)y * w + x) * 4;

				dst[x] = p[3] < 128 ? 0
				       : (Uint8)SDL_MapRGB(surface->format, p[0], p[1], p[2]);
			}
		}

		SDL_SetColorKey(surface, SDL_SRCCOLORKEY, 0);

		stbi_image_free(pixels);

		return surface;
	}

	/* stb liefert die Bytes in der Reihenfolge R,G,B,A im Speicher */
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
	rmask = 0xff000000; gmask = 0x00ff0000; bmask = 0x0000ff00; amask = 0x000000ff;
#else
	rmask = 0x000000ff; gmask = 0x0000ff00; bmask = 0x00ff0000; amask = 0xff000000;
#endif

	surface = SDL_CreateRGBSurface(SDL_SWSURFACE, w, h, 32, rmask, gmask, bmask, amask);

	if(!surface)
	{
		set_error(SDL_GetError());
		stbi_image_free(pixels);
		return 0;
	}

	if(SDL_MUSTLOCK(surface)) SDL_LockSurface(surface);

	for(y = 0; y < h; y++)
		memcpy((Uint8*)surface->pixels + y * surface->pitch, pixels + (size_t)y * w * 4, (size_t)w * 4);

	if(SDL_MUSTLOCK(surface)) SDL_UnlockSurface(surface);

	stbi_image_free(pixels);

	return surface;
}

const char *IMG_GetError(void)
{
	return addon_error;
}

/* ------------------------------------------------------------------ */
/* SDL_ttf (Attrappe)                                                  */
/* ------------------------------------------------------------------ */

int TTF_Init(void) { return 0; }
void TTF_Quit(void) {}
TTF_Font *TTF_OpenFont(const char *file, int ptsize) { (void)file; (void)ptsize; return 0; }
void TTF_CloseFont(TTF_Font *font) { (void)font; }

SDL_Surface *TTF_RenderText_Solid(TTF_Font *font, const char *text, SDL_Color fg)
{
	(void)font; (void)text; (void)fg;
	return 0;
}

const char *TTF_GetError(void) { return "SDL_ttf nicht verfuegbar"; }

/* ------------------------------------------------------------------ */
/* SDL_mixer über AHI (port/amiga/amiga_audio.cpp)                      */
/* ------------------------------------------------------------------ */
/*
 * Die Engine benutzt Mix_Chunk nur als Griff und setzt vor jedem Abspielen
 * das Feld volume (0..128); Mix_PlayChannel(-1, ...) sucht einen freien Kanal.
 * Genau das bildet das AHI-Modul ab. Die Klangdaten kommen aus dem Archiv
 * (signed 8 Bit mono) und muessen bis zum Schliessen gueltig bleiben, weil
 * AHI direkt daraus spielt -- sie werden deshalb nicht freigegeben.
 */

static int mix_frequency = 11025;
static Uint16 mix_format = 0x8008; /* AUDIO_S8 */
static int mix_channels_out = 1;
static int mix_channels = 8;

int Mix_Init(int flags) { return flags; }
void Mix_Quit(void) { zod_audio_close(); }

/* Steht weiter unten bei den Musikfunktionen -- gerufen wird sie aber
 * schon hier, beim Oeffnen des Tons. */
static int musik_aufsetzen(void);

/* Welches Stueck gerade aufliegt. Vorwaerts deklariert, weil
 * Mix_CloseAudio den Griff zuruecksetzen muss und weiter oben steht als
 * die Musikfunktionen. */
struct _Mix_Music;
static struct _Mix_Music *musik_jetzt = 0;

/* Die Instrumentenbank, einmal fuer alle Stuecke. */
static unsigned char *bank_daten = 0;
static unsigned int   bank_laenge = 0;
static int            musik_bereit = 0;

int Mix_OpenAudio(int frequency, Uint16 format, int channels, int chunksize)
{
	(void)chunksize;
	(void)format;
	(void)channels;

	/* Die Klaenge liegen durchweg mit 11025 Hz mono vor, und abgespielt werden
	 * sie mit genau dieser Rate. Die Mischfrequenz richtet sich deshalb nach
	 * den Daten, nicht nach dem Wunsch der Engine (22050 stereo) -- sonst
	 * mischt AHI ohne Not hoeher, als das Material hergibt. */
	(void)frequency;
	mix_frequency = 11025;
	mix_format = 0x8008;
	mix_channels_out = 1;

	/* EIN WEG FUER ALLES.
	 *
	 * AHI und Paula wollen dieselbe Hardware. Laeuft AHI, bekommt
	 * audio.device keine Kanaele mehr -- auf der V1200 belegt mit
	 * "Musik: audio.device gibt keine Kanaele her", waehrend zwei Zeilen
	 * darueber "AHI: 8 Kanaele" stand. "Effekte ueber AHI, Musik ueber
	 * Paula" war deshalb kein Entwurf, sondern ein Widerspruch.
	 *
	 * Jetzt mischt der eigene Mischer beides, und der Ausgabeweg bekommt
	 * einen einzigen Stereo-Strom:
	 *
	 *   AHI    ein Kanal statt acht, AHIs Mischer bleibt unbenutzt
	 *   Paula  AHI wird gar nicht erst geoeffnet
	 *
	 * Traegt das nicht, faellt es auf den alten Weg zurueck (AHI mischt
	 * die Effekte, Musik als PCM) -- ein Spiel ohne Ton waere die
	 * schlechtere Antwort auf einen fehlenden Treiber. */
	if(musik_aufsetzen())
	{
		zod_audio_mischerweg(1);

		ZLOG("Ton: ein Weg fuer alles -- %s\n",
		     musik_weg() == MUSIK_PAULA ? "Paula" : "AHI");

		return 0;
	}

	ZLOG("Ton: Rueckfall auf den alten Weg (AHI mischt die Effekte)\n");

	return zod_audio_open(mix_channels, mix_frequency) ? 0 : -1;
}

void Mix_CloseAudio(void)
{
	/* zod_audio_close baut die eigene Ausgabe mit ab. Damit ein spaeteres
	 * Mix_OpenAudio sie WIEDER aufsetzt, muss der Merker zurueck -- sonst
	 * haette die Engine nach einem Schliessen und Oeffnen still keinen Ton
	 * mehr, und die Ursache waere ein Zaehler, den niemand sieht. */
	zod_audio_close();

	/* Der Griff zeigt auf ein _Mix_Music, das die Engine weiterhin haelt.
	 * Er darf nach dem Abbau nicht stehenbleiben: Mix_PlayingMusic wuerde
	 * sonst den Mischer fragen, der gar nicht mehr laeuft. */
	musik_jetzt = 0;
	musik_bereit = 0;

	if(bank_daten) { free(bank_daten); bank_daten = 0; bank_laenge = 0; }
}

int Mix_QuerySpec(int *frequency, Uint16 *format, int *channels)
{
	if(frequency) *frequency = mix_frequency;
	if(format) *format = mix_format;
	if(channels) *channels = mix_channels_out;
	return 1;
}

int Mix_AllocateChannels(int numchans)
{
	/* AHI mischt in Software; 32 Kanaele waeren auf einem 040 Verschwendung */
	if(numchans > 0 && numchans < mix_channels) mix_channels = numchans;

	return mix_channels;
}

Mix_Chunk *Mix_LoadWAV(const char *file)
{
	unsigned char *data = 0;
	unsigned int length = 0, rate = 0;

	if(!file) return 0;

	if(!zod_pack_load_sound(file, &data, &length, &rate))
		return 0;

	int sound_id = zod_audio_add_sound(data, length, rate);

	if(sound_id < 0)
	{
		free(data);
		return 0;
	}

	Mix_Chunk *chunk = (Mix_Chunk*)malloc(sizeof(Mix_Chunk));

	if(!chunk)
	{
		free(data);
		return 0;
	}

	/* allocated traegt die Klang-Nummer des AHI-Moduls */
	chunk->allocated = sound_id;
	chunk->abuf = data;
	chunk->alen = length;
	chunk->volume = MIX_MAX_VOLUME;

	return chunk;
}

void Mix_FreeChunk(Mix_Chunk *chunk)
{
	/* Daten bleiben liegen: AHI spielt direkt daraus, und die Engine gibt
	 * Klaenge ohnehin erst beim Beenden frei. */
	if(chunk) free(chunk);
}
/* ---- Musik ----------------------------------------------------------
 *
 * Es gibt genau EINEN Weg: Ereignisstrom (.zmu) plus Instrumentenbank,
 * gemischt in musik_mischer.c, ausgegeben ueber AHI oder Paula. Rund
 * 400 KB fuer den ganzen Soundtrack.
 *
 * Der frueher hier vorhandene PCM-Weg (fertige Aufnahmen aus
 * zod_music.zpk, 24 MB) ist ERSATZLOS ENTFERNT. Er war seit dem eigenen
 * Mischer nur noch dieselbe Musik in gross, und ein zweiter Weg, der
 * still einspringt, verdeckt genau die Fehler, die man sehen will --
 * im Emulatorlauf vom 22.09. stand deshalb "es laeuft" im Protokoll,
 * waehrend der neue Weg in Wahrheit gar nicht geoeffnet hatte.
 *
 * Fehlen Bank oder Stueck, gibt es keine Musik. Die Klangeffekte laufen
 * weiter -- der Mischer braucht fuer sie keine Bank.
 *
 * Die Engine fragt weiterhin nach "assets/sounds/music_desert.ogg"; die
 * Zuordnung auf das Stueck steht in zmu_name_zu(). So bleibt
 * zmusic_engine.cpp unberuehrt. */
struct _Mix_Music
{
	unsigned char	*zmu;
	unsigned int	zmu_len;
};

/* Welcher Ausgabeweg. Gesetzt aus der Befehlszeile (-M ahi|paula), Vorgabe
 * AHI: das laeuft auf jeder Soundkarte, Paula nur auf echter Amiga-Hardware. */
int zod_musik_weg = MUSIK_AHI;

namespace
{

int musik_lautstaerke_wert = 128;

}

/* Welches Stueck gehoert zu diesem Dateinamen?
 *
 * Die Engine fragt nach "assets/sounds/music_<planet>.ogg" bzw.
 * "assets/sounds/ABATTLE.mp3". Auf der CD heissen dieselben Stuecke
 * AD1, AJ1, AA1, AC1, AV1 und ABATTLE -- die Zuordnung steht hier an
 * EINER Stelle, damit sie nicht an mehreren auseinanderlaufen kann. */
static const char *zmu_name_zu(const char *datei)
{
	static const struct { const char *teil; const char *stueck; } tabelle[] =
	{
		{ "music_desert",   "AD1"     },
		{ "music_jungle",   "AJ1"     },
		{ "music_arctic",   "AA1"     },
		{ "music_city",     "AC1"     },
		{ "music_volcanic", "AV1"     },
		{ "ABATTLE",        "ABATTLE" }
	};

	for(unsigned i = 0; i < sizeof(tabelle) / sizeof(tabelle[0]); i++)
		if(strstr(datei, tabelle[i].teil)) return tabelle[i].stueck;

	return 0;
}

static unsigned char *datei_ganz(const char *name, unsigned int *laenge)
{
	FILE *f = fopen(name, "rb");
	unsigned char *p;
	long n;

	if(!f) return 0;

	fseek(f, 0, SEEK_END);
	n = ftell(f);
	fseek(f, 0, SEEK_SET);

	if(n <= 0) { fclose(f); return 0; }

	p = (unsigned char *)malloc((size_t)n);

	if(!p) { fclose(f); return 0; }

	if(fread(p, 1, (size_t)n, f) != (size_t)n) { free(p); fclose(f); return 0; }

	fclose(f);
	*laenge = (unsigned int)n;

	return p;
}

static int musik_aufsetzen(void)
{
	if(musik_bereit) return musik_bereit > 0;

	musik_bereit = -1;                      /* nur EINMAL versuchen */

	/* Die Bank ist OPTIONAL. Fehlt sie, mischt der Mischer nur die
	 * Klangeffekte -- das Spiel soll dann seine Klaenge behalten, nicht
	 * verstummen. Musik gibt es in dem Fall keine. */
	bank_daten = datei_ganz("music/MIDIBANK", &bank_laenge);

	if(!bank_daten)
		ZLOG("Musik: music/MIDIBANK fehlt -- es laufen nur die Klangeffekte\n");

	/* Fuer den AHI-Weg muss der AHI-Kontext VORHER stehen: der Strom
	 * laeuft ueber genau diesen einen. Ein Kanal genuegt, denn gemischt
	 * wird hier, nicht bei AHI. */
	if(zod_musik_weg != MUSIK_PAULA)
	{
		if(!zod_audio_open(1, mix_frequency))
		{
			ZLOG("Musik: AHI liess sich nicht oeffnen\n");
			if(bank_daten) { free(bank_daten); bank_daten = 0; }
			return 0;
		}
	}

	if(!musik_start(zod_musik_weg, bank_daten, bank_laenge, 22050, 32))
	{
		ZLOG("Musik: Ausgabe liess sich nicht oeffnen\n");
		if(bank_daten) { free(bank_daten); bank_daten = 0; }
		return 0;
	}

	/* Acht Stimmen fuer die Effekte, der Rest fuer die Musik. Gemessen
	 * braucht die Musik hoechstens 18. */
	mischer_effektstimmen(8);

	musik_bereit = 1;

	return 1;
}

/* --- Ton fuer eine Videosequenz vollstaendig freigeben ------------------
 *
 * AHI vergibt je Treiber genau EINEN Kontext, Paula hat vier Kanaele. Ein
 * externer Player kann sie deshalb nicht bekommen, solange die Engine sie
 * haelt -- "Effekte hier, Film dort" waere kein Entwurf, sondern ein
 * Widerspruch. Also ganz hergeben und danach genau so wieder aufsetzen.
 *
 * WARUM NICHT EINFACH Mix_CloseAudio/Mix_OpenAudio: Mix_CloseAudio gibt die
 * Instrumentenbank frei (rund 100 KB von der Platte) und setzt musik_bereit
 * zurueck. Je Film einmal neu laden waere Verschwendung, und musik_aufsetzen
 * ist eine EINMAL-Sperre (musik_bereit), die ein zweites Aufsetzen gar nicht
 * zulaesst. Deshalb dieses Paar, das nur den Hardware-Teil anfasst.
 *
 * Das laufende Stueck faengt danach von vorn an. Das ist hingenommen: die
 * Abspielstelle des Mischers ist nach aussen nicht lesbar, und die beiden
 * Stellen, an denen ein Film laeuft (Vorspann, Rundenende), wechseln
 * ohnehin gleich darauf die Karte. */

static int ton_angehalten = 0;
static _Mix_Music *ton_stueck_vorher = 0;

extern "C" void zod_ton_anhalten(void)
{
	if(ton_angehalten) return;

	ton_angehalten = 1;
	ton_stueck_vorher = musik_jetzt;

	if(musik_bereit > 0) musik_stop();

	zod_audio_close();

	ZLOG("Ton: fuer die Videosequenz freigegeben\n");
}

extern "C" void zod_ton_fortsetzen(void)
{
	if(!ton_angehalten) return;

	ton_angehalten = 0;

	if(musik_bereit <= 0) return;           /* es lief vorher schon nichts */

	if(zod_musik_weg != MUSIK_PAULA)
	{
		if(!zod_audio_open(1, mix_frequency))
		{
			ZLOG("Ton: AHI kam nach der Videosequenz nicht zurueck\n");
			musik_bereit = -1;
			return;
		}
	}

	if(!musik_start(zod_musik_weg, bank_daten, bank_laenge, 22050, 32))
	{
		ZLOG("Ton: Ausgabe kam nach der Videosequenz nicht zurueck\n");
		musik_bereit = -1;
		return;
	}

	mischer_effektstimmen(8);
	zod_audio_mischerweg(1);

	/* Die Klangliste hat zod_audio_close absichtlich stehenlassen (siehe
	 * dort) -- die Effekte spielen also sofort wieder. Nur die Musik muss
	 * neu aufgelegt werden. */
	if(ton_stueck_vorher)
	{
		musik_pause(0);
		musik_lautstaerke(musik_lautstaerke_wert);

		if(musik_spiele(ton_stueck_vorher->zmu, ton_stueck_vorher->zmu_len, 1))
			musik_jetzt = ton_stueck_vorher;
	}

	ZLOG("Ton: nach der Videosequenz wieder aufgesetzt (%s)\n",
	     musik_weg() == MUSIK_PAULA ? "Paula" : "AHI");
}

Mix_Music *Mix_LoadMUS(const char *file)
{
	const char *stueck;
	unsigned char *zd;
	unsigned int zl = 0;
	char pfad[64];
	_Mix_Music *m;

	if(!file) return 0;

	stueck = zmu_name_zu(file);

	if(!stueck || !musik_aufsetzen()) return 0;

	snprintf(pfad, sizeof(pfad), "music/%s.zmu", stueck);

	zd = datei_ganz(pfad, &zl);

	if(!zd)
	{
		ZLOG("Musik: %s fehlt -- %s bleibt stumm\n", pfad, file);

		return 0;
	}

	m = (_Mix_Music*)malloc(sizeof(_Mix_Music));

	if(!m) { free(zd); return 0; }

	m->zmu = zd;
	m->zmu_len = zl;

	ZLOG("Musik: %s -> %s, %ld Byte\n", file, pfad, (long)zl);

	return m;
}

void Mix_FreeMusic(Mix_Music *music)
{
	_Mix_Music *m = (_Mix_Music*)music;

	if(!m) return;

	if(musik_jetzt == m) { musik_pause(1); musik_jetzt = 0; }

	free(m->zmu);
	free(m);
}

int Mix_PlayChannel(int channel, Mix_Chunk *chunk, int loops)
{
	(void)channel;   /* die Engine uebergibt immer -1 = beliebiger Kanal */

	if(!chunk) return -1;

	return zod_audio_play(chunk->allocated, chunk->volume, loops != 0);
}

int Mix_HaltChannel(int channel)
{
	zod_audio_stop(channel);
	return 0;
}

int Mix_Playing(int channel)
{
	return zod_audio_playing(channel);
}

int Mix_Volume(int channel, int volume)
{
	/* channel == -1 heisst bei SDL_mixer ALLE Kanaele -- genau so ruft die
	 * Engine es aus dem Lautstaerkeregler im Spiel auf
	 * (ZPlayer::SetSoundSetting). Das ist hier bisher ins Leere gelaufen:
	 * Die Bedingung verlangte channel >= 0, der Regler hatte also auf die
	 * Klangeffekte UEBERHAUPT KEINE Wirkung. */
	if(volume < 0) return volume;

	if(channel < 0) zod_audio_set_master(volume);
	else            zod_audio_set_volume(channel, volume);

	return volume;
}

int Mix_PlayMusic(Mix_Music *music, int loops)
{
	_Mix_Music *m = (_Mix_Music*)music;

	if(!m) return -1;

	/* loops: -1 heisst bei SDL_mixer endlos. Die Engine uebergibt fuer
	 * Planetenmusik -1. */
	musik_pause(0);
	musik_lautstaerke(musik_lautstaerke_wert);

	if(!musik_spiele(m->zmu, m->zmu_len, loops != 0)) return -1;

	musik_jetzt = m;

	return 0;
}

int Mix_VolumeMusic(int volume)
{
	int vorher = musik_lautstaerke_wert;

	if(volume >= 0)
	{
		musik_lautstaerke_wert = volume > 128 ? 128 : volume;

		if(musik_bereit > 0) musik_lautstaerke(musik_lautstaerke_wert);
	}

	return vorher;
}

int Mix_PlayingMusic(void)
{
	return musik_jetzt ? musik_laeuft() : 0;
}

int Mix_HaltMusic(void)
{
	/* Nicht musik_stop(): das gaebe den Ausgabeweg samt Process frei, und
	 * das naechste Stueck muesste alles neu aufsetzen. Stillhalten reicht. */
	if(musik_bereit > 0) musik_pause(1);

	return 0;
}

/* Springen. ZMusicEngine setzt damit die Gefahrenstufe um: sie waehlt eine
 * vorberechnete Stelle des Stueckes. Der Ereignisstrom zaehlt in Schritten
 * zu 1/120 s -- eine feste Groesse, kein Tempo im Spiel. */
int Mix_SetMusicPosition(double position)
{
	if(!musik_jetzt) return -1;

	if(position < 0) position = 0;

	musik_springen((unsigned long)(position * 120.0));

	return 0;
}

const char *Mix_GetError(void) { return addon_error; }
