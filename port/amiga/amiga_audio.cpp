#include "amiga_audio.h"

#ifdef __amigaos__

#include <string.h>

#include <devices/ahi.h>
#include <utility/hooks.h>
#include <clib/alib_protos.h>   /* HookEntry */
#include <devices/timer.h>
#include <proto/ahi.h>
#include <proto/exec.h>
#include <proto/timer.h>

#include "zod_log.h"
#include "musik_mischer.h"
#include "musik_ausgabe.h"


#define ZOD_MAX_SOUNDS   320
#define ZOD_MAX_CHANNELS 16

/* Die beiden obersten Klangplaetze gehoeren der Musik: einer fuer das ganze
 * Stueck (auch der Schleifenklang), einer fuer den Einstieg nach einem Sprung.
 * Die Effekte zaehlen von 0 hoch und kommen dort nie an -- es sind 260. */
#define ZOD_MUSIC_GANZ   (ZOD_MAX_SOUNDS - 1)
#define ZOD_MUSIC_AB     (ZOD_MAX_SOUNDS - 2)

struct Library *AHIBase = 0;


/* Wird aus AHIs SoundFunc gerufen, also im INTERRUPT. */
static void (*stream_fertig)(void) = 0;
static struct Hook stream_hook;

extern "C" ULONG zod_audio_sound_hook(struct Hook *, struct AHIAudioCtrl *, APTR)
{
	if(stream_fertig) stream_fertig();

	return 0;
}

namespace
{

struct MsgPort *ahi_port = 0;
struct AHIRequest *ahi_request = 0;
struct AHIAudioCtrl *audio_ctrl = 0;
int device_open = 0;
int channel_count = 0;
int sound_count = 0;
int next_channel = 0;
unsigned int channel_end[ZOD_MAX_CHANNELS];   /* grobe Endzeit je Kanal */
unsigned int sound_ms[ZOD_MAX_SOUNDS];        /* Spieldauer je Klang     */

/* Musik */
unsigned char *music_data = 0;
unsigned int music_len = 0;
unsigned int music_rate = 11025;
unsigned int music_chan = 1;
int music_channel = -1;      /* fest reservierter Mischkanal */

/* 1 = Klaenge laufen ueber den eigenen Mischer, nicht ueber AHI. */
int mischerweg = 0;

/* Die Proben der Klaenge, wenn der Mischer sie spielt. AHI bekommt sie
 * dann gar nicht erst zu sehen. */
const unsigned char *snd_daten[ZOD_MAX_SOUNDS];
unsigned int snd_laenge[ZOD_MAX_SOUNDS];
unsigned int snd_rate[ZOD_MAX_SOUNDS];
int music_on = 0;

/* Vom Launcher ueber -A vorgegebener Modus; 0 heisst "wie bisher". */
ULONG wunsch_modus = 0;

int master_volume = 128;     /* Mix_Volume(-1, v) */

/* Lautstaerke der Engine (0..128) in AHIs Festkommawert wandeln, mit dem
 * Gesamtregler verrechnet. Mehr als voll laesst AHI nicht zu -- die Autodocs
 * nennen ausdruecklich "range is 0 to 0x10000". */
ULONG ahi_lautstaerke(int volume)
{
	long v;

	if(volume < 0) volume = 0;

	v = (long)volume * master_volume / 128;

	if(v > 128) v = 128;

	return (ULONG)((v * 0x10000L) / 128);
}

/* Systemzeit in Millisekunden. AHI meldet von sich aus nicht, wann ein Klang
 * zu Ende ist; deshalb merkt sich jeder Kanal seine Endzeit. */
unsigned int now_ms()
{
	struct timeval tv;

	GetSysTime(&tv);

	return (unsigned int)(tv.tv_secs * 1000 + tv.tv_micro / 1000);
}

/* Ein Anlaufversuch. Wird mehrfach mit verschiedenen Modi/Groessen gerufen. */
struct AHIAudioCtrl *alloc_audio(ULONG id, ULONG freq, ULONG chans, ULONG sounds)
{
	/* Der SoundFunc-Hook wird IMMER angemeldet, auch wenn gerade keine
	 * Musik laeuft: er ist nur bei AHI_AllocAudio zu setzen, und ein
	 * spaeteres Nachruesten ginge nur ueber ein Schliessen und
	 * Wiederoeffnen des ganzen Kontextes. Ohne angemeldeten Strom tut er
	 * nichts (stream_fertig ist dann null). */
	struct TagItem tags[] = {
		{ AHIA_AudioID,   id },
		{ AHIA_MixFreq,   freq },
		{ AHIA_Channels,  chans },
		{ AHIA_Sounds,    sounds },
		{ AHIA_SoundFunc, (ULONG)&stream_hook },
		{ TAG_DONE,       0 }
	};

	stream_hook.h_Entry    = (ULONG (*)())HookEntry;
	stream_hook.h_SubEntry = (ULONG (*)())zod_audio_sound_hook;
	stream_hook.h_Data     = 0;

	return AHI_AllocAudioA(tags);
}

} /* namespace */

int zod_audio_open(int channels, int mix_frequency)
{
	if(device_open) return 1;

	if(channels < 1) channels = 1;
	if(channels > ZOD_MAX_CHANNELS) channels = ZOD_MAX_CHANNELS;

	ahi_port = CreateMsgPort();
	if(!ahi_port)
	{
		ZLOG("AHI: kein MsgPort\n");
		return 0;
	}

	ahi_request = (struct AHIRequest *)CreateIORequest(ahi_port, sizeof(struct AHIRequest));
	if(!ahi_request)
	{
		ZLOG("AHI: kein IORequest\n");
		DeleteMsgPort(ahi_port);
		ahi_port = 0;
		return 0;
	}

	ahi_request->ahir_Version = 4;

	if(OpenDevice((CONST_STRPTR)AHINAME, AHI_NO_UNIT, (struct IORequest *)ahi_request, 0))
	{
		ZLOG("AHI: ahi.device nicht zu oeffnen -- Spiel laeuft ohne Ton\n");
		DeleteIORequest((struct IORequest *)ahi_request);
		DeleteMsgPort(ahi_port);
		ahi_request = 0;
		ahi_port = 0;
		return 0;
	}

	AHIBase = (struct Library *)ahi_request->ahir_Std.io_Device;
	device_open = 1;

	/* AHI_DEFAULT_ID liefert nur dann einen brauchbaren Modus, wenn die
	 * AHI-Voreinstellungen einmal gespeichert wurden. Auf einer frisch
	 * aufgesetzten V2 ist das oft nicht der Fall -- dann scheiterte
	 * AHI_AllocAudio und das Spiel blieb stumm. Deshalb der Reihe nach:
	 * Vorgabe, dann jeder vom System gemeldete Modus, dann weniger
	 * Kanaele/Klaenge (manche Treiber begrenzen beides). */
	ULONG want_freq = (ULONG)(mix_frequency > 0 ? mix_frequency : AHI_DEFAULT_FREQ);
	ULONG use_id    = wunsch_modus ? wunsch_modus : AHI_DEFAULT_ID;
	ULONG use_chans = (ULONG)channels;
	ULONG use_snd   = ZOD_MAX_SOUNDS;

	audio_ctrl = alloc_audio(use_id, want_freq, use_chans, use_snd);

	if(!audio_ctrl && wunsch_modus)
		ZLOG("AHI: gewuenschter Modus 0x%08lx traegt nicht -- suche weiter\n",
		     (unsigned long)wunsch_modus);

	if(!audio_ctrl)
	{
		ULONG id = AHI_NextAudioID(AHI_INVALID_ID);
		int guard = 0;

		while(id != (ULONG)AHI_INVALID_ID && guard++ < 64)
		{
			audio_ctrl = alloc_audio(id, want_freq, use_chans, use_snd);

			if(audio_ctrl)
			{
				use_id = id;
				break;
			}

			id = AHI_NextAudioID(id);
		}
	}

	while(!audio_ctrl && (use_chans > 1 || use_snd > 64))
	{
		if(use_chans > 1)   use_chans /= 2;
		else                use_snd = 64;

		audio_ctrl = alloc_audio(use_id, want_freq, use_chans, use_snd);

		if(!audio_ctrl && use_id != AHI_DEFAULT_ID)
			audio_ctrl = alloc_audio(AHI_DEFAULT_ID, want_freq, use_chans, use_snd);
	}

	if(!audio_ctrl)
	{
		ZLOG("AHI: AHI_AllocAudio fehlgeschlagen -- Spiel laeuft ohne Ton\n");
		zod_audio_close();
		return 0;
	}

	ZLOG("AHI: Modus 0x%08lx, %ld Kanaele angefordert, %ld Klangplaetze\n",
	     (unsigned long)use_id, (long)use_chans, (long)use_snd);

	channels = (int)use_chans;

	struct TagItem start_tags[] = { { AHIC_Play, TRUE }, { TAG_DONE, 0 } };

	if(AHI_ControlAudioA(audio_ctrl, start_tags))
	{
		ZLOG("AHI: AHI_ControlAudio(Play) fehlgeschlagen\n");
		zod_audio_close();
		return 0;
	}

	channel_count = channels;
	memset(channel_end, 0, sizeof(channel_end));

	/* Der letzte Kanal gehoert der Musik.
	 *
	 * Frueher stand hier "sofern mehr als einer da ist", sonst -1. Das war
	 * richtig, solange AHI auch die Effekte mischte -- bei einem einzigen
	 * Kanal gab es keinen zu entbehren. Seit der eigene Mischer alles
	 * zusammenfuehrt, wird GENAU EIN Kanal angefordert, und der ist der
	 * Musikkanal. Die alte Bedingung lieferte dafuer -1, und
	 * zod_audio_stream_open stieg wortlos aus:
	 *
	 *   AHI: 1 Kanaele, 11025 Hz
	 *   Musik: Ausgabe liess sich nicht oeffnen
	 *
	 * Fuer den alten Weg aendert sich nichts: dort werden 8 Kanaele
	 * angefordert, der Musikkanal ist wie bisher der siebte. */
	music_channel = (channel_count > 0) ? (channel_count - 1) : -1;

	ZLOG("AHI: %d Kanaele, %d Hz\n", channel_count, mix_frequency);

	return 1;
}

/* ---------------------------------------------------------------- Strom */
int zod_audio_stream_open(unsigned int rate, unsigned int rahmen,
                          void *puffer0, void *puffer1,
                          void (*fertig)(void))
{
	struct AHISampleInfo info;

	if(!audio_ctrl || music_channel < 0 || !puffer0 || !puffer1) return 0;

	/* Die beiden obersten Klangplaetze gehoeren der Musik -- die Effekte
	 * zaehlen von 0 hoch und kommen dort nie an. */
	void *p[2] = { puffer0, puffer1 };
	int platz[2] = { ZOD_MUSIC_GANZ, ZOD_MUSIC_AB };

	for(int i = 0; i < 2; i++)
	{
		info.ahisi_Type    = AHIST_S16S;      /* 16 Bit stereo */
		info.ahisi_Address = p[i];
		info.ahisi_Length  = rahmen;

		if(AHI_LoadSound((UWORD)platz[i], AHIST_SAMPLE, &info, audio_ctrl))
		{
			ZLOG("AHI: Musikpuffer %ld laesst sich nicht anmelden\n", (long)i);
			return 0;
		}
	}

	stream_fertig = fertig;

	struct TagItem spiel[] = {
		{ AHIP_BeginChannel, (ULONG)music_channel },
		{ AHIP_Freq,         (ULONG)rate },
		{ AHIP_Vol,          ahi_lautstaerke(128) },
		{ AHIP_Pan,          0x8000 },
		{ AHIP_Sound,        (ULONG)ZOD_MUSIC_GANZ },
		{ AHIP_LoopSound,    (ULONG)ZOD_MUSIC_AB },
		{ AHIP_EndChannel,   0 },
		{ TAG_DONE,          0 }
	};

	AHI_PlayA(audio_ctrl, spiel);

	ZLOG("AHI: Musikstrom auf Kanal %ld, %ld Hz, %ld Rahmen je Puffer\n",
	     (long)music_channel, (long)rate, (long)rahmen);

	return 1;
}

void zod_audio_stream_queue(int nr)
{
	if(!audio_ctrl || music_channel < 0 || !stream_fertig) return;

	struct TagItem t[] = {
		{ AHIP_BeginChannel, (ULONG)music_channel },
		{ AHIP_LoopSound,    (ULONG)(nr ? ZOD_MUSIC_AB : ZOD_MUSIC_GANZ) },
		{ AHIP_EndChannel,   0 },
		{ TAG_DONE,          0 }
	};

	AHI_PlayA(audio_ctrl, t);
}

void zod_audio_mischerweg(int an)
{
	mischerweg = an ? 1 : 0;

	ZLOG("Ton: Klangeffekte ueber %s\n",
	     mischerweg ? "den eigenen Mischer" : "AHI");
}

void zod_audio_stream_volume(int volume)
{
	if(!audio_ctrl || music_channel < 0) return;

	struct TagItem t[] = {
		{ AHIP_BeginChannel, (ULONG)music_channel },
		{ AHIP_Vol,          ahi_lautstaerke(volume) },
		{ AHIP_EndChannel,   0 },
		{ TAG_DONE,          0 }
	};

	AHI_PlayA(audio_ctrl, t);
}

void zod_audio_stream_close(void)
{
	stream_fertig = 0;

	if(!audio_ctrl || music_channel < 0) return;

	struct TagItem t[] = {
		{ AHIP_BeginChannel, (ULONG)music_channel },
		{ AHIP_Sound,        (ULONG)AHI_NOSOUND },
		{ AHIP_EndChannel,   0 },
		{ TAG_DONE,          0 }
	};

	AHI_PlayA(audio_ctrl, t);
}

void zod_audio_close(void)
{
	/* ZUERST die eigene Ausgabe abbauen, und zwar IMMER -- auch wenn sie
	 * gar nicht lief (musik_stop prueft das selbst).
	 *
	 * Vom Nutzer gemeldet: "nach dem Beenden des Spiels laeuft die Musik
	 * weiter". Das ist nicht nur laestig. Bei Paula haengt unser
	 * Interrupt-Handler ueber SetIntVector im System, und der Chip-Speicher
	 * der Puffer gehoert dem Programm -- nach dem Beenden zeigt der Vektor
	 * in freigegebenen Speicher, und die DMA liest aus fremdem Chip-RAM.
	 * Das ist ein Absturz, der irgendwann spaeter passiert.
	 *
	 * Die Reihenfolge ist zwingend: musik_stop gibt beim AHI-Weg den
	 * Strom ueber audio_ctrl zurueck, den diese Funktion gleich freigibt. */
	musik_stop();

	if(audio_ctrl)
	{
		struct TagItem stop_tags[] = { { AHIC_Play, FALSE }, { TAG_DONE, 0 } };

		AHI_ControlAudioA(audio_ctrl, stop_tags);
		AHI_FreeAudio(audio_ctrl);
		audio_ctrl = 0;
	}

	if(device_open)
	{
		CloseDevice((struct IORequest *)ahi_request);
		device_open = 0;
		AHIBase = 0;
	}

	if(ahi_request)
	{
		DeleteIORequest((struct IORequest *)ahi_request);
		ahi_request = 0;
	}

	if(ahi_port)
	{
		DeleteMsgPort(ahi_port);
		ahi_port = 0;
	}

	channel_count = 0;
	music_channel = -1;

	/* DIE KLANGLISTE BLEIBT, solange der eigene Mischer sie fuehrt.
	 *
	 * Die Proben gehoeren der Engine (Mix_Chunk), nicht AHI -- sie sind
	 * nach einem Schliessen und Wiederoeffnen unveraendert gueltig. Wuerde
	 * sound_count hier genullt, lehnte zod_audio_play danach JEDE Kennung
	 * ab (id >= sound_count), und das Spiel waere nach der Rueckkehr aus
	 * einer Videosequenz stumm -- ohne Meldung, denn abgelehnt wird
	 * wortlos. Die Engine meldet ihre Klaenge nur EINMAL an.
	 *
	 * Beim alten AHI-Weg ist es umgekehrt: dort liegen die Klaenge in
	 * AHIs Klangplaetzen, und AHI_FreeAudio wirft sie weg. */
	if(!mischerweg) sound_count = 0;
	music_on = 0;
	music_data = 0;
}






int zod_audio_available(void)
{
	return audio_ctrl ? 1 : 0;
}

void zod_audio_set_mode(unsigned long mode_id)
{
	wunsch_modus = (ULONG)mode_id;
}


void zod_audio_set_master(int volume)
{
	if(volume < 0) volume = 0;
	if(volume > 128) volume = 128;

	master_volume = volume;
}

int zod_audio_add_sound(unsigned char *data, unsigned int length, unsigned int rate)
{
	if(!data || !length) return -1;
	if(sound_count >= ZOD_MAX_SOUNDS) return -1;

	if(mischerweg)
	{
		/* Nur merken -- gemischt wird erst beim Abspielen. */
		snd_daten[sound_count]  = data;
		snd_laenge[sound_count] = length;
		snd_rate[sound_count]   = rate ? rate : 11025;
		sound_ms[sound_count]   = rate ? (length * 1000) / rate : 0;

		return sound_count++;
	}

	if(!audio_ctrl) return -1;

	struct AHISampleInfo info;

	info.ahisi_Type    = AHIST_M8S;      /* signed 8 Bit mono, wie im Archiv */
	info.ahisi_Address = data;
	info.ahisi_Length  = length;

	if(AHI_LoadSound((UWORD)sound_count, AHIST_SAMPLE, &info, audio_ctrl))
		return -1;

	/* Spieldauer merken, damit zod_audio_playing eine Aussage treffen kann */
	sound_ms[sound_count] = rate ? (length * 1000) / rate : 0;

	return sound_count++;
}

int zod_audio_play(int sound_id, int volume, int loop)
{
	if(sound_id < 0 || sound_id >= sound_count) return -1;

	if(mischerweg)
	{
		int v = (volume * master_volume) / 128;

		/* Panorama 64 = Mitte. Die Engine gibt keine Richtung mit; das
		 * waere eine Verhaltensaenderung und ist hier nicht gefragt. */
		return mischer_effekt((const BYTE *)snd_daten[sound_id],
		                      snd_laenge[sound_id], snd_rate[sound_id],
		                      v, 64, loop ? 1 : 0);
	}

	if(!audio_ctrl) return -1;

	/* Der Musikkanal ist ausgenommen -- sonst nimmt ihn der naechste Schuss. */
	int frei = channel_count - (music_channel >= 0 ? 1 : 0);
	int channel;

	if(frei < 1) frei = 1;

	channel = next_channel;
	next_channel = (next_channel + 1) % frei;

	if(volume < 0) volume = 0;
	if(volume > 128) volume = 128;

	struct TagItem tags[] = {
		{ AHIP_BeginChannel, (ULONG)channel },
		{ AHIP_Freq,         (ULONG)11025 },
		{ AHIP_Vol,          ahi_lautstaerke(volume) },
		{ AHIP_Pan,          0x8000 },
		{ AHIP_Sound,        (ULONG)sound_id },
		{ AHIP_LoopSound,    (ULONG)(loop ? sound_id : AHI_NOSOUND) },
		{ AHIP_EndChannel,   0 },
		{ TAG_DONE,          0 }
	};

	AHI_PlayA(audio_ctrl, tags);

	/* Endzeit vormerken; Endlosklaenge laufen bis zum ausdruecklichen Stopp */
	channel_end[channel] = loop ? 0xFFFFFFFFu : now_ms() + sound_ms[sound_id] + 1;

	return channel;
}

void zod_audio_stop(int channel)
{
	if(mischerweg) { mischer_effekt_stop(channel); return; }

	if(!audio_ctrl || channel < 0 || channel >= channel_count) return;

	struct TagItem tags[] = {
		{ AHIP_BeginChannel, (ULONG)channel },
		{ AHIP_Sound,        AHI_NOSOUND },
		{ AHIP_EndChannel,   0 },
		{ TAG_DONE,          0 }
	};

	AHI_PlayA(audio_ctrl, tags);
	channel_end[channel] = 0;
}

int zod_audio_playing(int channel)
{
	if(mischerweg) return mischer_effekt_laeuft(channel);

	if(!audio_ctrl || channel < 0 || channel >= channel_count) return 0;
	if(!channel_end[channel]) return 0;
	if(channel_end[channel] == 0xFFFFFFFFu) return 1;    /* Endlosklang */

	if(now_ms() >= channel_end[channel])
	{
		channel_end[channel] = 0;
		return 0;
	}

	return 1;
}

void zod_audio_set_volume(int channel, int volume)
{
	if(mischerweg) { mischer_effekt_vol(channel, volume); return; }

	if(!audio_ctrl || channel < 0 || channel >= channel_count) return;

	if(volume < 0) volume = 0;
	if(volume > 128) volume = 128;

	struct TagItem tags[] = {
		{ AHIP_BeginChannel, (ULONG)channel },
		{ AHIP_Vol,          (ULONG)((volume * 0x10000) / 128) },
		{ AHIP_EndChannel,   0 },
		{ TAG_DONE,          0 }
	};

	AHI_PlayA(audio_ctrl, tags);
}

#else /* andere Plattformen */

int zod_audio_stream_open(unsigned int, unsigned int, void *, void *, void (*)(void)) { return 0; }
void zod_audio_stream_queue(int) { }
void zod_audio_stream_volume(int) { }
void zod_audio_mischerweg(int) { }
void zod_audio_stream_close(void) { }
int zod_audio_open(int, int) { return 0; }
void zod_audio_set_mode(unsigned long) { }
void zod_audio_set_master(int) { }
void zod_audio_close(void) {}
int zod_audio_available(void) { return 0; }
int zod_audio_add_sound(unsigned char *, unsigned int, unsigned int) { return -1; }
int zod_audio_play(int, int, int) { return -1; }
void zod_audio_stop(int) {}
int zod_audio_playing(int) { return 0; }
void zod_audio_set_volume(int, int) {}

#endif
