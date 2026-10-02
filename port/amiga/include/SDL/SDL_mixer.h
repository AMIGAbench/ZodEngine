#ifndef ZOD_AMIGA_SDL_MIXER_H
#define ZOD_AMIGA_SDL_MIXER_H
/*
 * Ersatz fuer SDL_mixer auf dem Amiga.
 *
 * Erste Stufe (P3): stumm, damit der Bring-up ohne Tonausgabe laeuft.
 * Zweite Stufe (P5): AHI (8-Bit-Samples aus den Asset-Paketen).
 * Implementierung: port/amiga/sdl_addons.cpp
 */
#include <SDL/SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MIX_MAX_VOLUME 128
#define MIX_INIT_MOD   0x00000004
#define MIX_INIT_OGG   0x00000010

typedef struct Mix_Chunk
{
	int allocated;
	Uint8 *abuf;
	Uint32 alen;
	Uint8 volume;      /* 0 .. MIX_MAX_VOLUME, wird von der Engine gesetzt */
} Mix_Chunk;

typedef struct _Mix_Music Mix_Music;

int Mix_Init(int flags);
void Mix_Quit(void);
int Mix_OpenAudio(int frequency, Uint16 format, int channels, int chunksize);
void Mix_CloseAudio(void);
int Mix_QuerySpec(int *frequency, Uint16 *format, int *channels);
int Mix_AllocateChannels(int numchans);

Mix_Chunk *Mix_LoadWAV(const char *file);
void Mix_FreeChunk(Mix_Chunk *chunk);
Mix_Music *Mix_LoadMUS(const char *file);
void Mix_FreeMusic(Mix_Music *music);

int Mix_PlayChannel(int channel, Mix_Chunk *chunk, int loops);
int Mix_HaltChannel(int channel);
int Mix_Playing(int channel);
int Mix_Volume(int channel, int volume);

int Mix_PlayMusic(Mix_Music *music, int loops);
int Mix_VolumeMusic(int volume);
int Mix_PlayingMusic(void);
int Mix_HaltMusic(void);
int Mix_SetMusicPosition(double position);

const char *Mix_GetError(void);

#ifdef __cplusplus
}
#endif

#endif
