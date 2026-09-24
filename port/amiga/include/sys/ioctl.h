#ifndef ZOD_AMIGA_SYS_IOCTL_H
#define ZOD_AMIGA_SYS_IOCTL_H
#include <sys/socket.h>

#ifdef __cplusplus
extern "C" {
#endif

/* nur der nicht blockierende Modus wird gebraucht */
#define FIONBIO 0x8004667e

int ioctl(int s, unsigned long request, void *arg);

#ifdef __cplusplus
}
#endif

#endif
