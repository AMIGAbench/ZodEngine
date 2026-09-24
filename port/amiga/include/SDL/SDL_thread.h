#ifndef ZOD_AMIGA_SDL_THREAD_H
#define ZOD_AMIGA_SDL_THREAD_H
/*
 * Attrappen fuer Faeden und Sperren.
 *
 * Der Port laeuft seit P2 in EINEM Task: die Pfadsuche ist eine Auftrags-
 * schlange im Servertakt, das Laden ist synchron. Erhoben am 18.09.: kein
 * einziger lebender SDL_CreateThread-Aufruf, die Mutex-Funktionen liegen nur
 * noch als Reste in zpath_finding_old, zcore und qzod_map.
 *
 * Deshalb: Sperren sind leere Huellen, SDL_WaitThread kehrt sofort zurueck.
 * Kaeme je wieder ein zweiter Task ins Spiel, muesste das hier echte
 * exec-Semaphore werden -- der Kommentar steht bewusst hier.
 */
#include <SDL/SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SDL_Thread SDL_Thread;
typedef struct SDL_mutex  SDL_mutex;

SDL_mutex *SDL_CreateMutex(void);
void       SDL_DestroyMutex(SDL_mutex *mutex);
int        SDL_mutexP(SDL_mutex *mutex);      /* Sperren   */
int        SDL_mutexV(SDL_mutex *mutex);      /* Freigeben */

#define SDL_LockMutex(m)   SDL_mutexP(m)
#define SDL_UnlockMutex(m) SDL_mutexV(m)

SDL_Thread *SDL_CreateThread(int (*fn)(void *), void *data);
void        SDL_WaitThread(SDL_Thread *thread, int *status);
void        SDL_KillThread(SDL_Thread *thread);
Uint32      SDL_ThreadID(void);

#ifdef __cplusplus
}
#endif

#endif
