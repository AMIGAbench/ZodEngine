/*
 * Netzwerk auf dem Amiga.
 *
 * P3 (Bring-up): Attrappen. Der Singleplayer laeuft ueber den Loopback im
 * eigenen Prozess und braucht keine Sockets; echtes Netzwerkspiel meldet
 * hier sauber einen Fehler statt abzustuerzen.
 * P6: Diese Funktionen rufen bsdsocket.library (SocketBase pro Task,
 * IoctlSocket, WaitSelect, CloseSocket).
 */
#include <stdio.h>
#include <string.h>

#include <sys/socket.h>
#include <sys/select.h>
#include <sys/ioctl.h>
#include <netdb.h>
#include <arpa/inet.h>

#include "zod_log.h"

static int warned = 0;

static int no_net(const char *what)
{
	if(!warned)
	{
		ZLOG("Netzwerk: %s -- in dieser Fassung ist nur Singleplayer moeglich\n", what);
		warned = 1;
	}

	return -1;
}

extern "C" {

int socket(int domain, int type, int protocol)
{
	(void)domain; (void)type; (void)protocol;
	return no_net("socket()");
}

int connect(int s, const struct sockaddr *name, socklen_t namelen)
{
	(void)s; (void)name; (void)namelen;
	return no_net("connect()");
}

int bind(int s, const struct sockaddr *name, socklen_t namelen)
{
	(void)s; (void)name; (void)namelen;
	return no_net("bind()");
}

int listen(int s, int backlog)
{
	(void)s; (void)backlog;
	return no_net("listen()");
}

int accept(int s, struct sockaddr *addr, socklen_t *addrlen)
{
	(void)s; (void)addr; (void)addrlen;
	return -1;
}

int recv(int s, void *buf, int len, int flags)
{
	(void)s; (void)buf; (void)len; (void)flags;
	return -1;
}

int send(int s, const void *buf, int len, int flags)
{
	(void)s; (void)buf; (void)len; (void)flags;
	return -1;
}

int setsockopt(int s, int level, int optname, const void *optval, socklen_t optlen)
{
	(void)s; (void)level; (void)optname; (void)optval; (void)optlen;
	return -1;
}

int getsockopt(int s, int level, int optname, void *optval, socklen_t *optlen)
{
	(void)s; (void)level; (void)optname; (void)optval; (void)optlen;
	return -1;
}

int select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout)
{
	(void)nfds; (void)readfds; (void)writefds; (void)exceptfds; (void)timeout;
	return 0;
}

int ioctl(int s, unsigned long request, void *arg)
{
	(void)s; (void)request; (void)arg;
	return -1;
}

struct hostent *gethostbyname(const char *name)
{
	(void)name;
	no_net("gethostbyname()");
	return 0;
}

char *inet_ntoa(struct in_addr addr)
{
	static char buf[16];
	unsigned long v = addr.s_addr;

	/* Adresse liegt in Netzreihenfolge vor */
	snprintf(buf, sizeof(buf), "%lu.%lu.%lu.%lu",
	         (v >> 24) & 0xff, (v >> 16) & 0xff, (v >> 8) & 0xff, v & 0xff);

	return buf;
}

unsigned long inet_addr(const char *cp)
{
	(void)cp;
	return 0xffffffffUL;
}

/* m68k ist big endian: Netzreihenfolge ist die Hostreihenfolge */
unsigned short htons(unsigned short v) { return v; }
unsigned short ntohs(unsigned short v) { return v; }
unsigned long htonl(unsigned long v) { return v; }
unsigned long ntohl(unsigned long v) { return v; }

} /* extern "C" */
