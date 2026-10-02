#ifndef SOCKETHANDLER_H
#define SOCKETHANDLER_H

#include <QtGlobal>
#include "qzod_dnclientserver_global.h"

#ifdef Q_OS_WIN //if windows
#include <windows.h>		//win for Sleep(1000)
#include <direct.h>		//win
#include <Iphlpapi.h>
#endif
#ifdef Q_OS_UNIX
#include <sys/types.h>		//lin
#include <sys/socket.h>		//lin
#include <netinet/in.h>		//lin
#include <netdb.h>		//lin
#include <sys/stat.h>		//lin
#include <sys/ioctl.h>		//lin
#include <arpa/inet.h>
#include <net/if.h>
#include <unistd.h>
#endif


//receive buffer per connection. packets are capped at MAX_BUF_SIZE by
//GetPacket/PacketAvailable, so 64k is plenty (was 400000, which cost 800k
//per connection and was far too much for amiga memory).
#define MAX_DATA_STORED 65536
#define MAX_BUF_SIZE 20000

//outgoing queue: grows on demand, connection is dropped beyond the cap
#define SEND_QUEUE_START 16384
#define SEND_QUEUE_MAX 1048576

class QZOD_DNCLIENTSERVERSHARED_EXPORT SocketHandler
{
	public:
		SocketHandler();
		SocketHandler(int s, struct sockaddr_in s_in);
		~SocketHandler();
		int Init(int s, struct sockaddr_in s_in);
		//Loopback: zwei Handler im selben Prozess, Zustellung direkt in den
		//Empfangspuffer der Gegenstelle. Kein Socket, kein Byte-Tausch.
		int InitLoopback(SocketHandler *peer_);
		int IsLoopback() const { return loopback; }
		int DeliverFromPeer(const char *data, int size);
		//Loopback-Gegenstueck zu FlushSendQueue: so viel an die Gegenstelle
		//abgeben, wie deren Empfangspuffer gerade fasst.
		int FlushLoopback();
		//push queued bytes out; call regularly, send() on a non blocking
		//socket accepts partial writes
		int FlushSendQueue();
		int Connected();
		int Disconnect();
		int DoRecv();
		int DoProcess(char **message, int *size, int *pack_id);
		int DoFastProcess(char **message, int *size, int *pack_id);
		void ResetFastProcess();
		int PacketAvailable();
		int GetPacket(char **message, int *size, int *pack_id);
		int SendMessage(int pack_id, const char *data, int size);
		int SendMessageAscii(int pack_id, const char *data);
		static char *GetMAC(char *buf);
	private:
		int recv_good(int rcv_amt);
		int pause_for_send();
		int socket_good_to_send(bool &kill_me);
		int QueueBytes(const char *data, int size);
		
		int s;
		struct sockaddr_in s_in;
		char ipaddress[50];
		int connected;
		int fp_ptr;
		
		char buf[MAX_DATA_STORED];
		int buf_size;

		//for the do process func
		char dp_temp_buf[MAX_DATA_STORED];

		//scratch for the little endian conversion of an outgoing packet
		char wire_buf[MAX_BUF_SIZE];

		//outgoing queue (heap, grows on demand)
		int loopback;
		SocketHandler *peer;

		char *send_q;
		int send_q_cap;
		int send_q_len;
};

#endif
