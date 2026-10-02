#include "socket_handler.h"
#include "zod_wire.h"

#ifdef ZOD_PACKET_LOG
#include <cstdio>
#include <cstdlib>

//packet log for protocol goldens: active when the environment variable
//ZOD_PACKET_LOG names a file. one line per packet, payload as hex.
static FILE *packet_log_file()
{
	static FILE *fp = nullptr;
	static bool tried = false;

	if(!tried)
	{
		const char *name = getenv("ZOD_PACKET_LOG");

		tried = true;
		if(name && *name) fp = fopen(name, "w");
	}

	return fp;
}

static void packet_log(int s, char dir, int pack_id, const char *data, int size)
{
	FILE *fp = packet_log_file();

	if(!fp) return;

#if defined(Q_OS_UNIX) && !defined(__amigaos__)
	flockfile(fp);
#endif
	fprintf(fp, "%.3f fd=%d %c id=%d size=%d ", current_time(), s, dir, pack_id, size);
	for(int i=0;i<size;i++) fprintf(fp, "%02x", (unsigned char)data[i]);
	fputc('\n', fp);
	fflush(fp);
#if defined(Q_OS_UNIX) && !defined(__amigaos__)
	funlockfile(fp);
#endif
}
#endif

SocketHandler::SocketHandler()
{
	connected = 0;
	fp_ptr = 0;
	loopback = 0;
	peer = nullptr;

	send_q = nullptr;
	send_q_cap = 0;
	send_q_len = 0;
}

SocketHandler::SocketHandler(int s, struct sockaddr_in s_in)
{
	loopback = 0;
	peer = nullptr;
	send_q = nullptr;
	send_q_cap = 0;
	send_q_len = 0;

	Init(s, s_in);
}

SocketHandler::~SocketHandler()
{
	if(send_q) free(send_q);
	send_q = nullptr;
}

int SocketHandler::Init(int s_, struct sockaddr_in s_in_)
{
	u_long on_mode = 1;
	
	s = s_;
	s_in = s_in_;

	buf_size = 0;

	fp_ptr = 0;

	send_q_len = 0;
	
	connected = 1;
	
	strcpy(ipaddress,(char*)inet_ntoa(s_in.sin_addr));
	
#ifdef Q_OS_WIN
	ioctlsocket(s, FIONBIO, &on_mode);
#else		
	ioctl(s, FIONBIO, &on_mode);
#endif
	
	ZLOG("socket connected:%s\n", ipaddress);
	
	return 1;
}

int SocketHandler::InitLoopback(SocketHandler *peer_)
{
	s = -1;
	loopback = 1;
	peer = peer_;

	buf_size = 0;
	fp_ptr = 0;
	send_q_len = 0;
	connected = 1;

	strcpy(ipaddress, "loopback");

	return 1;
}

//Paket der Gegenstelle in den eigenen Empfangspuffer legen
int SocketHandler::DeliverFromPeer(const char *data, int size)
{
	if(!connected) return 0;

	if(buf_size + size > MAX_DATA_STORED)
	{
		ZLOG("SocketHandler::DeliverFromPeer:loopback buffer full\n");
		return 0;
	}

	memcpy(buf + buf_size, data, size);
	buf_size += size;

	return 1;
}

/* LOOPBACK HATTE KEINE FLUSSKONTROLLE -- das war der Grund, warum Karten
 * ueber rund 64 KB den Klienten nie erreichten.
 *
 * Die Karte wird laengst in Stuecken verschickt: ZServer::request_map_event
 * zerlegt sie in 4-KB-Bloecke, 180 KB sind also rund 44 Nachrichten. Kein
 * einziges Paket ist zu gross. Der Fehler lag daneben:
 *
 *   echter TCP-Weg: QueueBytes -> Sendeschlange (waechst, bis 1 MB), und
 *                   FlushSendQueue schiebt ueber viele Takte nach
 *   Loopback bisher: DeliverFromPeer sofort in den Puffer der Gegenstelle,
 *                   und ist der voll, `return 0` -- was der Aufrufer NICHT
 *                   auswertet
 *
 * request_map_event laeuft in EINEM Aufruf durch. Alle 44 Nachrichten
 * gingen also hintereinander hinueber, ohne dass der Empfaenger dazwischen
 * leeren konnte; nach 65536/4108 rund 16 Bloecken fielen die restlichen 28
 * LAUTLOS weg. Belegt: mit p08_bb_p08m01.map (179 710 Byte) meldet der Lauf
 * 58x "loopback buffer full" und NULL mal map_data_size.
 *
 * Jetzt nimmt auch der Loopback denselben Weg wie TCP: erst in die eigene
 * Schlange, dann so viel abgeben, wie drueben Platz ist. Die Rahmen sind
 * ein reiner Bytestrom (8 Byte Kopf + Nutzlast), der Empfaenger wartet in
 * DoProcess ohnehin auf einen vollstaendigen Rahmen -- eine TEILweise
 * Abgabe ist deshalb unbedenklich.
 *
 * Am Normalfall aendert sich nichts: ist drueben Platz, wird in SendMessage
 * sofort geleert, also genau wie vorher. Nur wenn es eng wird, wird jetzt
 * verschoben statt verworfen. */
int SocketHandler::FlushLoopback()
{
	int frei, nimm;

	if(!connected || !send_q_len) return 1;

	if(!peer || !peer->Connected())
	{
		Disconnect();
		return 0;
	}

	frei = MAX_DATA_STORED - peer->buf_size;

	if(frei <= 0) return 1;

	nimm = (send_q_len < frei) ? send_q_len : frei;

	memcpy(peer->buf + peer->buf_size, send_q, nimm);
	peer->buf_size += nimm;

	send_q_len -= nimm;

	if(send_q_len) memmove(send_q, send_q + nimm, send_q_len);

	return 1;
}

int SocketHandler::Connected()
{
	return connected;
}

int SocketHandler::Disconnect()
{
	if(connected && loopback)
	{
		//Gegenstelle loesen, es gibt keinen Socket zu schliessen
		if(peer && peer->peer == this) peer->peer = nullptr;
		peer = nullptr;
		connected = 0;
		ipaddress[0] = 0;

		return 1;
	}

	if(connected)
	{
		//first close
#ifdef Q_OS_WIN
		closesocket(s);
#else
		close(s);
#endif
		
		//set stuff...
		connected = 0;
		ZLOG("socket disconnected: %s\n", ipaddress);
		
		//clear out
		ipaddress[0] = 0;
	}

	return 1;
}

int SocketHandler::recv_good(int rcv_amt)
{
#ifdef Q_OS_WIN
	int terr = WSAGetLastError();
	if(terr == 10054 || (!terr && !rcv_amt))
#else
	if(!rcv_amt)
#endif
	{
		Disconnect();
		return 0;
	}
	else if(rcv_amt == -1)
	{
		//simply no packet yet
		return 0;
	}

	return 1;
}

int SocketHandler::PacketAvailable()
{
	int rcv_amt;
	char temp_buf[MAX_BUF_SIZE];
	int pack_size;
	
	rcv_amt = recv(s, temp_buf, sizeof(int), MSG_PEEK);

	if(!recv_good(rcv_amt)) return 0;

	//get pack size
	if(rcv_amt != sizeof(int))
		return 0;
	else
		pack_size = (int)zod_rd_le32(temp_buf) + 8;

	//bad packet so just disconnect?
	if(pack_size < 8 || pack_size > MAX_BUF_SIZE)
	{
		Disconnect();
		return 0;
	}

	//check if there is the right amount of data on the buffer
	//there has to be a faster way...
	rcv_amt = recv(s, temp_buf, pack_size, MSG_PEEK);

	//if(!recv_good(rcv_amt)) return 0;

	return rcv_amt == pack_size;
}

int SocketHandler::GetPacket(char **message, int *size, int *pack_id)
{
	int rcv_amt;
	//char temp_buf[MAX_BUF_SIZE];
	char *temp_buf = dp_temp_buf;
	int pack_size;
	
	rcv_amt = recv(s, temp_buf, sizeof(int), MSG_PEEK);

	if(!recv_good(rcv_amt)) return 0;

	//get pack size
	if(rcv_amt != sizeof(int))
		return 0;
	else
		pack_size = (int)zod_rd_le32(temp_buf) + 8;

	//bad packet so just disconnect?
	if(pack_size < 8 || pack_size > MAX_BUF_SIZE)
	{
		Disconnect();
		return 0;
	}

	rcv_amt = recv(s, temp_buf, pack_size, 0);

	if(!recv_good(rcv_amt)) return 0;

	if(rcv_amt != pack_size) 
	{
		ZLOG("SocketHandler::GetPacket:rcv_amt != pack_size\n");
		return 0;
	}

	*size = (int)zod_rd_le32(temp_buf);
	*pack_id = (int)zod_rd_le32(temp_buf + 4);
	*message = temp_buf + 8;

	if(*size < 0) return 0;

	//wire is little endian -> bring the payload into host order
	if(!loopback) zod_wire_swap_payload(*pack_id, *message, *size, 0);

#ifdef ZOD_PACKET_LOG
	packet_log(s, 'R', *pack_id, *message, *size);
#endif

	return 1;
}

int SocketHandler::DoRecv()
{
	int rcv_amt;
	char temp_buf[MAX_BUF_SIZE];

	//im Loopback liegen die Daten schon im Puffer -- vorher aber noch
	//nachschieben, was beim Senden nicht mehr hineinpasste.
	if(loopback)
	{
		FlushLoopback();

		return buf_size > 0;
	}

	//good place to push out what is still queued
	FlushSendQueue();

	rcv_amt = recv(s, temp_buf, MAX_BUF_SIZE, 0);

	if(!recv_good(rcv_amt)) return 0;

//#ifdef _WIN32
//	int terr = WSAGetLastError();
//	if(terr == 10054 || (!terr && !rcv_amt))
//#else
//		if(!rcv_amt)
//#endif
//	{
//		Disconnect();
//		return 0;
//	}
//	else if(rcv_amt == -1)
//	{
//		//simply no packet yet
//		return 0;
//	}
	
	//process that monkey yo!
	//should we clear?
	if(rcv_amt + buf_size > MAX_DATA_STORED) 
	{
		ZLOG("WARNING: recv tossed because of oversize\n");
		buf_size = 0;
		return 0;
	}
	
	//fill it
	memcpy(buf + buf_size, temp_buf, rcv_amt);
	buf_size += rcv_amt;
	
	return 1;
}

int SocketHandler::DoProcess(char **message, int *size, int *pack_id)
{
	//char temp_buf[MAX_DATA_STORED];
	char *temp_buf = dp_temp_buf;
	int packet_size;
	
	//dont even have the indentifier?
	if(buf_size < sizeof(int) + sizeof(int)) return 0;
	*size = (int)zod_rd_le32(buf);
	*pack_id = (int)zod_rd_le32(buf + 4);
	packet_size = *size + 8;
	
// 	print_dump(buf, 20, "rcv ");
// 	ZLOG("rcv buf_size:%d packet_size:%d\n", buf_size, packet_size);
	
	if(*size < 0)
	{
		//whoops garbage
		ZLOG("invalid packet size:%d\n", *size);
		buf_size = 0;
		return 0;
	}
	
	//all the packet there?
	if(packet_size > buf_size) return 0;
	
	memcpy(temp_buf, buf + 8, *size);
	*message = temp_buf;

	if(!loopback) zod_wire_swap_payload(*pack_id, *message, *size, 0);

#ifdef ZOD_PACKET_LOG
	packet_log(s, 'R', *pack_id, *message, *size);
#endif
	
	//push back stuff at the end?
	if(packet_size < buf_size) memcpy(buf, buf + packet_size, buf_size - packet_size);
	
	//shrink buf_size
	buf_size -= packet_size;
	
	return 1;
}

int SocketHandler::DoFastProcess(char **message, int *size, int *pack_id)
{
	char *fp_buf = buf + fp_ptr;
	int fp_buf_size = buf_size - fp_ptr;
	int packet_size;

	//dont even have the indentifier?
	if(fp_buf_size < sizeof(int) + sizeof(int)) return 0;
	*size = (int)zod_rd_le32(fp_buf);
	*pack_id = (int)zod_rd_le32(fp_buf + 4);
	*message = fp_buf + 8;
	packet_size = *size + 8;

	if(*size < 0)
	{
		//whoops garbage
		ZLOG("invalid packet size:%d\n", *size);
		buf_size = 0;
		return 0;
	}

	//all the packet there?
	if(packet_size > fp_buf_size) return 0;

	//move fp_ptr forward
	fp_ptr += packet_size;

	if(!loopback) zod_wire_swap_payload(*pack_id, *message, *size, 0);

#ifdef ZOD_PACKET_LOG
	packet_log(s, 'R', *pack_id, *message, *size);
#endif

	//return good
	return 1;
}

void SocketHandler::ResetFastProcess()
{
	if(!fp_ptr) return;

	char *fp_buf = buf + fp_ptr;
	int fp_buf_size = buf_size - fp_ptr;

	//if some remaining then push it back to the front
	if(fp_buf_size) memcpy(buf, fp_buf, fp_buf_size);

	//set real size to the fp size
	buf_size = fp_buf_size;

	//reset ptr
	fp_ptr = 0;
}

//grow the queue if needed and append. returns 0 when the cap is hit.
int SocketHandler::QueueBytes(const char *data, int size)
{
	if(size <= 0) return 1;

	if(send_q_len + size > send_q_cap)
	{
		int new_cap = send_q_cap ? send_q_cap : SEND_QUEUE_START;
		char *new_q;

		while(new_cap < send_q_len + size) new_cap *= 2;

		if(new_cap > SEND_QUEUE_MAX)
		{
			ZLOG("SocketHandler::QueueBytes:send queue overflow (%d bytes)\n", send_q_len + size);
			return 0;
		}

		new_q = (char*)realloc(send_q, new_cap);
		if(!new_q)
		{
			ZLOG("SocketHandler::QueueBytes:out of memory for send queue\n");
			return 0;
		}

		send_q = new_q;
		send_q_cap = new_cap;
	}

	memcpy(send_q + send_q_len, data, size);
	send_q_len += size;

	return 1;
}

//send as much of the queue as the socket takes. a non blocking socket may
//accept only part of it, the rest stays queued for the next call.
int SocketHandler::FlushSendQueue()
{
	int sent;

	if(!connected || !send_q_len) return 1;

	while(send_q_len > 0)
	{
#ifdef MSG_NOSIGNAL
		sent = send(s, send_q, send_q_len, MSG_NOSIGNAL);
#else
		sent = send(s, send_q, send_q_len, 0);
#endif

		if(sent > 0)
		{
			send_q_len -= sent;
			if(send_q_len) memmove(send_q, send_q + sent, send_q_len);
			continue;
		}

		//socket full for now -> keep the rest queued
#ifdef Q_OS_WIN
		if(sent < 0 && WSAGetLastError() == WSAEWOULDBLOCK) return 1;
#else
		if(sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) return 1;
#endif

		Disconnect();
		return 0;
	}

	return 1;
}

int SocketHandler::SendMessage(int pack_id, const char *data, int size)
{
	char header[8];

	if(!connected) return 0;

	//Loopback: Rahmen direkt bei der Gegenstelle abliefern, ohne Byte-Tausch
	if(loopback)
	{
		if(!peer || !peer->Connected())
		{
			Disconnect();
			return 0;
		}

		zod_wr_le32(header, (unsigned int)size);
		zod_wr_le32(header + 4, (unsigned int)pack_id);

#ifdef ZOD_PACKET_LOG
		packet_log(s, 'S', pack_id, data, size);
#endif

		/* ERST in die eigene Schlange, DANN abgeben. Die Reihenfolge ist
		 * der Punkt: gaebe man neue Bytes direkt ab, waehrend noch welche
		 * in der Schlange liegen, ueberholten sie diese und der Bytestrom
		 * waere zerrissen. */
		if(!QueueBytes(header, 8))
		{
			Disconnect();
			return 0;
		}

		if(size && !QueueBytes(data, size))
		{
			Disconnect();
			return 0;
		}

		FlushLoopback();

		return 1;
	}

	if(size < 0 || size > MAX_BUF_SIZE)
	{
		ZLOG("SocketHandler::SendMessage:bad packet size %d (id %d)\n", size, pack_id);
		return 0;
	}

	//pack header, then payload; queueing keeps partial sends from
	//corrupting the stream. header and payload go out little endian.
	zod_wr_le32(header, (unsigned int)size);
	zod_wr_le32(header + 4, (unsigned int)pack_id);

	if(!QueueBytes(header, 8))
	{
		Disconnect();
		return 0;
	}

	if(size)
	{
		const char *payload = data;

		if(zod_wire_swaps())
		{
			memcpy(wire_buf, data, size);
			zod_wire_swap_payload(pack_id, wire_buf, size, 1);
			payload = wire_buf;
		}

		if(!QueueBytes(payload, size))
		{
			Disconnect();
			return 0;
		}
	}

#ifdef ZOD_PACKET_LOG
	packet_log(s, 'S', pack_id, data, size);
#endif

	return FlushSendQueue();
}

int SocketHandler::SendMessageAscii(int pack_id, const char *data)
{
	return SendMessage(pack_id, data, strlen(data) + 1);
}

int SocketHandler::socket_good_to_send(bool &kill_me)
{
	fd_set write_flags;
	fd_set error_flags;
	int the_stat;
	struct timeval waitd;
	
	waitd.tv_sec = 0;
	waitd.tv_usec = 0;
	
	FD_ZERO(&write_flags);
	FD_ZERO(&error_flags);
	FD_SET(s, &write_flags);
	FD_SET(s, &error_flags);
	
	the_stat = select(s + 1, (fd_set*)0, &write_flags, &error_flags, &waitd);
	
	if(the_stat < 0)
	{
#ifdef Q_OS_WIN
		ZLOG("select errno:%d\n",errno);
#else
		ZLOG("select errno:%d-%s\n",errno, strerror(errno));
#endif
		switch(errno)
		{
		case 9: //bad file descriptor
			kill_me = true;
			break;
		}
		return 0;
	}
	
	if(FD_ISSET(s, &error_flags))
	{
		ZLOG("socket_good_to_send:error flag set\n");
		return 0;
	}
	
	if(FD_ISSET(s, &write_flags))
	{
		FD_CLR(s, &write_flags);
		return 1;
	}
	else
		return 0;
}

int SocketHandler::pause_for_send()
{
	const double max_wait = 0.5;
	double start_time;
	bool kill_me = false;
	
	if(socket_good_to_send(kill_me)) return 1;
	
	start_time = current_time();
	
	do
	{
		uni_pause(10);
		
		ZLOG("pause_for_send:had to begin pausing\n");
		
		if(socket_good_to_send(kill_me)) return 1;
		if(kill_me) return 0;
	} while(current_time() - start_time < max_wait);
	
	return 0;
}

char *SocketHandler::GetMAC(char *buf)
{
	//clear
	memset(buf, 0, 6);

#ifdef __amigaos__
	//Registrierungspruefung ist abgeschaltet; die Linux-Abfrage ueber
	//SIOCGIFHWADDR gibt es hier nicht.
	return buf;
#endif

#ifdef Q_OS_WIN
	IP_ADAPTER_INFO AdapterInfo[16];

	DWORD dwBufLen = sizeof(AdapterInfo);

	DWORD dwStatus = GetAdaptersInfo(AdapterInfo, &dwBufLen);
	if(dwStatus == ERROR_SUCCESS) 
	{
		PIP_ADAPTER_INFO pAdapterInfo = AdapterInfo;

		do 
		{
			for(int i=0; i<6;i++)
				if(pAdapterInfo->Address[i])
				{
					memcpy(buf, pAdapterInfo->Address, 6);
					return buf;
				}

			pAdapterInfo = pAdapterInfo->Next;
		}
		while(pAdapterInfo);
	}
#elif !defined(__amigaos__)
	int skfd;
	struct ifreq sIfReq;
	struct if_nameindex *pIfList, *pIfList_tofree;

	//create socket
	skfd = socket(AF_INET,SOCK_DGRAM,0);
	if(skfd<0)
	{
		ZLOG( "SocketHandler::GetMAC: create socket failed\n");
		return buf;
	}

	pIfList = if_nameindex();
	pIfList_tofree = pIfList;

	for ( pIfList; *(char *)pIfList != 0; pIfList++ )
	{
		strncpy( sIfReq.ifr_name, pIfList->if_name, IF_NAMESIZE );

		if ( ioctl(skfd, SIOCGIFHWADDR, &sIfReq) != 0 )
			ZLOG( "SocketHandler::GetMAC: ioctl failed\n");
		else
		{
			for(int i=0; i<6;i++)
				if(sIfReq.ifr_ifru.ifru_hwaddr.sa_data[i])
				{
					memcpy(buf, sIfReq.ifr_ifru.ifru_hwaddr.sa_data, 6);

					if(pIfList_tofree) if_freenameindex(pIfList_tofree);
					close(skfd);

					return buf;
				}
		}
	}

	if(pIfList_tofree) if_freenameindex(pIfList_tofree);
	close(skfd);
#endif

	ZLOG("MAC ADDRESS: ");
	for(int i=0;i<6;i++) ZLOG("%02x-", (unsigned char)buf[i]);
	ZLOG("\n");

	return buf;
}

