#ifndef ZOD_AMIGA_ARPA_INET_H
#define ZOD_AMIGA_ARPA_INET_H
#include <sys/socket.h>

#ifdef __cplusplus
extern "C" {
#endif

char *inet_ntoa(struct in_addr addr);
unsigned long inet_addr(const char *cp);

#ifdef __cplusplus
}
#endif

#endif
