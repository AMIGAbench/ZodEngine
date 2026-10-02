#include "server_socket.h"

ServerSocket::ServerSocket()
{
	int i;
	
	started = 0;
	bound = 0;
	listen_socket = -1;
	client_socket.clear();
	
	event_list = nullptr;
	ehandler = nullptr;
}

int ServerSocket::PlayersConnected()
{
   return client_socket.size();
}

int ServerSocket::SendMessageAll(int pack_id, const char *data, int size)
{
	vector<SocketHandler*>::iterator s;

	for(s=client_socket.begin();s!=client_socket.end();s++)
		(*s)->SendMessage(pack_id, data, size);

	return 1;
}

int ServerSocket::SendMessage(int player, int pack_id, const char *data, int size)
{
	if(client_socket[player]) return client_socket[player]->SendMessage(pack_id, data, size);
	else return 0;
}

int ServerSocket::SendMessageAscii(int player, int pack_id, const char *data)
{
	if(client_socket[player]) return client_socket[player]->SendMessageAscii(pack_id, data);
	else return 0;
}

void ServerSocket::SetEventList(list<Event*> *event_list_)
{
	event_list = event_list_;
}

SocketHandler* ServerSocket::GetHandler(int id)
{
	return client_socket[id];
}

int ServerSocket::Process()
{
	//check for connects
	CheckConnects();
	
	//check for data
	CheckData();

	return 1;
}

int ServerSocket::CheckData()
{
	int i;
	char *message;
	int size;
	int pack_id;

	/* ---- Index statt Iterator, und das ist kein Stilfrage ---------------
	 *
	 * `ehandler->ProcessEvent` laeuft MITTEN in dieser Schleife und kann die
	 * Liste veraendern: Ein START_BOT_EVENT fuehrt ueber ZServer::StartVote
	 * -> ProcessVote -> StartBot -> ZBot::Setup ->
	 * ClientSocket::StartLoopback -> ServerSocket::AddLoopbackClient zu
	 * einem `client_socket.push_back(...)`.
	 *
	 * `push_back` macht bei einer Neubelegung ALLE Iteratoren ungueltig. Die
	 * Urfassung las danach freigegebenen Speicher -- auf dem Host ein
	 * Absturz in DoFastProcess mit `this == 0`, auf dem Amiga (ohne
	 * Speicherueberwachung) ein Einfrieren des Spiels, waehrend die Musik
	 * weiterlief. Genau so hat es der Nutzer gemeldet: "Manage Bots" ->
	 * Farbe auf "On".
	 *
	 * Ein Index liest den -- moeglicherweise verschobenen -- Datenzeiger bei
	 * JEDEM Zugriff neu und ist damit gegen das Wachsen der Liste sicher.
	 *
	 * Zweiter Fehler derselben Stelle: Das alte `if(*the_sock)` hatte keinen
	 * else-Zweig. Ein leerer Platz haette weder `i` noch den Iterator
	 * weitergeschaltet -- eine Endlosschleife, also ebenfalls ein
	 * Einfrieren. */
	for(i = 0; i < (int)client_socket.size(); )
	{
		SocketHandler *sock = client_socket[i];

		//leerer Platz: ueberspringen, NICHT haengenbleiben
		if(!sock)
		{
			i++;
			continue;
		}

		//not connected, free it up
		if(!sock->Connected())
		{
			delete sock;
			client_socket.erase(client_socket.begin() + i);

			ehandler->ProcessEvent(OTHER_EVENT, DISCONNECT_EVENT, nullptr, 0, i);
			continue;
		}

		if(sock->DoRecv())
		{
			/* Nach jedem ProcessEvent neu nachsehen: der Eintrag kann
			 * verschoben (Neubelegung) oder entfernt worden sein. */
			while(i < (int)client_socket.size() && client_socket[i] &&
			      client_socket[i]->DoFastProcess(&message, &size, &pack_id))
				ehandler->ProcessEvent(TCP_EVENT, pack_id, message, size, i);

			if(i < (int)client_socket.size() && client_socket[i])
				client_socket[i]->ResetFastProcess();
		}

		i++;
	}

	return 1;
}

SocketHandler* ServerSocket::AddLoopbackClient()
{
	SocketHandler *handler = new SocketHandler();

	client_socket.push_back(handler);

	string connect_ip = "loopback";
	ehandler->ProcessEvent(OTHER_EVENT, CONNECT_EVENT, (char*)connect_ip.c_str(),
	                       connect_ip.size()+1, client_socket.size()-1);

	ZLOG("ServerSocket::AddLoopbackClient:client connected (loopback)\n");

	return handler;
}

int ServerSocket::CheckConnects()
{
	struct sockaddr_in temp_in;
	int temp_id;
	int so_sndbuf_amount = 200000;
#ifdef _WIN32
	int c_temp_addrlen;
	int tst = sizeof(so_sndbuf_amount);
#else
	socklen_t c_temp_addrlen;
	socklen_t tst = sizeof(so_sndbuf_amount);
#endif
	int &s = listen_socket;

	c_temp_addrlen = sizeof(temp_in);
	temp_id = accept(s,(struct sockaddr*)  &temp_in, &c_temp_addrlen);

	//accept
	if(temp_id > -1) //got one so lets put him in
	{
		//we want a big buffer to send the map in particular
		if(setsockopt(temp_id, SOL_SOCKET, SO_SNDBUF, (char*)&so_sndbuf_amount, sizeof(so_sndbuf_amount)) == -1)
			ZLOG("ServerSocket::CheckConnects:error setting SO_SNDBUF to %d\n", so_sndbuf_amount);

		//testing purposes
		//if(!getsockopt(temp_id, SOL_SOCKET, SO_SNDBUF, (char*)&so_sndbuf_amount, &tst))
		//	ZLOG("ServerSocket::CheckConnects:SO_SNDBUF:%d\n", so_sndbuf_amount);

		client_socket.push_back(new SocketHandler(temp_id, temp_in));

		string connect_ip = inet_ntoa(temp_in.sin_addr);
		ehandler->ProcessEvent(OTHER_EVENT, CONNECT_EVENT, (char*)connect_ip.c_str(), connect_ip.size()+1, client_socket.size()-1);
		//event_list->push_back(new Event(OTHER_EVENT, CONNECT_EVENT, client_socket.size()-1, (char*)connect_ip.c_str(), connect_ip.size()+1));
		//event_list->push_back(new Event(OTHER_EVENT, CONNECT_EVENT, client_socket.size()-1, nullptr, 0));

		ZLOG("ServerSocket::CheckConnects:client connected:%s\n", inet_ntoa(temp_in.sin_addr));
	}

	return 1;
}

int ServerSocket::Start(int port_)
{
	port = port_;
	
	if(!CreateSocket()) return 0;
	
	if(!Bind()) return 0;
	
	if(!Listen()) return 0;
	
	started = 1;
	
	return 1;
}

int ServerSocket::CreateSocket()
{
	int &s = listen_socket;
	
	if(s != -1) return 1;
	
#ifdef _WIN32
	WSADATA wsaData;
	
	WSAStartup(0x202,&wsaData);
#endif
	
	if((s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
	{
		ZLOG("ServerSocket::CreateSocket:error in socket creation\n");
		return 0;
	}

	//so the server can do a quick restart after a crash
	int on = 1;
	if(setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (char*)&on, sizeof(on)) == -1)
		ZLOG("ServerSocket::CreateSocket:error setting SO_REUSEADDR on\n");
	
	return 1;
}

int ServerSocket::Bind()
{
	struct sockaddr_in si_me;
	int &s = listen_socket;
	
	if(bound) return 1;
	
	memset((char *) &si_me, sizeof(si_me), 0);
	si_me.sin_family = AF_INET;
	si_me.sin_port = htons(port);
	si_me.sin_addr.s_addr = htonl(INADDR_ANY);
	
	while(bind(s, (struct sockaddr *) &si_me, sizeof(si_me))==-1)
	{
		ZLOG("ServerSocket::Bind:error binding socket\n");
		uni_pause(5000);
	}
	
	return 1;
}

int ServerSocket::Listen()
{
	u_long on_mode = 1;
	int &s = listen_socket;
	
	if(started) return 1;
	
	while (listen(s, 5) == -1)
	{
		ZLOG("ServerSocket::Listen:error starting listen\n");
		uni_pause(5000);
	}
	
#ifdef _WIN32
	ioctlsocket(s, FIONBIO, &on_mode);
#else		
	ioctl(s, FIONBIO, &on_mode);
#endif
	
	return 1;
}
