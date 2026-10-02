#ifndef ZOD_AMIGA_SYS_SOCKET_H
#define ZOD_AMIGA_SYS_SOCKET_H
/*
 * BSD-Socket-Schnittstelle fuer AmigaOS.
 *
 * Die -noixemul-Umgebung der Toolchain hat keine Socket-Header (die liegen nur
 * unter clib2/ixemul). Diese Header erklaeren genau die Teilmenge, die die
 * Engine benutzt; die Implementierung steht in port/amiga/net_bsdsocket.cpp:
 *  - P3: Attrappen (Singleplayer laeuft ueber den Loopback ohne Sockets)
 *  - P6: echte Aufrufe an bsdsocket.library (Roadshow/AmiTCP/Emulation)
 */
#include <sys/types.h>
#include <sys/time.h>
/* fd_set, die FD_-Makros und select() kommen aus der Toolchain */
#include <sys/select.h>

#ifdef __cplusplus
extern "C" {
#endif

/* u_long kommt aus <sys/types.h>, socklen_t fehlt dort */
typedef unsigned int socklen_t;

#define AF_INET       2
#define PF_INET       AF_INET
#define SOCK_STREAM   1
#define SOCK_DGRAM    2
#define IPPROTO_TCP   6

#define SOL_SOCKET    0xffff
#define SO_REUSEADDR  0x0004
#define SO_SNDBUF     0x1001

#define MSG_PEEK      0x2

#define INADDR_ANY    ((unsigned long)0x00000000)

struct in_addr
{
	unsigned long s_addr;
};

struct sockaddr
{
	unsigned char sa_len;
	unsigned char sa_family;
	char sa_data[14];
};

struct sockaddr_in
{
	unsigned char sin_len;
	unsigned char sin_family;
	unsigned short sin_port;
	struct in_addr sin_addr;
	char sin_zero[8];
};

int socket(int domain, int type, int protocol);
int connect(int s, const struct sockaddr *name, socklen_t namelen);
int bind(int s, const struct sockaddr *name, socklen_t namelen);
int listen(int s, int backlog);
int accept(int s, struct sockaddr *addr, socklen_t *addrlen);
int recv(int s, void *buf, int len, int flags);
int send(int s, const void *buf, int len, int flags);
int setsockopt(int s, int level, int optname, const void *optval, socklen_t optlen);
int getsockopt(int s, int level, int optname, void *optval, socklen_t *optlen);
/* select() ist bereits in <sys/select.h> deklariert (mit __stdargs) */

unsigned short htons(unsigned short v);
unsigned short ntohs(unsigned short v);
unsigned long htonl(unsigned long v);
unsigned long ntohl(unsigned long v);

#ifdef __cplusplus
}
#endif

#endif
