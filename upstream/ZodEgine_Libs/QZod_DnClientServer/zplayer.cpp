#include "zplayer.h"
#include <zod_video.h>
#include <zod_schirm.h>
#include <zod_pack.h>
#include <stdio.h>
#ifdef ZOD_ALLOC_PROBE
extern "C" void zod_alloc_report(const char *tag);
#endif

#include <math.h>

#include "fineclock.h"        /* feine Uhr fuer die Bildratenanzeige, beide Plattformen */
#include "amiga_startup.h"   /* zod_env: Schalter lesen, auf beiden Plattformen */

/* Deckel auf die GEZEICHNETE Groesse -- Flugbahn und Mechanik unberuehrt. */
extern "C" void zod_roto_max_set(int hundertstel);
extern "C" int  zod_roto_max_get(void);
extern "C" int  zod_roto_quant_get(void);   /* fest verdrahtet, nur zum Melden */



#ifdef __amigaos__
#include <proto/dos.h>   /* GetVar -- getenv sieht SetEnv unter libnix nicht */
#endif

using namespace COMMON;

void selection_info::DeleteObject(ZObject *obj)
{
	ZObject::RemoveObjectFromList(obj, selected_list);

	for(int i=0;i<10;i++)
		ZObject::RemoveObjectFromList(obj, quick_group[i]);

	SetupGroupDetails(false);
}

void selection_info::RemoveFromSelected(ZObject *obj)
{
	ZObject::RemoveObjectFromList(obj, selected_list);
	SetupGroupDetails(false);
}

bool selection_info::AverageCoordsOfSelected(int &x, int &y)
{
	if(!selected_list.size()) return false;

	x=0;
	y=0;

	for(vector<ZObject*>::iterator i=selected_list.begin(); i!=selected_list.end(); i++)
	{
		int cx, cy;

		(*i)->GetCenterCords(cx, cy);

		x+=cx;
		y+=cy;
	}

	x/=selected_list.size();
	y/=selected_list.size();

	return true;
}

bool selection_info::GroupIsSelected(int group)
{
	if(!selected_list.size()) return false;
	if(selected_list.size() != quick_group[group].size()) return false;

	for(int i=0;i<selected_list.size();i++)
		if(selected_list[i] != quick_group[group][i])
			return false;

	return true;
}

void selection_info::LoadGroup(int group)
{
	vector<ZObject*>::iterator i;

	selected_list = quick_group[group];

	//set the group number for all of the units
	for(i=selected_list.begin(); i!=selected_list.end(); i++)
		(*i)->SetGroup(group);

	SetupGroupDetails();
}

void selection_info::SetGroup(int group)
{
	vector<ZObject*>::iterator i, e;

	//clear out the list as it stands
	i=quick_group[group].begin();
	e=quick_group[group].end();
	for(; i!=e; i++)
		(*i)->SetGroup(-1);
	

	//save this list
	quick_group[group] = selected_list;

	//set the group number for all of the units
	i=selected_list.begin();
	e=selected_list.end();
	for(; i!=e; i++)
		(*i)->SetGroup(group);
}

bool selection_info::UpdateGroupMember(ZObject *obj)
{
	vector<ZObject*>::iterator i, e;

	i=selected_list.begin();
	e=selected_list.end();
	for(; i!=e; i++)
		if(obj == *i)
		{
			SetupGroupDetails();
			return true;
		}

	return false;
}

void selection_info::SetupGroupDetails(bool show_waypoints)
{
	double &the_time = ztime->ztime;

	vector<ZObject*>::iterator i, e;

	have_explosives = false;
	can_pickup_grenades = false;
	can_move = false;
	can_equip = false;
	can_attack = false;

	i=selected_list.begin();
	e=selected_list.end();
	for(; i!=e; i++)
	{
		unsigned char ot, oid;
		
		(*i)->GetObjectID(ot, oid);

		if(ot == ROBOT_OBJECT) can_equip = true;
		if(ot != CANNON_OBJECT) can_move = true;
		if(ot == VEHICLE_OBJECT && oid == CRANE) can_repair = true;
		
		if((*i)->HasExplosives()) have_explosives = true;
		if((*i)->CanAttack()) can_attack = true;
		if((*i)->CanBeRepaired()) can_be_repaired = true;
		if((*i)->CanPickupGrenades()) can_pickup_grenades = true;

		//we need to reshow their waypoints and this is a good solve all location
		(*i)->ShowWaypoints();
	}
}

bool selection_info::ObjectIsSelected(ZObject *obj)
{
	if(!obj) return false;

	for(vector<ZObject*>::iterator i=selected_list.begin(); i!=selected_list.end(); i++)
		if(*i == obj)
			return true;

	return false;
}

ZPlayer::ZPlayer() : ZClient()
{
	gload_thread = 0;
	graphics_loaded = false;
	prev_w = init_w = 800;
	prev_h = init_h = 600;
	mouse_x = 0;
	/* Bewusst auf einen Wert, den keine echte Zeigerposition annimmt: so wird
	 * das HUD beim ersten Bild einmal aufgebaut und danach nur noch bei
	 * tatsaechlicher Mausbewegung. Uninitialisiert waere das auf AmigaOS
	 * genau die Fehlerklasse, die dieses Projekt schon einmal teuer bezahlt hat. */
	hud_mouse_x = -1;

	/* Frischer Heap ist auf dem Amiga nicht genullt -- ohne das waere die
	 * Frage "haben wir gewonnen?" beim allerersten Rundenende Zufall. */
	end_video_gewonnen = false;
	story_modus = true;          /* ohne -G wie bisher: volle Kampagne */
	fps_an = true;               /* ohne -F wie bisher: Bildrate sichtbar */
	video_planet = 0;            /* die Kampagne beginnt auf der Wueste */
	video_stufe = 0;
	lade_planet = -1;            /* unbekannt -> bisheriges Ladebild */
	lade_name[0] = 0;
	lade_level = 0;
	lade_schirm_steht = false;
	stat_getoetet = 0;
	stat_verloren = 0;
	stat_beginn = 0;
	end_faellig = 0;

	/* Frischer Heap ist auf dem Amiga NICHT genullt -- ohne diese
	 * Vorbelegung zeigte die Bildratenanzeige beim Start Unsinn. */
	fps_last_tick = 0;
	fps_digits_ready = false;
	/* Aus demselben Grund: stuende hier zufaellig "wahr", liefe im ersten Bild
	 * ein voller Objektdurchlauf ohne Anlass; stuende dort dauerhaft Unsinn,
	 * bliebe die Knopfleiste stehen. */
	objekt_anzahl_faellig = false;

	hud_mouse_y = -1;
	mouse_y = 0;
	splash_fade = 255;
	player_name = "Player";
	our_mode = PLAYER_MODE;
	hover_object = nullptr;
	disable_zcursor = false;
	lshift_down = false;
	rshift_down = false;
	right_down = false;
	left_down = false;
	up_down = false;
	down_down = false;
	lctrl_down = false;
	rctrl_down = false;
	lalt_down = false;
	ralt_down = false;
	pcursor_death_time = 0;
	gui_window = nullptr;
	place_cannon = false;
	collect_chat_message = false;
	do_focus_to = false;
	is_windowed = true;
	use_opengl = true;
	//ausdruecklich vorbelegen: ein nie beschriebenes bool entschiede hier
	//zufaellig ueber die Pufferung. Genau diese Fehlerklasse hat heute einen
	//ganzen Abend gekostet -- es ist die schwerste Falle dieser
	//Werkzeugkette.
#ifdef __amigaos__
	//Auf echtem RTG ist SDL_DOUBLEBUF ein echter Pufferwechsel (Vampire V2:
	//136 us; im Emulator 64846 us, weil SDL dort kopiert). Dann zeigt jedes
	//Bild abwechselnd einen anderen Puffer, und alles, was nicht in JEDEM Bild
	//neu gezeichnet wird, flackert -- auf der V2 beobachtet bei Einheiten,
	//Kakteen und dem Sichtrechteck der Minikarte, waehrend Gebaeude und Menue
	//stabil blieben. Der Nutzer hat es als Ein-Variablen-Vergleich belegt:
	//mit -f kein Flackern, ohne -f Flackern.
	//
	//Das hier ist eine UMGEHUNG, nicht die Idealform. Sauber waere, jedes Bild
	//vollstaendig neu zu zeichnen; welche Teile das heute nicht tun, ist nicht
	//ermittelt. Eine belegt wirksame Umgehung ist besser als eine Idealform
	//ohne Beleg.
	single_buffer = true;
#else
	single_buffer = false;
#endif
	//-N: Vorgabe ist der Skalierer AN, auf jeder Plattform.
	no_scaler = false;
	//FARBTIEFE: auf dem Amiga 8 Bit, FEST. Die Zeichenflaeche ist seit der
	//gemeinsamen Palette (18.09.) ohnehin 8 Bit; ein 16-Bit-Schirm zwang
	//SDL_Flip deshalb, jedes Bild Bildpunkt fuer Bildpunkt durch eine Tabelle
	//umzusetzen und die doppelte Menge in den Schirm zu schreiben (im
	//Emulator gemessen 868 gegen 192 us, bei ziffergleichem Zeichnen), 32 Bit
	//das Vierfache. Mit einem 8-Bit-Schirm ist das Umschalten ein reines
	//memcpy je Zeile.
	//
	//Die Option -D und der Setzer SetScreenDepth sind deshalb am 24.09.
	//ENTFALLEN: eine Einstellung, deren andere Stellungen nur langsamer sind
	//und nichts besser machen, ist keine Wahl.
	//
	//DER RUECKFALL BLEIBT: findet der Treiber keinen LUT8-Modus, nimmt
	//port/amiga/sdl_screen.cpp von sich aus 16 Bit. Das ist ein Rueckfall im
	//Treiber, keine Einstellung.
	//
	//AUF DEM HOST BLEIBT ES BEI 32 BIT. Dort zeichnet echtes SDL, nicht der
	//eigene 8-Bit-Weg -- eine 8 waere hier kein Gewinn, sondern ein Bruch.
#ifdef __amigaos__
	screen_depth = 8;
#else
	screen_depth = 32;
#endif
	//music_on = true;
	loaded_percent = 0;
	show_chat_history = false;
	fort_ref_id = -1;

	ClearAsciiStates();

	select_info.SetZTime(&ztime);
	zhud.SetZTime(&ztime);

	//setup minimap
	zhud.GetMiniMap().Setup(&zmap, &object_list);
	
	//setup events
	SetupEHandler();

	//setup the comp message system
	/* Nur die Gebaeude: RenderGuns laeuft je Bild ueber diese Liste und
	 * verwirft ohnehin alles, was kein BUILDING_OBJECT ist. Ueber
	 * object_list waren das 393 Objekte im Mittel (bis 3004 auf der
	 * grossen Karte), ueber building_olist rund 36. Ergebnis gleich,
	 * weil ols.AddObject beide Listen parallel fuellt. */
	zcomp_msg.SetObjectList(&ols.building_olist);
	zcomp_msg.SetZTime(&ztime);
	
	//give the tcp socket the event list so it can cram in events
	client_socket.SetEventList(&ehandler.GetEventList());
	//ZObject::SetEffectList(&effect_list);
	ZEffect::SetSettings(&zsettings);
	ZEffect::SetEffectList(&new_effect_list);
	ZEffect::SetMap(&zmap);

	//alloc menu memory
	InitMenus();

	//create and setup factory gui
	gui_factory_list = new GWFactoryList(&ztime);
	gui_factory_list->Hide();
	gui_factory_list->SetOList(&ols);
}

void ZPlayer::ProcessResetGame()
{
#ifdef ZOD_ALLOC_PROBE
	zod_alloc_report("client-reset-anfang");
#endif

	//unset this
	fort_ref_id = -1;

	/* Gedrehte Fassungen verwerfen: Sie gehoeren zur alten Runde, und ihr
	 * Speicher wird gleich fuer die neue Kartenflaeche gebraucht. Gemessen
	 * hielt der Zwischenspeicher am Rundenende 19,3 MB. */
	ZSDL_Surface::DropAllRotoZoom();

	//clear stuff out
	zmap.ClearMap();
	
	//clear object list
	ols.DeleteAllObjects();
	//for(vector<ZObject*>::iterator obj=object_list.begin(); obj!=object_list.end(); obj++)
	//	delete *obj;

	//object_list.clear();

	//selection info
	select_info.ClearAll();

	//effect list
	{
		/* end() statt begin(): die Schleife lief nie, und beim Kartenwechsel
		 * blieb die gesamte Effektliste im Speicher liegen. Auf AmigaOS ist
		 * nicht freigegebener Speicher bis zum Neustart verloren. */
		for(vector<ZEffect*>::iterator e=effect_list.begin(); e!=effect_list.end(); e++)
			delete *e;

		effect_list.clear();
	}

	/* Im selben Takt erzeugte Effekte: Sie wandern sonst nach dem Ruecksetzen
	 * in die Effektliste der NEUEN Karte, mit einem Verweis auf die inzwischen
	 * geleerte alte Karte. */
	for(vector<ZEffect*>::iterator e=new_effect_list.begin(); e!=new_effect_list.end(); e++)
		delete *e;

	new_effect_list.clear();

	//space bar event list
	space_event_list.clear();

	//no more animals
	ClearAnimals();

	hover_object = nullptr;

	//hud
	zhud.ResetGame();

	//gui window ie production
	if(gui_window) DeleteCurrentGuiWindow();

#ifdef ZOD_ALLOC_PROBE
	zod_alloc_report("client-reset-ende");
#endif

	//ask for the map
	client_socket.SendMessage(REQUEST_MAP, nullptr, 0);
}

void ZPlayer::SetUseOpenGL(bool use_opengl_)
{
	use_opengl = use_opengl_;
}

void ZPlayer::SetLoginName(string login_name_)
{
	login_name = login_name_;
}

void ZPlayer::SetLoginPassword(string login_password_)
{
	login_password = login_password_;
}

void ZPlayer::SetWindowed(bool is_windowed_)
{
	is_windowed = is_windowed_;
}

void ZPlayer::DisableCursor(bool disable_zcursor_)
{
	disable_zcursor = disable_zcursor_;
}

void ZPlayer::SetSoundsOff(bool setoff)
{
	ZSDL_SetMusicOn(!setoff);
}

void ZPlayer::SetMusicOff(bool setoff)
{
	//music_on = !setoff;
	ZMusicEngine::SetMusicOn(!setoff);
}

void ZPlayer::SetPlayerTeam(team_type player_team)
{
	our_team = player_team;

	cursor.SetTeam(our_team);
	zhud.SetTeam(our_team);
	zcomp_msg.SetTeam(our_team);
	if(gui_factory_list) gui_factory_list->SetTeam(our_team);

	RefindOurFortRefID();

	//unselect stuff
	select_info.ClearAll();
	zhud.SetSelectedObject(nullptr);
	zhud.ReRenderAll();

	//clear
	space_event_list.clear();

	//reset cursor
	DetermineCursor();
}

void ZPlayer::SetDimensions(int w, int h)
{
	if(w > 0) prev_w = init_w = w;
	if(h > 0) prev_h = init_h = h;
}

void ZPlayer::Setup()
{
	//randomizer
	SetupRandomizer();

	//registered?
	CheckRegistration();

	if(loopback_server)
	{
		if(!client_socket.StartLoopback(loopback_server))
			ZLOG("ZPlayer::Setup:loopback not setup\n");
	}
	else if(!client_socket.Start(remote_address.c_str()))
		ZLOG("ZPlayer::Setup:socket not setup\n");

	//setup gfile
	//ZGFile::Init();
	
	InitSDL();

	ZMusicEngine::Init();
	ZFontEngine::Init();

	//get it on
	ZMusicEngine::PlaySplashMusic();

	/* ORIGINALLADEBILD, wenn es im Archiv liegt (zod_screens.zpk, von
	 * ZExtract aus z/main.pac geholt).
	 *
	 * Es bekommt die Schirmpalette fuer die Dauer der Anzeige -- rund 250
	 * eigene Farben, die sich nicht in die gemeinsame Palette bringen
	 * lassen. Deshalb faellt zweierlei weg: die Prozentanzeige (sie waere
	 * falschfarbig, und das Original hatte ohnehin keine) und das
	 * Ueberblenden ins Spiel (dafuer muessten beide in EINER Palette
	 * liegen).
	 *
	 * Gezeichnet wird EINMAL. Load_Graphics laeuft danach am Stueck durch,
	 * ohne den Schirm anzufassen -- die bisherige Prozentanzeige hat sich
	 * aus demselben Grund nie bewegt. */
	lade_schirm_steht = false;

	if(lade_planet >= 0)
	{
		char n[64];

		zod_schirm_name(n, sizeof(n), "load", lade_planet,
		                (int)our_team - 1);

		lade_schirm_steht = zod_schirm_zeigen(n) != 0;

		/* BESCHRIFTUNG. Das Originalbild traegt keine -- im Spiel
		 * standen dort "LEVEL 01", darunter der Levelname und ganz
		 * unten "LOADING", gezeichnet vom Programm.
		 *
		 * Alle Lagen sind am DOSBox-Bild des Nutzers AUSGEMESSEN
		 * (23.09.), in Bildkoordinaten 320x200:
		 *
		 *   "LEVEL    01"  font19 gross, zentriert, Oberkante  24
		 *   Levelname      font19 gross, zentriert, Oberkante 100
		 *   "LOADING"      font19 klein eng,        Oberkante 131
		 *
		 * Die vier Leerzeichen sind nicht Zierde: gemessen klafft
		 * zwischen "LEVEL" und der Zahl eine Luecke von 33,8 Punkten,
		 * also rund vier Leerzeichen zu 8. */
		if(lade_schirm_steht)
		{
			char zeile[48];

			if(lade_level > 0)
			{
				zod_schirm_schrift("gross", 1);
				snprintf(zeile, sizeof(zeile), "LEVEL    %02d",
				         lade_level);
				zod_schirm_text((320 - zod_schirm_breite(zeile)) / 2,
				                24, zeile);
			}

			if(lade_name[0])
			{
				int k = 0;

				while(lade_name[k] && k < (int)sizeof(zeile) - 1)
				{
					char c = lade_name[k];

					zeile[k] = (c >= 'a' && c <= 'z')
					         ? (char)(c - 'a' + 'A') : c;
					k++;
				}

				zeile[k] = 0;
				zod_schirm_schrift("gross", 1);
				zod_schirm_text((320 - zod_schirm_breite(zeile)) / 2,
				                100, zeile);
			}

			zod_schirm_schrift("laden", 1);
			zod_schirm_text((320 - zod_schirm_breite("LOADING")) / 2,
			                131, "LOADING");

			zod_schirm_ausgeben();
		}
	}

	if(!lade_schirm_steht)
	{
		DoSplash();
		if(use_opengl) SDL_GL_SwapBuffers();
		else SDL_Flip(screen);
	}

	//important to keep the server from crashing us
	ZTeam::Init();
	ZLOG("Start: ZTeam::Init fertig\n");
	ZMap::Init();
	ZLOG("Start: ZMap::Init fertig\n");
	zhud.Init();
	ZLOG("Start: zhud.Init fertig\n");
	ORock::Init();
	ZLOG("Start: ORock::Init fertig\n");
	SetupSelectionImages();
	ZLOG("Start: SetupSelectionImages fertig\n");

	//if(!disable_zcursor) SDL_ShowCursor(SDL_DISABLE);

	//synchron laden: unter AmigaOS soll das Spiel in einem Task laufen
	Load_Graphics(this);
	ZLOG("Start: Load_Graphics fertig\n");

	/* Ladebild weg und gemeinsame Palette zurueck. splash_fade auf 0,
	 * damit DoSplash im Spiel gar nicht erst anfaengt zu ueberblenden --
	 * es gab nichts zu ueberblenden. */
	if(lade_schirm_steht)
	{
		zod_schirm_ende();
		lade_schirm_steht = false;
		splash_fade = 0;
	}

	/* Schirm einmal loeschen, bevor das Spiel beginnt.
	 *
	 * Die Engine zeichnet nicht jeden Bildpunkt in jedem Bild neu: Teile der
	 * Anzeige (z. B. der Platz fuer das Einheitenbild rechts) bleiben leer,
	 * solange nichts anzuzeigen ist. Sie setzt dabei stillschweigend voraus,
	 * dass der Schirm schwarz beginnt.
	 *
	 * Auf dem Amiga stimmt das nicht: Dort steht noch das Ladebild, und es
	 * blieb sichtbar durch die Anzeige stehen -- vom Nutzer auf der V2
	 * gemeldet und im Bildschirmfoto bestaetigt. Ein einziges Loeschen hier
	 * genuegt; es kostet nichts und gilt auf allen Plattformen gleich. */
	if(screen) SDL_FillRect(screen, nullptr, SDL_MapRGB(screen->format, 0, 0, 0));
}

void ZPlayer::InitSDL()
{
	int audio_rate = 22050;
	Uint16 audio_format = AUDIO_S16; /* 16-bit stereo */
	int audio_channels = 2;
	int audio_buffers = 4096;

	//init SDL
	SDL_Init(SDL_INIT_VIDEO|SDL_INIT_AUDIO);

	//some stuff that just has to be right after init
	game_icon = IMG_Load("assets/icon.png");
	//ffuts

	if(game_icon) SDL_WM_SetIcon(game_icon, nullptr);
	SDL_WM_SetCaption("Zod Engine", "Zod Engine");
	atexit(ZSDL_Quit);//SDL_Quit);
	SDL_EnableUNICODE(SDL_ENABLE);
	SDL_EnableKeyRepeat(SDL_DEFAULT_REPEAT_DELAY, SDL_DEFAULT_REPEAT_INTERVAL);

#ifdef DISABLE_OPENGL
	use_opengl = false;
#endif

	ZSDL_Surface::SetUseOpenGL(use_opengl);
	ZSDL_Surface::SetScreenDimensions(init_w, init_h);
	ZSDL_Surface::SetNoScaler(no_scaler);

	/* Das je Bild neue Drehen der vier Truemmerklassen -- auf dem Amiga
	 * Vorgabe AUS, weil es dort den schwersten Einbruch verursacht (gemessen
	 * 167 ms in einem Bild bei 22 Drehungen). Auf anderen Systemen bleibt es
	 * an. `SetEnv ZOD_SPIN ein|aus` stellt beides um, fuer den A/B in
	 * derselben Binaerdatei. */
	{
#ifdef __amigaos__
		bool spin = false;
#else
		bool spin = true;
#endif
		char buf[16];
		const char *e = zod_env("ZOD_SPIN", buf, sizeof(buf));

		if(e)
		{
			if(*e == 'e' || *e == 'E' || *e == '1') spin = true;
			if(*e == 'a' || *e == 'A' || *e == '0') spin = false;
		}

		ZSDL_Surface::SetNoSpin(!spin);

		/* Deckel auf die gezeichnete Groesse. Vorgabe 0 = keiner.
		 *
		 * Die Umgebungsvariable ist nur der RUECKFALL: Hat die Befehlszeile
		 * `-S` gesetzt (main.cpp, vor Setup()), steht hier bereits ein Wert,
		 * und der gewinnt. Sonst waere die Reihenfolge davon abhaengig, wer
		 * zuletzt schreibt -- und `SetEnv` bleibt auf AmigaOS stehen, koennte
		 * also eine Launcher-Einstellung stillschweigend ueberstimmen. */
		{
			char b4[16];
			const char *m = zod_roto_max_get() ? NULL
			                                   : zod_env("ZOD_ROTOMAX", b4, sizeof(b4));

			if(m)
			{
				int v = 0, k = 0, ziffern = 0;

				while(m[k] >= '0' && m[k] <= '9')
				{
					v = v * 10 + (m[k] - '0');
					k++;
					ziffern++;
				}

				if(ziffern) zod_roto_max_set(v);
			}

			if(zod_roto_max_get())
				ZLOG("Groessendeckel: %ld Hundertstel (nur die Grafik -- "
				     "Flugbahn und Flugdauer unveraendert)\n",
				     (long)zod_roto_max_get());
			else
				ZLOG("Groessendeckel: keiner (Vorgabe) -- SetEnv ZOD_ROTOMAX 300 "
				     "fuer hoechstens Groesse 3,00\n");
		}

		ZLOG("Groessenraster: %ld Hundertstel (fest)\n",
		     (long)zod_roto_quant_get());

		ZLOG("Truemmer drehen: %s\n", spin
		     ? "ja (frei, je Bild)"
		     : "nein (Bildfolge wie Original) -- SetEnv ZOD_SPIN ein zum Vergleichen");
	}

	/* Ins Protokoll, weil sonst ein Mitschnitt nicht zuzuordnen ist -- genau
	 * der Fehler, der bei -f und bei -u schon einmal passiert ist. */
	ZLOG("Skalierer: %s\n", no_scaler ? "aus (-N, wie Original 1996)" : "an");

	if(use_opengl)
	{
		//if(is_windowed)
		//	screen = SDL_SetVideoMode(init_w, init_h, 32, SDL_HWSURFACE|SDL_DOUBLEBUF|SDL_RESIZABLE);
		//else
		//	screen = SDL_SetVideoMode(init_w, init_h, 32, SDL_HWSURFACE|SDL_DOUBLEBUF|SDL_RESIZABLE|SDL_FULLSCREEN);

		if(is_windowed)
			screen = SDL_SetVideoMode(init_w, init_h, 0, SDL_OPENGL | SDL_RESIZABLE);
		else
			screen = SDL_SetVideoMode(init_w, init_h, 0, SDL_OPENGL | SDL_RESIZABLE | SDL_FULLSCREEN);

		InitOpenGL();
		ResetOpenGLViewPort(init_w, init_h);
	}
	else
	{
		//-f laesst SDL_DOUBLEBUF weg. Grund: auf echtem RTG ist das Umschalten
		//ein echter Pufferwechsel (V2: 136 us gegen 64846 us im Emulator, wo
		//SDL kopiert). Dann zeigt jedes Bild abwechselnd einen anderen Puffer,
		//und alles, was nicht in JEDEM Bild neu gezeichnet wird, flackert --
		//auf der V2 beobachtet bei allen bewegten Grafiken, waehrend statische
		//stabil blieben.
		Uint32 flags = SDL_HWSURFACE | SDL_RESIZABLE;

		if(!single_buffer) flags |= SDL_DOUBLEBUF;
		if(!is_windowed)   flags |= SDL_FULLSCREEN;

		screen = SDL_SetVideoMode(init_w, init_h, screen_depth, flags);

		/* Der Text muss sagen, WAS gilt, nicht welcher Schalter gemeint war:
		 * die alte Fassung meldete "einfach (-f)" auch dann, wenn -f gar nicht
		 * uebergeben wurde, weil einfache Pufferung auf dem Amiga Vorgabe ist.
		 * Genau das hat beim Auswerten eines Mitschnitts schon einmal zu einer
		 * falschen Schlussfolgerung gefuehrt. */
#ifdef __amigaos__
		ZLOG("Pufferung: %s\n", single_buffer ? "einfach (Vorgabe auf Amiga)" : "doppelt (-f nicht wirksam?)");
#else
		ZLOG("Pufferung: %s\n", single_buffer ? "einfach (-f)" : "doppelt");
#endif

		ZSDL_Surface::SetMainSoftwareSurface(screen);
	}

	//Das Original prueft den Rueckgabewert nicht: schlaegt das Oeffnen fehl,
	//laeuft das Spiel mit einem Null-Zeiger weiter und rendert ins Nichts.
	//Auf dem Amiga war genau das nicht zu unterscheiden von "laeuft".
	if(!screen)
		ZLOG("InitSDL: Bildschirm %dx%d liess sich nicht oeffnen: %s\n",
		     init_w, init_h, SDL_GetError());
	else
	{
		const SDL_VideoInfo *vi = SDL_GetVideoInfo();

		//hw/fill: ob SDL_FillRect den Hardware-Fuellweg des Treibers nimmt
		//(auf echtem RTG ja, auf der Picasso96-Workbench im Emulator nicht)
		ZLOG("Bildschirm: %dx%d, %d Bit%s, hw=%d fill=%d\n", screen->w, screen->h,
		     screen->format->BitsPerPixel,
		     (screen->flags & SDL_FULLSCREEN) ? ", Vollbild" : "",
		     (screen->flags & SDL_HWSURFACE) ? 1 : 0,
		     vi ? (int)vi->blit_fill : -1);
	}

	if(!disable_zcursor) SDL_ShowCursor(SDL_DISABLE);

	//some initial mouse stuff
	SDL_WM_GrabInput(SDL_GRAB_ON);
	//SDL_EventState(SDL_MOUSEMOTION, SDL_IGNORE);
	SDL_WarpMouse(init_w>>1, init_h>>1);
	//SDL_EventState(SDL_MOUSEMOTION, SDL_ENABLE);

	//Removed because some sdl_mixer libs dont have  
	//this function and it is not 100% required
	//if(Mix_Init(MIX_INIT_MOD | MIX_INIT_OGG) != (MIX_INIT_MOD | MIX_INIT_OGG))
	//	ZLOG("InitSDL::Mix_Init() error\n");

	if(Mix_OpenAudio(audio_rate, audio_format, audio_channels, audio_buffers) == -1)
		ZLOG("InitSDL::Mix_OpenAudio() error\n");

	Mix_QuerySpec(&audio_rate, &audio_format, &audio_channels);
	Mix_Volume(-1, 128);
	Mix_VolumeMusic(80);
	sound_setting = SOUND_100;

	//TTF
	TTF_Init();
	ttf_font = TTF_OpenFont("assets/arial.ttf",10);
	ttf_font_7 = TTF_OpenFont("assets/arial.ttf",7);
	if (!ttf_font) ZLOG("could not load assets/arial.ttf\n");

	//splash sound best loaded here
	//splash_music = MUS_Load_Error("assets/sounds/ABATTLE.mp3");
	splash_screen.LoadBaseImage("assets/splash.bmp");// = IMG_Load("assets/splash.bmp");
	splash_screen.UseDisplayFormat(); //Regular needs this to do fading

//	if(splash_screen)
//	{
//		SDL_Surface* bmpFile2 = SDL_DisplayFormat( splash_screen );
//		SDL_FreeSurface( splash_screen );
//		splash_screen = bmpFile2;
//// 		SDL_Surface* tempScreen = SDL_CreateRGBSurface( SDL_SWSURFACE | SDL_SRCALPHA, splash_screen->w, splash_screen->h, 32, 0xff000000,0x00ff0000,0x0000ff00,0x000000ff);
//// 		SDL_Surface* tempScreen2 = SDL_DisplayFormat( tempScreen );
//// 		SDL_FreeSurface( tempScreen );
//	}

	//test
	//Mix_Chunk *test_wav = Mix_LoadWAV("test.wav");
	//ZMix_PlayChannel(-1, test_wav, 0);

	//repeat music
	//if(splash_music)
	//{
	//	ZSDL_PlayMusic(splash_music, -1);
	//	Mix_VolumeMusic(128);
	//}
}

int ZPlayer::Load_Graphics(void *p)
{
	const int max_items = 80;
	int loaded_items = 0;

	ZCompMessageEngine::Init(); ((ZPlayer*)p)->loaded_percent = 100 * ++loaded_items / max_items;
	ZSoundEngine::Init(&((ZPlayer*)p)->zmap); ((ZPlayer*)p)->loaded_percent = 100 * ++loaded_items / max_items;
	if(!((ZPlayer*)p)->disable_zcursor) ZCursor::Init(); ((ZPlayer*)p)->loaded_percent = 100 * ++loaded_items / max_items;
	ZObject::Init(((ZPlayer*)p)->ttf_font_7); ((ZPlayer*)p)->loaded_percent = 100 * ++loaded_items / max_items;
	ZBuilding::Init(); ((ZPlayer*)p)->loaded_percent = 100 * ++loaded_items / max_items;
	ZCannon::Init(); ((ZPlayer*)p)->loaded_percent = 100 * ++loaded_items / max_items;
	ZVehicle::Init(); ((ZPlayer*)p)->loaded_percent = 100 * ++loaded_items / max_items;
	ZRobot::Init(); ((ZPlayer*)p)->loaded_percent = 100 * ++loaded_items / max_items;
	ABird::Init(); ((ZPlayer*)p)->loaded_percent = 100 * ++loaded_items / max_items;
	AHutAnimal::Init(); ((ZPlayer*)p)->loaded_percent = 100 * ++loaded_items / max_items;
	BFort::Init(); ((ZPlayer*)p)->loaded_percent = 100 * ++loaded_items / max_items;
	BRepair::Init(); ((ZPlayer*)p)->loaded_percent = 100 * ++loaded_items / max_items;
	BRadar::Init(); ((ZPlayer*)p)->loaded_percent = 100 * ++loaded_items / max_items;
	BRobot::Init(); ((ZPlayer*)p)->loaded_percent = 100 * ++loaded_items / max_items;
	BVehicle::Init(); ((ZPlayer*)p)->loaded_percent = 100 * ++loaded_items / max_items;
	BBridge::Init(); ((ZPlayer*)p)->loaded_percent = 100 * ++loaded_items / max_items;
	OFlag::Init(); ((ZPlayer*)p)->loaded_percent = 100 * ++loaded_items / max_items;
	OGrenades::Init(); ((ZPlayer*)p)->loaded_percent = 100 * ++loaded_items / max_items;
	ORockets::Init(); ((ZPlayer*)p)->loaded_percent = 100 * ++loaded_items / max_items;
	OHut::Init(); ((ZPlayer*)p)->loaded_percent = 100 * ++loaded_items / max_items;
	OMapObject::Init(); ((ZPlayer*)p)->loaded_percent = 100 * ++loaded_items / max_items;
	CGatling::Init(); ((ZPlayer*)p)->loaded_percent = 100 * ++loaded_items / max_items;
	CGun::Init(); ((ZPlayer*)p)->loaded_percent = 100 * ++loaded_items / max_items;
	CHowitzer::Init(); ((ZPlayer*)p)->loaded_percent = 100 * ++loaded_items / max_items;
	CMissileCannon::Init(); ((ZPlayer*)p)->loaded_percent = 100 * ++loaded_items / max_items;
	VJeep::Init(); ((ZPlayer*)p)->loaded_percent = 100 * ++loaded_items / max_items;
	VLight::Init(); ((ZPlayer*)p)->loaded_percent = 100 * ++loaded_items / max_items;
	VMedium::Init(); ((ZPlayer*)p)->loaded_percent = 100 * ++loaded_items / max_items;
	VHeavy::Init(); ((ZPlayer*)p)->loaded_percent = 100 * ++loaded_items / max_items;
	VAPC::Init(); ((ZPlayer*)p)->loaded_percent = 100 * ++loaded_items / max_items;
	VMissileLauncher::Init(); ((ZPlayer*)p)->loaded_percent = 100 * ++loaded_items / max_items;
	VCrane::Init(); ((ZPlayer*)p)->loaded_percent = 100 * ++loaded_items / max_items;
	RGrunt::Init(); ((ZPlayer*)p)->loaded_percent = 100 * ++loaded_items / max_items;
	RPsycho::Init(); ((ZPlayer*)p)->loaded_percent = 100 * ++loaded_items / max_items;
	RLaser::Init(); ((ZPlayer*)p)->loaded_percent = 100 * ++loaded_items / max_items;
	RPyro::Init(); ((ZPlayer*)p)->loaded_percent = 100 * ++loaded_items / max_items;
	RSniper::Init(); ((ZPlayer*)p)->loaded_percent = 100 * ++loaded_items / max_items;
	RTough::Init(); ((ZPlayer*)p)->loaded_percent = 100 * ++loaded_items / max_items;
	ELaser::Init(); ((ZPlayer*)p)->loaded_percent = 100 * ++loaded_items / max_items;
	EFlame::Init(); ((ZPlayer*)p)->loaded_percent = 100 * ++loaded_items / max_items;
	EPyroFire::Init(); ((ZPlayer*)p)->loaded_percent = 100 * ++loaded_items / max_items;
	EToughRocket::Init(); ((ZPlayer*)p)->loaded_percent = 100 * ++loaded_items / max_items;
	EToughMushroom::Init(); ((ZPlayer*)p)->loaded_percent = 100 * ++loaded_items / max_items;
	EToughSmoke::Init(); ((ZPlayer*)p)->loaded_percent = 100 * ++loaded_items / max_items;
	ELightRocket::Init(); ((ZPlayer*)p)->loaded_percent = 100 * ++loaded_items / max_items;
	ELightInitFire::Init(); ((ZPlayer*)p)->loaded_percent = 100 * ++loaded_items / max_items;
	EMoMissileRockets::Init(); ((ZPlayer*)p)->loaded_percent = 100 * ++loaded_items / max_items;
	EMissileCRockets::Init(); ((ZPlayer*)p)->loaded_percent = 100 * ++loaded_items / max_items;
	ERobotDeath::Init(); ((ZPlayer*)p)->loaded_percent = 100 * ++loaded_items / max_items;
	ERobotTurrent::Init(); ((ZPlayer*)p)->loaded_percent = 100 * ++loaded_items / max_items;
	EUnitParticle::Init(); ((ZPlayer*)p)->loaded_percent = 100 * ++loaded_items / max_items;
	EDeath::Init(); ((ZPlayer*)p)->loaded_percent = 100 * ++loaded_items / max_items;
	EStandard::Init(); ((ZPlayer*)p)->loaded_percent = 100 * ++loaded_items / max_items;
	EDeathSparks::Init(); ((ZPlayer*)p)->loaded_percent = 100 * ++loaded_items / max_items;
	ETurrentMissile::Init(); ((ZPlayer*)p)->loaded_percent = 100 * ++loaded_items / max_items;
	ESideExplosion::Init(); ((ZPlayer*)p)->loaded_percent = 100 * ++loaded_items / max_items;
	ECannonDeath::Init(); ((ZPlayer*)p)->loaded_percent = 100 * ++loaded_items / max_items;
	ERockParticle::Init(); ((ZPlayer*)p)->loaded_percent = 100 * ++loaded_items / max_items;
	ERockTurrent::Init(); ((ZPlayer*)p)->loaded_percent = 100 * ++loaded_items / max_items;
	EMapObjectTurrent::Init(); ((ZPlayer*)p)->loaded_percent = 100 * ++loaded_items / max_items;
	EBridgeTurrent::Init(); ((ZPlayer*)p)->loaded_percent = 100 * ++loaded_items / max_items;
	ECraneConco::Init(); ((ZPlayer*)p)->loaded_percent = 100 * ++loaded_items / max_items;
	ETrack::Init(); ((ZPlayer*)p)->loaded_percent = 100 * ++loaded_items / max_items;
	ETankDirt::Init(); ((ZPlayer*)p)->loaded_percent = 100 * ++loaded_items / max_items;
	ETankSmoke::Init(); ((ZPlayer*)p)->loaded_percent = 100 * ++loaded_items / max_items;
	ETankOil::Init(); ((ZPlayer*)p)->loaded_percent = 100 * ++loaded_items / max_items;
	ETankSpark::Init(); ((ZPlayer*)p)->loaded_percent = 100 * ++loaded_items / max_items;
	GWProduction::Init(); ((ZPlayer*)p)->loaded_percent = 100 * ++loaded_items / max_items;
	GWFactoryList::Init(); ((ZPlayer*)p)->loaded_percent = 100 * ++loaded_items / max_items;
	GWLogin::Init(); ((ZPlayer*)p)->loaded_percent = 100 * ++loaded_items / max_items;
	GWCreateUser::Init(); ((ZPlayer*)p)->loaded_percent = 100 * ++loaded_items / max_items;
	ZGuiMainMenuBase::Init(); ((ZPlayer*)p)->loaded_percent = 100 * ++loaded_items / max_items;
	GMMWButton::Init(); ((ZPlayer*)p)->loaded_percent = 100 * ++loaded_items / max_items;
	GMMWList::Init(); ((ZPlayer*)p)->loaded_percent = 100 * ++loaded_items / max_items;
	GMMWRadio::Init(); ((ZPlayer*)p)->loaded_percent = 100 * ++loaded_items / max_items;
	GMMWTeamColor::Init(); ((ZPlayer*)p)->loaded_percent = 100 * ++loaded_items / max_items;
	ZGuiButton::Init(); ((ZPlayer*)p)->loaded_percent = 100 * ++loaded_items / max_items;
	ZGuiScrollBar::Init(); ((ZPlayer*)p)->loaded_percent = 100 * ++loaded_items / max_items;
	ZPortrait::Init(); ((ZPlayer*)p)->loaded_percent = 100 * ++loaded_items / max_items;
	ZVote::Init(); ((ZPlayer*)p)->loaded_percent = 100 * ++loaded_items / max_items;


	//this should only ever
	//need to be done once...
#ifdef COLLECT_TEAM_COLORS
	ZTeam::SaveAllPalettes();
#endif	

	ZLOG("graphics loaded\n");
	((ZPlayer*)p)->graphics_loaded = true;

	return 1;
}
void ZPlayer::InitAnimals()
{
	const int sq_tile_per_bird = 650;
	int birds;

	ClearAnimals();

	birds = (zmap.GetMapBasics().height * zmap.GetMapBasics().width) / sq_tile_per_bird;
	//birds = 1;

	for(int i=0;i<birds;i++)
		bird_list.push_back(new ABird(&ztime, &zsettings, (planet_type)zmap.GetMapBasics().terrain_type, zmap.GetMapBasics().width * 16, zmap.GetMapBasics().height * 16));

}

void ZPlayer::ClearAnimals()
{
	//Upstream leerte nur die Liste und liess die Voegel liegen -- bei jedem
	//Kartenwechsel rund ein Dutzend Objekte
	for(vector<ZObject*>::iterator i=bird_list.begin(); i!=bird_list.end(); i++)
		delete *i;

	bird_list.clear();
}

void ZPlayer::ProcessDisconnect()
{
	AddNewsEntry("Disconnected from the game server, please restart the client.");
}

void ZPlayer::AddNewsEntry(string message, int r, int g, int b)
{
	const double lasting_time = 17.0;
	news_entry *new_entry;
	SDL_Color textcolor;

	if(!message.length()) return;

	//alloc
	new_entry = new news_entry;

	//extract
	new_entry->r = r;
	new_entry->g = g;
	new_entry->b = b;
	new_entry->message = message;

	//a hack to get software renderer working again...
	//(don't ever allow setting to the invisible "color key" color
	if(!new_entry->r && !new_entry->g && !new_entry->b)
		new_entry->r = 1;

	//make surface
	textcolor.r = new_entry->r;
	textcolor.g = new_entry->g;
	textcolor.b = new_entry->b;
	//new_entry.text_image = TTF_RenderText_Solid(p->ttf_font, new_entry.message.c_str(), textcolor);
	//new_entry->text_image.LoadBaseImage(TTF_RenderText_Solid(p->ttf_font, new_entry->message.c_str(), textcolor));
	new_entry->text_image.LoadBaseImage(ZFontEngine::GetFont(SMALL_WHITE_FONT).Render(new_entry->message.c_str()));
	
	//a hack to get software renderer working again...
	new_entry->text_image.MakeAlphable();
	//if(!use_opengl && new_entry->text_image.GetBaseSurface())
	//{
	//	ZSDL_ModifyBlack(new_entry->text_image.GetBaseSurface());
	//	new_entry->text_image.UseDisplayFormat();
	//	SDL_SetColorKey(new_entry->text_image.GetBaseSurface(), SDL_SRCCOLORKEY, 0x000000); 
	//}

	//set death time
    new_entry->death_time = COMMON::current_time() + lasting_time;

	if(new_entry->text_image.GetBaseSurface())
	  news_list.insert(news_list.begin(), new_entry);
	else
	  ZLOG("ZPlayer::display_news_event: was not about to render news message:%s...\n", new_entry->message.c_str());
}

void ZPlayer::DisplayPlayerList()
{
	string team_players[MAX_TEAM_TYPES];
	string spectators;
	bool bot_player[MAX_TEAM_TYPES];
	bool bot_player_ignored[MAX_TEAM_TYPES];
	bool have_bot_players;
	int tray_players;
	int nobodies;
	char c_message[50];

	if(!player_info.size())
	{
		AddNewsEntry("No one is currently connected");
		return;
	}

	//init
	tray_players = 0;
	nobodies = 0;
	have_bot_players = false;
	for(int i=0;i<MAX_TEAM_TYPES;i++)
	{
		bot_player[i] = false;
		bot_player_ignored[i] = false;
	}

	for(vector<p_info>::iterator i=player_info.begin(); i!=player_info.end(); i++)
	{
		//char message[500];

		//sprintf(message, "p:'%s' t:%s m:%s\n", i->name.c_str(), team_type_string[i->team].c_str(), player_mode_string[i->mode].c_str());

		//AddNewsEntry(message);

		switch(i->mode)
		{
		case NOBODY_MODE:
			nobodies++;
			break;
		case PLAYER_MODE:
			if(team_players[i->team].size())
				team_players[i->team] += ", " + i->name;
			else
				team_players[i->team] += i->name;
			break;
		case BOT_MODE:
			bot_player[i->team] = true;
			bot_player_ignored[i->team] = i->ignored;
			have_bot_players = true;
			break;
		case SPECTATOR_MODE:
			if(spectators.size())
				spectators += ", " + i->name;
			else
				spectators += i->name;
			break;
		case TRAY_MODE:
			tray_players++;
			break;
		}
	}

	AddNewsEntry("--- Players connected ---");
	for(int i=0; i<MAX_TEAM_TYPES; i++)
		if(team_players[i].size() || bot_player[i])
		{
			if(bot_player[i])
			{
				if(bot_player_ignored[i])
					AddNewsEntry(team_type_string[i] + " team -b: " + team_players[i]);
				else
					AddNewsEntry(team_type_string[i] + " team +b: " + team_players[i]);
			}
			else
				AddNewsEntry(team_type_string[i] + " team: " + team_players[i]);
		}

	if(spectators.size())
		AddNewsEntry("spectators: " + spectators);
	
	//if(have_bot_players)
	//{
	//	string message;

	//	for(int i=0; i<MAX_TEAM_TYPES; i++)
	//		if(bot_player[i])
	//		{
	//			if(message.size())
	//				message += ", " + team_type_string[i];
	//			else
	//				message += team_type_string[i];
	//		}

	//	message = "bot teams: " + message;
	//	AddNewsEntry(message);
	//}

	if(tray_players)
	{
		snprintf(c_message, sizeof(c_message), "tray players: %d", tray_players);
		AddNewsEntry(c_message);
	}

	if(nobodies)
	{
		snprintf(c_message, sizeof(c_message), "nobodies: %d", nobodies);
		AddNewsEntry(c_message);
	}

	AddNewsEntry("---------------------");

}

void ZPlayer::DisplayFactoryProductionList()
{
	vector<ZObject*> fort_list;
	vector<ZObject*> robot_list;
	vector<ZObject*> vehicle_list;

	AddNewsEntry("------ Factories ------");

	for(vector<ZObject*>::iterator i=object_list.begin(); i!=object_list.end(); i++)
	{
		if((*i)->GetOwner() != our_team) continue;

		unsigned char ot, oid;

		(*i)->GetObjectID(ot, oid);

		if(ot != BUILDING_OBJECT) continue;

		switch(oid)
		{
		case FORT_FRONT:
		case FORT_BACK:
			fort_list.push_back(*i);
			break;
		case ROBOT_FACTORY: robot_list.push_back(*i); break;
		case VEHICLE_FACTORY: vehicle_list.push_back(*i); break;
		}
	}

	for(vector<ZObject*>::iterator i=fort_list.begin(); i!=fort_list.end(); i++)
		DisplayFactoryProductionListUnit(*i);

	for(vector<ZObject*>::iterator i=robot_list.begin(); i!=robot_list.end(); i++)
		DisplayFactoryProductionListUnit(*i);

	for(vector<ZObject*>::iterator i=vehicle_list.begin(); i!=vehicle_list.end(); i++)
		DisplayFactoryProductionListUnit(*i);

	AddNewsEntry("--------------------");
}

void ZPlayer::DisplayFactoryProductionListUnit(ZObject *obj)
{
	string display_msg;
	string build_unit;
	char num_c[50];
	unsigned char ot, oid;
	unsigned char fot, foid;

	if(!obj) return;

	obj->GetObjectID(fot, foid);

	switch(foid)
		{
		case FORT_FRONT:
		case FORT_BACK:
			display_msg = "fort ";
			break;
		case ROBOT_FACTORY: display_msg = "robot "; break;
		case VEHICLE_FACTORY: display_msg = "vehicle "; break;
		}

	snprintf(num_c, sizeof(num_c), "L%d: ", obj->GetLevel() + 1);
	display_msg += num_c;

	obj->GetBuildUnit(ot, oid);

	switch(ot)
	{
	case ROBOT_OBJECT:
		if(oid >= 0 && oid < MAX_ROBOT_TYPES) build_unit = robot_type_string[oid];
		break;
	case VEHICLE_OBJECT:
		if(oid >= 0 && oid < MAX_VEHICLE_TYPES) build_unit = vehicle_type_string[oid];
		break;
	case CANNON_OBJECT:
		if(oid >= 0 && oid < MAX_CANNON_TYPES) build_unit = cannon_type_string[oid];
		break;
	}

	switch(obj->GetBuildState())
	{
	case BUILDING_PLACE:
		display_msg += "placing cannon";
		break;
	case BUILDING_SELECT:
		display_msg += "no production";
		break;
	case BUILDING_BUILDING:
		display_msg += build_unit;
		break;
	case BUILDING_PAUSED:
		display_msg += "paused";
		break;
	}

	AddNewsEntry(display_msg);
}

void ZPlayer::SetupSelectionImages()
{
	int t;
	SDL_Rect the_box;

	the_box.x = 0;
	the_box.y = 0;
	the_box.w = 2;
	the_box.h = 2;

	for(t=0;t<MAX_TEAM_TYPES;t++)
	{
		int r,g,b;

		r = team_color[t].r - (int)(team_color[t].r * 0.2);
		g = team_color[t].g - (int)(team_color[t].g * 0.2);
		b = team_color[t].b - (int)(team_color[t].b * 0.2);

		//selection_img[t] = SDL_CreateRGBSurface(SDL_HWSURFACE | SDL_SRCALPHA, 4, 4, 32, 0xFF000000, 0x0000FF00, 0x00FF0000, 0x000000FF);
		/* Im SCHIRMFORMAT, nicht fest in 32 Bit: Auf dem Amiga ist der Schirm
		 * 8 Bit, und eine 32-Bit-Flaeche laesst sich dorthin nicht blitten --
		 * die Auswahlmarkierung waere unsichtbar (dieselbe Ursache wie bei
		 * Schrift und Kratern am 18.09.). */
		{
			SDL_Surface *schirm = SDL_GetVideoSurface();
			const int tiefe = (schirm && schirm->format) ? schirm->format->BitsPerPixel : 32;
			SDL_Surface *neu;

			if(tiefe == 32)
			{
				neu = SDL_CreateRGBSurface(SDL_HWSURFACE | SDL_SRCALPHA, 4, 4, 32,
				                           0xFF000000, 0x0000FF00, 0x00FF0000, 0x000000FF);
			}
			else
			{
				neu = SDL_CreateRGBSurface(SDL_HWSURFACE, 4, 4, tiefe,
				                           schirm->format->Rmask, schirm->format->Gmask,
				                           schirm->format->Bmask, 0);

				/* Gefuellt werden nur 2x2 von 4x4 Pixeln; der Rest muss
				 * DURCHSICHTIG bleiben. In 32 Bit erledigt das SDL_SRCALPHA
				 * (Alpha 0 auf der frisch genullten Flaeche). Im Schirmformat
				 * gibt es kein Alpha -- ohne Farbschluessel zeichnet der
				 * Blitter die genullten Pixel als Platz 0, und der ist
				 * schwarz. Auf der V1200 sah man deshalb statt des gepunkteten
				 * Auswahlrahmens einen schwarzen Kasten. */
				if(neu) SDL_SetColorKey(neu, SDL_SRCCOLORKEY, 0);
			}

			selection_img[t].LoadBaseImage(neu);
		}
		//SDL_FillRect(selection_img[t], &the_box, SDL_MapRGB(selection_img[t]->format, r, g, b));
		ZSDL_FillRect(&the_box, r, g, b, &selection_img[t]);
	}
}

void ZPlayer::Run()
{
	while(allow_run)
	{
		Tick();
		uni_pause(10);
	}
}

//Zeitanteile eines Bildes messen.
//
//Warum nicht einfach Bilder je Sekunde: Auf dem Amiga wird der Schirm mit
//SDL_DOUBLEBUF geoeffnet, das Umschalten wartet also auf den Strahlruecklauf.
//Gemessen wurden 1186 Bilder in 20 s (59,3 fps) -- ziffergleich vor und nach
//vier Aenderungen am Renderpfad. Die Bildrate haengt dort am Deckel, nicht an
//der Arbeit. Die im Zeichnen verbrachte Zeit haengt es nicht.
//
//Nur im Messlauf aktiv (-B), damit das Spiel nichts dafuer bezahlt.
//ACHTUNG, hier stand bis zum 19.09. `current_time()`. Das ist auf dem Amiga
//`gettimeofday`, und die libnix-Fassung holt die Zeit ueber `DateStamp()` --
//im Disassemblat steht woertlich `jsr -192(a6)` auf DOSBase und `muls.l
//#20000,d2`. Die Aufloesung betraegt damit **20 ms**, also mehr als eine ganze
//Bildzeit. Jede Einzelmessung lieferte 0 oder 20000 us; der Mittelwert ueber
//tausende Bilder stimmt zwar, streut aber gewaltig. Genau daher kommen die
//dokumentierten 22 Prozent Streuung "bei gleicher Arbeit" und die 430 us, die
//zwischen `umschalten` und seinen drei Unterposten fehlten -- letztere wurden
//schon mit der feinen Uhr gemessen, der Gesamtposten noch mit der groben.
//
//Alles hier laeuft deshalb ueber die E-Clock (port/amiga/fineclock.h, 1,41 us).
static bool          zod_time_measure = false;
static unsigned long zod_acc_render  = 0;
static unsigned long zod_acc_process = 0;
static unsigned long zod_acc_socket  = 0;
static long          zod_acc_frames  = 0;

void zod_time_measure_enable()
{
	zod_time_measure = true;
}

bool zod_time_measuring()
{
	return zod_time_measure;
}

//Wartezeit im Umschalten getrennt fuehren: SDL_Flip steckt INNERHALB von
//RenderScreen, die Wartezeit auf den Strahlruecklauf wuerde sonst als
//Zeichenarbeit erscheinen.
static unsigned long zod_acc_flip = 0;

extern void zod_roto_frame_end(void);
extern "C" unsigned long zod_roto_frame_made_get(void);   /* zsdl_opengl.cpp */
extern "C" unsigned long zod_roto_frame_ticks_get(void);  /* dito: reine Rechenzeit */
/* Was neben dem Rechnen anfaellt: Freigeben der verdraengten Flaeche (libnix
 * free laeuft die Blockliste ab) und die Speicherverwaltung. */
extern "C" void zod_roto_frame_extra_get(unsigned long *frei, unsigned long *verw,
                                         unsigned long *weg);

/* Blitzahlen JE BILD, nur aus dem Effektabschnitt (sdl_video.cpp liefert die
 * laufenden Summen; auf dem Host gibt es eine leere Fassung). */
extern "C" void zod_blit_now(unsigned long *calls, unsigned long *px,
                             unsigned long *rows);
#ifndef __amigaos__
/* Auf dem Host gibt es den eigenen Blitter nicht -- die Zahlen bleiben 0.
 * Die Zeile im Bericht steht trotzdem da, damit eine fehlende Zahl nicht mit
 * "kein Blit" verwechselt wird. */
extern "C" void zod_blit_now(unsigned long *calls, unsigned long *px,
                             unsigned long *rows)
{
	*calls = 0; *px = 0; *rows = 0;
}
#endif
static unsigned long zod_effblit_calls, zod_effblit_px, zod_effblit_rows;

void zod_time_add_flip(unsigned long ticks)
{
	zod_acc_flip += ticks;
}

/* --- Aufteilung von `zeichnen` ------------------------------------------
 *
 * `zeichnen` ist auf der V1200 35 bis 43 Prozent der Bildzeit, und die
 * vorhandenen Zaehlwerte (Kartenhintergrund, Sprite-Blits, Minikarte)
 * erklaeren davon nur rund ein Viertel. Rund 4500 us je Bild sind nicht
 * Blitten, sondern Arbeit je Objekt.
 *
 * Statt einen Verdacht umzubauen, wird die Zeit hier abschnittsweise
 * aufgeteilt -- dasselbe Vorgehen, das die Frage beim Umschalten in einem
 * einzigen Lauf geklaert hat (sperren/kopieren/rest).
 *
 * Es wird je ABSCHNITTSGRENZE genau einmal auf die Uhr gesehen, nicht je
 * Abschnitt zweimal: `zod_rsec_end` schreibt seinen Messpunkt gleich als
 * Beginn des naechsten Abschnitts fort. */
enum
{
	RSEC_HUD_NEU = 0,   /* zhud.ReRenderAll -- HUD vollstaendig neu       */
	RSEC_KARTE,         /* zmap.DoRender -- Kartenhintergrund             */
	RSEC_KARTENEFFEKTE, /* zmap.DoEffects -- Kachelanimation              */
	RSEC_FUELLER,       /* RenderSmallMapFiller                           */
	RSEC_ZONEN,         /* zmap.DoZoneEffects                             */
	RSEC_EFF_VOR,       /* DoPreRender der Effekte (Spuren, Rauch)        */
	RSEC_OBJ_VOR,       /* DoPreRender der Objekte                        */
	RSEC_WEGPUNKTE,     /* DoRenderWaypoints -- Wege und Sammelpunkte     */
	RSEC_OBJEKTE,       /* DoRender der Objekte                           */
	RSEC_OBJ_NACH,      /* DoAfterEffects                                 */
	RSEC_EFFEKTE,       /* DoRender der Effekte                           */
	RSEC_VOEGEL,        /* bird_list                                      */
	RSEC_AUSWAHL,       /* RenderSelection, RenderAttackRadius, Hover     */
	RSEC_MAUS,          /* RenderMouse, RenderPreviousCursor              */
	RSEC_GUI,           /* Fenster, Vote, Meldungen, News, Menue          */
	RSEC_HUD,           /* zhud.DoRender                                  */
	RSEC_FPS,           /* RenderFps                                      */
	RSEC_HAUPTMENUE,    /* RenderMainMenu                                 */
	RSEC_ZEIGER,        /* cursor.Render + HUD-Auffrischung bei Naehe     */
	RSEC_KLAENGE,       /* PlayBuildingSounds                             */
	RSEC_SPLASH,        /* DoSplash und der Rahmen um die Ausgabe         */
	RSEC_LADEN,         /* ganzes Bild, solange keine Karte geladen ist   */
	RSEC_MAX
};

static const char *zod_rsec_name[RSEC_MAX] =
{
	"hud_neu", "karte", "karteneffekte", "fueller", "zonen",
	"eff_vor", "obj_vor", "wegpunkte",
	"objekte", "obj_nach", "effekte", "voegel", "auswahl",
	"maus", "gui", "hud",
	"fps", "hauptmenue", "zeiger", "klaenge", "splash", "laden"
};

static unsigned long zod_rsec[RSEC_MAX];
static unsigned long zod_rsec_mark = 0;

static unsigned long zod_rsec_spielbilder = 0;

/* Groesse der drei Objektlisten im letzten Bild -- siehe Bericht unten. */
unsigned long zod_rsec_olist_all = 0, zod_rsec_olist_pre = 0, zod_rsec_olist_post = 0;

/* Die Effektliste wurde nirgends gezaehlt -- damit war `zeichnen.effekte`
 * nicht deutbar: 62 us auf 5 Effekte oder auf 1500? Bei einer Fort-Explosion
 * leben dort laut Analyse 1500 bis 2000 Stueck. */
unsigned long zod_eff_frame = 0, zod_eff_peak = 0;
/* Summe ueber alle Bilder, fuer den MITTELWERT je Bild. Ohne ihn ist
 * `zeichnen.effekte` nicht in "je Effekt" umzurechnen -- und genau das ist
 * die Zahl, um die es beim groessten verbliebenen Posten geht. Bisher gab es
 * nur die Spitze, und die sagt ueber das Mittel nichts. */
unsigned long zod_eff_summe = 0, zod_eff_bilder = 0;
unsigned long zod_eff_neu = 0, zod_eff_weg = 0, zod_eff_neu_peak = 0;

extern "C" unsigned long zod_eff_mittel_get(void)
{
	return zod_eff_bilder ? zod_eff_summe / zod_eff_bilder : 0;
}

extern "C" void zod_effect_report(unsigned long *jetzt, unsigned long *spitze,
                                  unsigned long *neu, unsigned long *weg,
                                  unsigned long *neu_spitze)
{
	*jetzt = zod_eff_frame; *spitze = zod_eff_peak;
	*neu = zod_eff_neu; *weg = zod_eff_weg; *neu_spitze = zod_eff_neu_peak;
}

/* --- Die schlimmsten drei Bilder einzeln aufschluesseln -------------------
 *
 * Der Mittelwert kann einen Einbruch prinzipiell nicht zeigen. Ein Lauf des
 * Nutzers vom 19.09. meldet `laengstes 953167 us` -- fast eine Sekunde -- bei
 * einem Mittel von 9534 us fuer `zeichnen`. Kein Posten der Aufteilung
 * erklaert das.
 *
 * DAS HIER KOSTET KEINEN EINZIGEN ZUSAETZLICHEN ZEITABRUF. Die 23 Blicke auf
 * die Uhr je Bild werden ohnehin gemacht (auf der V1200 rund 198 us je Bild,
 * siehe `zeichnen.Messkosten`); sie werden bisher nur aufsummiert. Gesichert
 * wird der Stand am Bildanfang, am Bildende die Differenz gebildet -- 46
 * Additionen und selten ein Kopiervorgang von 92 Byte.
 *
 * DREI Bilder, nicht eines: Sonst gewinnt regelmaessig ein Ladebild oder ein
 * Plattenzugriff, und der eigentliche Einbruch bleibt unsichtbar.
 *
 * Mitgeschrieben werden auch die Arbeitsanzeiger, denn erst sie machen ein
 * Bild identifizierbar: 1400 Effekte heisst Explosion, 0 Effekte und viel
 * `karte` heisst Kartenwechsel. */
#define ZOD_WORST 3

static unsigned long zod_rsec_prev[RSEC_MAX];
static unsigned long zod_worst_sum[ZOD_WORST];
static unsigned long zod_worst_sec[ZOD_WORST][RSEC_MAX];
static unsigned long zod_worst_nr[ZOD_WORST];
static unsigned long zod_worst_eff[ZOD_WORST];
static unsigned long zod_worst_roto[ZOD_WORST];
static unsigned long zod_worst_rotot[ZOD_WORST];   /* davon reines Rechnen */
static unsigned long zod_worst_frei[ZOD_WORST];    /* davon Freigeben */
static unsigned long zod_worst_verw[ZOD_WORST];    /* davon Speicherverwaltung */
static unsigned long zod_worst_weg[ZOD_WORST];     /* Stueckzahl Freigaben */

/* ---- Die laengste FOLGE langsamer Bilder ----------------------------------
 *
 * Der Nutzer meldet, der groesste Einbruch komme im Augenblick des Uebergangs
 * vom heilen zum zerstoerten Hauptgebaeude -- noch bevor die Truemmer hoch
 * fliegen. Das schlimmste EINZELbild liegt aber woanders (im hohen Flug).
 * Beides kann stimmen: ein einzelnes Bild von 150 ms ist ein Zucken, zwanzig
 * Bilder zu 60 ms sind eine spuerbare Zaehigkeit.
 *
 * Genau das konnte die bisherige Messung nicht unterscheiden -- sie meldet
 * die drei schlimmsten Einzelbilder. Hier wird die laengste ununterbrochene
 * Folge von Bildern ueber der Schwelle festgehalten, samt ihrer Aufteilung.
 * Gewertet wird nach GESAMTZEIT der Folge, nicht nach ihrer Laenge: zwanzig
 * Bilder zu 60 ms wiegen schwerer als fuenfzig zu 35. */
/* Schwelle auf `zeichnen` ALLEIN, nicht auf die Bildzeit -- diese Messung
 * sitzt im Zeichenblock und sieht nichts anderes. Auf der V1200 liegt
 * `zeichnen` im Mittel bei 8,8 ms; ein Bild, dessen Zeichnen allein ueber
 * 20 ms braucht, ist dort sicher ein Ruckler. */
enum { ZOD_ZAEH_US = 20000 };

static unsigned long zod_zaeh_sec[RSEC_MAX], zod_zaeh_sum, zod_zaeh_n, zod_zaeh_ab, zod_zaeh_rot;
static unsigned long zod_zaeh_eff, zod_zaeh_bc, zod_zaeh_bp, zod_zaeh_br;
static unsigned long zod_zaeh_frei, zod_zaeh_verw, zod_zaeh_weg;
static unsigned long zod_best_sec[RSEC_MAX], zod_best_sum, zod_best_n, zod_best_ab, zod_best_rot;
static unsigned long zod_best_eff, zod_best_bc, zod_best_bp, zod_best_br;
static unsigned long zod_best_frei, zod_best_verw, zod_best_weg;

static void zod_zaeh_abschluss(void)
{
	if(zod_zaeh_sum > zod_best_sum)
	{
		zod_best_sum = zod_zaeh_sum;
		zod_best_n   = zod_zaeh_n;
		zod_best_ab  = zod_zaeh_ab;
		zod_best_rot = zod_zaeh_rot;
		zod_best_eff = zod_zaeh_eff;
		zod_best_bc  = zod_zaeh_bc;
		zod_best_bp  = zod_zaeh_bp;
		zod_best_br  = zod_zaeh_br;
		zod_best_frei = zod_zaeh_frei;
		zod_best_verw = zod_zaeh_verw;
		zod_best_weg  = zod_zaeh_weg;

		for(int i = 0; i < RSEC_MAX; i++) zod_best_sec[i] = zod_zaeh_sec[i];
	}

	zod_zaeh_sum = 0;
	zod_zaeh_n   = 0;
	zod_zaeh_rot = 0;
	zod_zaeh_eff = 0;
	zod_zaeh_bc  = 0;
	zod_zaeh_bp  = 0;
	zod_zaeh_br  = 0;
	zod_zaeh_frei = 0;
	zod_zaeh_verw = 0;
	zod_zaeh_weg  = 0;

	for(int i = 0; i < RSEC_MAX; i++) zod_zaeh_sec[i] = 0;
}

static void zod_worst_note(void)
{
	unsigned long d[RSEC_MAX], summe = 0;

	for(int i = 0; i < RSEC_MAX; i++)
	{
		d[i] = zod_rsec[i] - zod_rsec_prev[i];
		summe += d[i];
	}

	/* Ladebilder und das Einblenden des Startbilds ausschliessen.
	 *
	 * Ohne das belegen sie auf dem Host ALLE DREI Plaetze: DoSplash blittet
	 * das Vollbild und baut je Bild eine Schriftflaeche fuer "LOADING x%".
	 * Gesucht ist aber der Einbruch IM SPIEL. Der Agent hatte genau davor
	 * gewarnt ("sonst gewinnt regelmaessig das Ladebild").
	 *
	 * Massstab ist der Anteil, nicht eine feste Bildnummer -- eine Bildnummer
	 * waere auf jeder Maschine eine andere. */
	if(d[RSEC_LADEN] + d[RSEC_SPLASH] > summe / 2) return;

	/* Die erste Sekunde ebenfalls aussen vor: Dort liegt der erste
	 * Kartenaufbau (RenderMap, auf dem Host allein 4400 us), und der ist eine
	 * einmalige Startkost -- eine andere Frage als der Einbruch im Spiel.
	 * 60 Bilder sind auf jeder Maschine ungefaehr eine Sekunde. */
	if(zod_acc_frames < 60) return;

	/* Folge langsamer Bilder fortschreiben bzw. abschliessen. */
	if(summe >= (unsigned long)ZOD_ZAEH_US)
	{
		if(!zod_zaeh_n) zod_zaeh_ab = (unsigned long)zod_acc_frames;

		zod_zaeh_n++;
		zod_zaeh_sum += summe;
		zod_zaeh_rot += zod_roto_frame_ticks_get();
		zod_zaeh_eff += zod_eff_frame;
		zod_zaeh_bc  += zod_effblit_calls;
		zod_zaeh_bp  += zod_effblit_px;
		zod_zaeh_br  += zod_effblit_rows;
		{
			unsigned long f, v, w;

			zod_roto_frame_extra_get(&f, &v, &w);
			zod_zaeh_frei += f;
			zod_zaeh_verw += v;
			zod_zaeh_weg  += w;
		}

		for(int i = 0; i < RSEC_MAX; i++) zod_zaeh_sec[i] += d[i];
	}
	else if(zod_zaeh_n)
		zod_zaeh_abschluss();

	/* Einsortieren, groesstes zuerst. */
	for(int p = 0; p < ZOD_WORST; p++)
	{
		if(summe <= zod_worst_sum[p]) continue;

		for(int q = ZOD_WORST - 1; q > p; q--)
		{
			zod_worst_sum[q] = zod_worst_sum[q-1];
			zod_worst_nr[q]  = zod_worst_nr[q-1];
			zod_worst_eff[q] = zod_worst_eff[q-1];
			zod_worst_roto[q]= zod_worst_roto[q-1];
			zod_worst_rotot[q]=zod_worst_rotot[q-1];
			zod_worst_frei[q] = zod_worst_frei[q-1];
			zod_worst_verw[q] = zod_worst_verw[q-1];
			zod_worst_weg[q]  = zod_worst_weg[q-1];

			for(int i = 0; i < RSEC_MAX; i++)
				zod_worst_sec[q][i] = zod_worst_sec[q-1][i];
		}

		zod_worst_sum[p]  = summe;
		zod_worst_nr[p]   = (unsigned long)zod_acc_frames;
		zod_worst_eff[p]  = zod_eff_frame;
		zod_worst_roto[p] = zod_roto_frame_made_get();
		zod_worst_rotot[p]= zod_roto_frame_ticks_get();
		zod_roto_frame_extra_get(&zod_worst_frei[p], &zod_worst_verw[p],
		                         &zod_worst_weg[p]);

		for(int i = 0; i < RSEC_MAX; i++) zod_worst_sec[p][i] = d[i];

		break;
	}
}

extern "C" void zod_worst_report(void)
{
	const unsigned long freq = zod_fineclock_freq();

	if(freq < 1000) return;

	for(int p = 0; p < ZOD_WORST; p++)
	{
		if(!zod_worst_sum[p]) continue;

		/* Erst teilen, dann malnehmen -- sonst Ueberlauf auf 32 Bit. */
		ZLOG("  Schlimmstes Bild %ld: #%ld  %ld us  (%ld Effekte, %ld Drehungen, "
		     "davon %ld us reines Rechnen)\n",
		     (long)(p + 1), (long)zod_worst_nr[p],
		     (long)((zod_worst_sum[p] * 1000UL) / (freq / 1000UL)),
		     (long)zod_worst_eff[p], (long)zod_worst_roto[p],
		     (long)((zod_worst_rotot[p] * 1000UL) / (freq / 1000UL)));

		ZLOG("      Drehweg: %ld us rechnen, %ld us freigeben (%ld Stueck), "
		     "%ld us verwalten\n",
		     (long)((zod_worst_rotot[p] * 1000UL) / (freq / 1000UL)),
		     (long)((zod_worst_frei[p]  * 1000UL) / (freq / 1000UL)),
		     (long)zod_worst_weg[p],
		     (long)((zod_worst_verw[p]  * 1000UL) / (freq / 1000UL)));

		for(int i = 0; i < RSEC_MAX; i++)
		{
			const unsigned long us = (zod_worst_sec[p][i] * 1000UL) / (freq / 1000UL);

			/* Nur nennen, was ins Gewicht faellt -- unter 500 us ist bei
			 * einem Bild von 100 ms plus Rauschen. */
			if(us >= 500)
				ZLOG("      %s = %ld us\n", zod_rsec_name[i], (long)us);
		}
	}

	/* Eine laufende Folge am Ende noch abschliessen, sonst faellt die
	 * letzte -- und womoeglich schlimmste -- unter den Tisch. */
	zod_zaeh_abschluss();

	if(zod_best_n)
	{
		const unsigned long ges = (zod_best_sum * 1000UL) / (freq / 1000UL);

		const unsigned long rot = (zod_best_rot * 1000UL) / (freq / 1000UL);

		ZLOG("  Zaehste Folge (zeichnen > 20 ms): %ld Bilder ab #%ld, "
		     "zusammen %ld us (im Mittel %ld us je Bild); "
		     "davon %ld us reines Drehen/Zoomen\n",
		     (long)zod_best_n, (long)zod_best_ab, (long)ges,
		     (long)(ges / zod_best_n), (long)rot);

		/* Je Bild, damit es ohne Kopfrechnen deutbar ist. Und die Stueckzahl
		 * NEBEN der Zeit -- sonst sagt die Zeit nicht, woran sie haengt. */
		ZLOG("      je Bild: %ld Effekte, %ld Blits, %ld Blitzeilen, "
		     "%ld Blitpunkte\n",
		     (long)(zod_best_eff / zod_best_n),
		     (long)(zod_best_bc  / zod_best_n),
		     (long)(zod_best_br  / zod_best_n),
		     (long)(zod_best_bp  / zod_best_n));

		ZLOG("      Drehweg je Bild: %ld us rechnen, %ld us freigeben "
		     "(%ld Stueck), %ld us verwalten\n",
		     (long)(rot / zod_best_n),
		     (long)(((zod_best_frei * 1000UL) / (freq / 1000UL)) / zod_best_n),
		     (long)(zod_best_weg / zod_best_n),
		     (long)(((zod_best_verw * 1000UL) / (freq / 1000UL)) / zod_best_n));

		for(int i = 0; i < RSEC_MAX; i++)
		{
			const unsigned long us = (zod_best_sec[i] * 1000UL) / (freq / 1000UL);

			if(us >= 500)
				ZLOG("      %s = %ld us\n", zod_rsec_name[i], (long)us);
		}
	}
}

static inline void zod_rsec_begin(void)
{
	if(!zod_time_measure) return;

	for(int i = 0; i < RSEC_MAX; i++) zod_rsec_prev[i] = zod_rsec[i];

	zod_rsec_mark = zod_fineclock_ticks();
}

static inline void zod_rsec_end(int slot)
{
	if(!zod_time_measure) return;

	const unsigned long jetzt = zod_fineclock_ticks();

	zod_rsec[slot] += jetzt - zod_rsec_mark;
	zod_rsec_mark = jetzt;
}

extern "C" void zod_render_sections_report(long frames)
{
	const unsigned long freq = zod_fineclock_freq();

	/* `freq / 1000` ist unten der Teiler -- unter 1000 waere er 0 und der
	 * Bericht endete mit Guru 80000005. Genau das ist mir in der Uhr-Sonde
	 * passiert (dort stand `freq / 1000000`, und die E-Clock hat nur
	 * 709379 Schritte). */
	if(freq < 1000 || frames <= 0) return;

	unsigned long summe = 0;

	for(int i = 0; i < RSEC_MAX; i++)
	{
		/* Erst teilen, dann malnehmen -- sonst laeuft es auf 32 Bit ueber
		 * (dieselbe Falle wie bei den Bildzeiten). */
		const unsigned long us = (zod_rsec[i] / frames) * 1000UL / (freq / 1000UL);

		summe += us;

		ZLOG("  zeichnen.%s = %ld us\n", zod_rsec_name[i], (long)us);
	}

	/* Die Summe gehoert ins Protokoll, nicht in meinen Kopf: Weicht sie vom
	 * gemeldeten `zeichnen` ab, liegt Arbeit ausserhalb jeder Klammer -- und
	 * genau die waere sonst unsichtbar. */
	ZLOG("  zeichnen.SUMME = %ld us (muss `zeichnen` oben entsprechen)\n",
	     (long)summe);

	/* Ohne diese Zeile ist jeder Posten oben nicht deutbar: Alle sind auf
	 * ALLE Bilder bezogen, gearbeitet wurde aber nur in den Spielbildern.
	 * Sind es deutlich weniger, sind die Posten entsprechend zu hoch. */
	ZLOG("  zeichnen.Spielbilder = %ld von %ld\n",
	     (long)zod_rsec_spielbilder, (long)frames);

	/* Wie stark die beiden Filterlisten wirklich filtern. Ohne diese Zeile
	 * ist `obj_vor` nicht deutbar: Ein Filter, der nichts aussortiert, sieht
	 * in der Zeitangabe genauso aus wie einer, der die Haelfte entfernt. */
	ZLOG("  zeichnen.Objektlisten = %ld gesamt, %ld mit Vorzeichnen, %ld mit Nachzeichnen\n",
	     (long)zod_rsec_olist_all, (long)zod_rsec_olist_pre, (long)zod_rsec_olist_post);

	/* Was die Aufteilung selbst kostet. Auf der V1200 braucht ein Blick auf
	 * die E-Clock **9882 ns** -- das 27-fache des Emulators und das
	 * 580-fache des Hosts. Bei RSEC_MAX+1 Marken steckt das in den Posten
	 * drin, und zwar je Posten einmal.
	 *
	 * Praktische Folge: Alles unterhalb von rund 20 us ist hier Rauschen.
	 * Ohne diese Zeile haette ich Posten wie `auswahl` (12 us) fuer eine
	 * Messung gehalten, obwohl sie fast nur den Zeitabruf enthalten. */
	{
		const unsigned long t0 = zod_fineclock_ticks();

		for(int i = 0; i < 1000; i++) zod_fineclock_ticks();

		const unsigned long ns = ((zod_fineclock_ticks() - t0) * 1000UL)
		                         / (freq / 1000UL);

		ZLOG("  zeichnen.Messkosten = %ld us je Bild (%ld Marken zu %ld ns)\n",
		     (long)((ns * (RSEC_MAX + 1)) / 1000L), (long)(RSEC_MAX + 1),
		     (long)ns);
	}
}

void zod_time_report(double &render, double &flip, double &process, double &socket, long &frames)
{
	const unsigned long freq = zod_fineclock_freq();

	/* Ohne feine Uhr ist hier nichts zu holen. Das MUSS auffallen -- ein
	 * stiller Messausfall hat in diesem Projekt schon dreimal einen Lauf
	 * wertlos gemacht, ohne dass er misslungen aussah. */
	if(!freq)
	{
		ZLOG("Zeitmessung: KEINE feine Uhr (timer.device fehlt) -- Werte ungueltig\n");

		render = flip = process = socket = 0.0;
		frames = zod_acc_frames;
		return;
	}

	render  = (double)zod_acc_render  / (double)freq;
	flip    = (double)zod_acc_flip    / (double)freq;
	process = (double)zod_acc_process / (double)freq;
	socket  = (double)zod_acc_socket  / (double)freq;
	frames  = zod_acc_frames;
}

void ZPlayer::Tick()
{
	unsigned long process_time = 0;
	unsigned long render_time = 0;
	unsigned long socket_time = 0;

	{
		ztime.UpdateTime();

		//check for tcp events
		if(zod_time_measure) socket_time = zod_fineclock_ticks();
		ProcessSocketEvents();

		/* SOFORT hier, noch VOR ProcessSDL: die Eingabeverarbeitung liest
		 * unit_limit_reached (Produktionsknoepfe). Stuende der Sammelpunkt
		 * hinter ProcessSDL, saehe ein Baubefehl im selben Bild einen um ein
		 * Bild veralteten Wert -- das waere eine Verhaltensaenderung. */
		FlushObjectAmount();

		if(zod_time_measure) socket_time = zod_fineclock_ticks() - socket_time;
		//client_socket.Process();

		//check for sdl events
		ProcessSDL();

		//process events
		ehandler.ProcessEvents();

		//do stuff
		if(zod_time_measure) process_time = zod_fineclock_ticks();
		ProcessGame();
		if(zod_time_measure) process_time = zod_fineclock_ticks() - process_time;

		//render
		if(zod_time_measure) render_time = zod_fineclock_ticks();
		RenderScreen();
		if(zod_time_measure) render_time = zod_fineclock_ticks() - render_time;

		/* Faelliger Rundenabschluss. ERST HIER, nach dem Zeichnen -- so
		 * laeuft die Explosion in den Sekunden davor zu Ende (siehe
		 * ProcessEndGame).
		 *
		 * Zwei Aufrufe, drei Teile: den Aufbruchsfilm loest ZeigeStatistik
		 * selbst aus, und nur bei "Continue". Er gehoert an die
		 * Auswertung des Knopfes, nicht hierher -- hier stuende er
		 * unabhaengig davon, wie der Spieler entschieden hat. */
		if(end_faellig > 0 && COMMON::current_time() >= end_faellig)
		{
			end_faellig = 0;

			SpieleEndFilme();
			ZeigeStatistik();
		}

		/* Vor zod_roto_frame_end -- das nullt die Bildzaehler. */
		if(zod_time_measure) zod_worst_note();

		zod_roto_frame_end();

		if(zod_time_measure)
		{
			zod_acc_socket  += socket_time;
			zod_acc_process += process_time;
			zod_acc_render  += render_time;
			zod_acc_frames++;
		}
	}
}

void ZPlayer::ProcessSocketEvents()
{
	char *message;
	int size;
	int pack_id;
	SocketHandler* shandler;
	int packets_processed = 0;
	double time_took;

	shandler = client_socket.GetHandler();

	if(!shandler) return;
	
	//not connected, free it up
	if(!shandler->Connected())
	{
		client_socket.ClearConnection();
		
		//event_list->push_back(new Event(OTHER_EVENT, DISCONNECT_EVENT, 0, nullptr, 0));
		ehandler.ProcessEvent(OTHER_EVENT, DISCONNECT_EVENT, nullptr, 0, 0);
	}
	
	else if(shandler->DoRecv())
	{
		//time_took = current_time();
		/*
		while(shandler->DoProcess(&message, &size, &pack_id))
		{
			ehandler.ProcessEvent(TCP_EVENT, pack_id, message, size, 0);
			//event_list->push_back(new Event(TCP_EVENT, pack_id, 0, message, size));
 			//ZLOG("ClientSocket::Process:got packet id:%d\n", pack_id);
			packets_processed++;
		}
		*/
		while(shandler->DoFastProcess(&message, &size, &pack_id))
		{
			ehandler.ProcessEvent(TCP_EVENT, pack_id, message, size, 0);
			//packets_processed++;
		}
		shandler->ResetFastProcess();
		//time_took = current_time() - time_took;
		//ZLOG("packets_processed:%05d \t time_took:%lf\n", packets_processed, time_took);
	}

	/*
	time_took = current_time();
	while(shandler->PacketAvailable() && shandler->GetPacket(&message, &size, &pack_id))
	{
		//event_list->push_back(new Event(TCP_EVENT, pack_id, 0, message, size));	
		ehandler.ProcessEvent(TCP_EVENT, pack_id, message, size, 0);
		packets_processed++;
	}
	time_took = current_time() - time_took;
	if(packets_processed > 0) ZLOG("packets_processed:%05d \t time_took:%lf\n", packets_processed, time_took);
	*/
}

void ZPlayer::ProcessGame()
{
	double &the_time = ztime.ztime;
	
	//zmap.DoEffects(the_time);

	ZMusicEngine::Process(object_list, zmap, our_team, fort_ref_id);

	//fort under attack etc?
	ProcessVerbalWarnings();

	cursor.Process(the_time);
	Pcursor.Process(the_time);

    zcomp_msg.Process(COMMON::current_time());

	zhud.Process(the_time, object_list);
		
	/* Zeichenreihenfolge nach y. Einfuegesortierung statt std::sort:
	 *
	 * Die Liste ist zwischen zwei Bildern fast vollstaendig sortiert -- rund
	 * 88 % der Objekte (Steine, Kakteen, Gebaeude) bewegen sich nie, und die
	 * uebrigen ruecken je Bild um wenige Punkte. std::sort ist ein Introsort
	 * und braucht auch auf fast sortierten Daten rund n*log(n) Vergleiche
	 * (bei 393 Objekten etwa 3400, auf der grossen Karte 35 000);
	 * Einfuegesortierung braucht n plus die Zahl der Vertauschungen, hier
	 * also fast nur n.
	 *
	 * Nebenwirkung, und zwar eine erwuenschte: Einfuegesortierung ist STABIL.
	 * std::sort darf bei gleichem Schluessel die Reihenfolge von Bild zu Bild
	 * willkuerlich aendern -- bei gleich hohen Objekten ist das eine
	 * moegliche Flimmerquelle. */
	{
		vector<ZObject*> &pol = ols.prender_olist;

		for(size_t i = 1; i < pol.size(); i++)
		{
			ZObject *obj = pol[i];
			size_t j = i;

			while(j > 0 && sort_objects_func(obj, pol[j - 1]))
			{
				pol[j] = pol[j - 1];
				j--;
			}

			pol[j] = obj;
		}
	}

	/* Dieselbe Sortierung fuer die kleine Vorzeichnen-Liste. Sie ist eine
	 * Teilmenge, und derselbe Schluessel liefert deshalb genau die relative
	 * Reihenfolge, die sie in der grossen Liste haette -- die Ueberdeckung
	 * bleibt damit unveraendert. */
	{
		vector<ZObject*> *klein[2] = { &ols.prerender_olist, &ols.aftereffect_olist };

		for(int k = 0; k < 2; k++)
		{
			vector<ZObject*> &ppl = *klein[k];

			for(size_t i = 1; i < ppl.size(); i++)
			{
				ZObject *obj = ppl[i];
				size_t j = i;

				while(j > 0 && sort_objects_func(obj, ppl[j - 1]))
				{
					ppl[j] = ppl[j - 1];
					j--;
				}

				ppl[j] = obj;
			}
		}
	}

 	for(vector<ZObject*>::iterator i=ols.prender_olist.begin(); i!=ols.prender_olist.end(); i++)
	{
		(*i)->ProcessObject();
 		(*i)->Process();
	}

	//kill effects
	//
	//Ein `erase` JE Effekt schiebt den ganzen Rest des Vektors um einen Platz
	//nach vorn. Beim Tod eines Forts leben hier Hunderte Effekte, die
	//grossenteils gemeinsam ablaufen -- also O(n^2) Verschiebungen in EINEM
	//Bild, dazu n Freigaben. Auf dieser Werkzeugkette ist `free` kein
	//Ruecksprung, sondern ein Exec-Aufruf mit Semaphore und einem LINEAREN
	//Durchlauf der Blockliste (gemessen 88 672 lebende Bloecke), also der
	//teuerste Teil daran.
	//
	//Jetzt: ein Durchgang, der die Ueberlebenden nach vorn schreibt und die
	//Toten gleich freigibt, danach EINMAL abschneiden. Reihenfolge bleibt
	//erhalten, die Zahl der Freigaben ebenfalls -- nur das Verschieben faellt
	//weg.
	//
	//ACHTUNG, hier stand kurzzeitig `std::remove_if` mit anschliessendem
	//`delete` ueber den Schwanz. DAS WAERE EINE DOPPELTE FREIGABE GEWESEN:
	//remove_if laesst den Bereich hinter dem Rueckgabewert in einem
	//UNBESTIMMTEN Zustand -- bei Zeigern stehen dort Kopien der
	//UEBERLEBENDEN, nicht die entfernten. Aus [A, B(tot), C] wird [A, C, C];
	//ein delete auf den Schwanz haette C freigegeben, waehrend C noch in der
	//Liste steht. Wer remove_if auf einen Zeigerbehaelter anwendet, muss VOR
	//dem Verschieben freigeben.
	{
		size_t w = 0;

		for(size_t r = 0; r < effect_list.size(); r++)
		{
			if(effect_list[r]->KillMe()) { delete effect_list[r]; zod_eff_weg++; }
			else                         effect_list[w++] = effect_list[r];
		}

		effect_list.resize(w);

		zod_eff_frame = (unsigned long)effect_list.size();
		zod_eff_summe += zod_eff_frame;
		zod_eff_bilder++;

		if(zod_eff_frame > zod_eff_peak) zod_eff_peak = zod_eff_frame;
	}

	//process effects
	for(vector<ZEffect*>::iterator i=effect_list.begin(); i!=effect_list.end();i++)
	{
		(*i)->Process();

		if((*i)->GetEFlags().unit_particles)
			MissileObjectParticles((*i)->GetEFlags().x, (*i)->GetEFlags().y, (*i)->GetEFlags().unit_particles_radius, (*i)->GetEFlags().unit_particles_amount);
	}

	//the zeffects push new effects into this queue, so we need to move it to the real one
	{
		const unsigned long n = (unsigned long)new_effect_list.size();

		zod_eff_neu += n;

		if(n > zod_eff_neu_peak) zod_eff_neu_peak = n;
	}

	for(vector<ZEffect*>::iterator i=new_effect_list.begin(); i!=new_effect_list.end(); i++)
		effect_list.push_back(*i);
	new_effect_list.clear();

	//animals
	for(vector<ZObject*>::iterator i=bird_list.begin(); i!=bird_list.end();i++)
		(*i)->Process();

	//gui
	if(gui_window) 
	{
		if(gui_window->KillMe())
			DeleteCurrentGuiWindow();
		else
			gui_window->Process();
	}

	if(gui_factory_list) gui_factory_list->Process();

	//main menus
	{
		for(vector<ZGuiMainMenuBase*>::iterator i=gui_menu_list.begin();i!=gui_menu_list.end();)
		{
			if((*i)->KillMe())
			{
				delete *i;
				i = gui_menu_list.erase(i);
			}
			else
				++i;
		}

		for(vector<ZGuiMainMenuBase*>::iterator i=gui_menu_list.begin();i!=gui_menu_list.end();++i)
			(*i)->Process();
	}

	//placing a cannon?
	SetPlaceCannonCords();

	//scroll
	ProcessScroll();
	ProcessFocusCamerato();
}

void ZPlayer::ProcessVerbalWarnings()
{
	double &the_time = ztime.ztime;

	static double next_fort_under_attack_msg_time = 0;
	static double next_your_losing_msg_time = 0;

	if(!graphics_loaded) return;

	if(team_units_available[our_team] <= 0) return;

	//it in fort crazy mode and we need to redisplay the fort msg?
	if(ZMusicEngine::GetDangerLevel() == M_FORT && the_time >= next_fort_under_attack_msg_time)
	{
		next_fort_under_attack_msg_time = the_time + 10;

		ZSoundEngine::PlayWav(COMP_FORT_UNDER_ATTACK);
		zcomp_msg.DisplayMessage(FORT_MSG, fort_ref_id);

		//save space bar event
		AddSpaceBarEvent(SpaceBarEvent(fort_ref_id));
	}

	if(the_time >= next_your_losing_msg_time)
	{
		int our_unit_count;
		int next_worst_unit_count = 0;
		double our_territory_percentage;
		double next_worst_territory_percentage;

		our_unit_count = team_units_available[our_team];
		our_territory_percentage = team_zone_percentage[our_team];

		//find next worst
		for(int i=RED_TEAM;i<MAX_TEAM_TYPES;i++)
		{
			if(i == our_team) continue;
			if(team_units_available[i])
			{
				//first enemy?
				if(!next_worst_unit_count)
				{
					next_worst_unit_count = team_units_available[i];
					next_worst_territory_percentage = team_zone_percentage[i];

					continue;
				}
				
				if(next_worst_unit_count > team_units_available[i])
					next_worst_unit_count = team_units_available[i];

				if(next_worst_territory_percentage > team_zone_percentage[i])
					next_worst_territory_percentage = team_zone_percentage[i];
			}
		}

		our_unit_count *= 1.7;
		our_territory_percentage *= 1.7;

		if(next_worst_unit_count > our_unit_count && 
			next_worst_territory_percentage > our_territory_percentage)
		{
			next_your_losing_msg_time = the_time + 8;
		
			ZSoundEngine::PlayWav(COMP_YOUR_LOSING_0 + (rand() % MAX_COMP_LOSING_MESSAGES));
		}
	}
}

void ZPlayer::PlayBuildingSounds()
{
	bool do_play_radar = false;
	bool do_play_robot = false;
	bool do_play_vehicle = false;

	for(vector<ZObject*>::iterator i=object_list.begin(); i!=object_list.end(); i++)
	{
		unsigned char ot, oid;
		int x, y, w, h;

		(*i)->GetObjectID(ot, oid);

		if(ot != BUILDING_OBJECT) continue;
		if((*i)->IsDestroyed()) continue;

		//radar
		if(oid == RADAR && (*i)->GetOwner() != NULL_TEAM)
		{
			(*i)->GetCords(x, y);
			(*i)->GetDimensionsPixel(w, h);

			if(zmap.WithinView(x, y, w, h)) do_play_radar = true;
		}

		//robot factory
		if(oid == ROBOT_FACTORY || oid == VEHICLE_FACTORY)
		{
			(*i)->GetCords(x, y);
			(*i)->GetDimensionsPixel(w, h);

			if(zmap.WithinView(x, y, w, h))
				if((*i)->GetBuildState() != BUILDING_SELECT)
					if((*i)->GetOwner() != NULL_TEAM)
						do_play_robot = true;
		}

		//vehicle factory

	}

	if(do_play_radar)
		ZSoundEngine::RepeatWav(RADAR_SND);
	else
		ZSoundEngine::StopRepeatWav(RADAR_SND);

	if(do_play_robot)
		ZSoundEngine::RepeatWav(ROBOT_FACTORY_SND);
	else
		ZSoundEngine::StopRepeatWav(ROBOT_FACTORY_SND);
}

void ZPlayer::MissileObjectParticles(int x_, int y_, int radius, int particles)
{
	radius *= 0.8;

	for(vector<ZObject*>::iterator i=object_list.begin(); i!=object_list.end(); i++)
	{
		unsigned char ot, oid;
		int ox, oy;
		int w, h;

		(*i)->GetObjectID(ot, oid);

		if(!(ot == CANNON_OBJECT || ot == VEHICLE_OBJECT || ot == ROBOT_OBJECT)) continue;

		(*i)->GetCords(ox, oy);
		(*i)->GetDimensionsPixel(w, h);

		//if(obj.loc.x > x + width_pix) return false;
		//if(obj.loc.x + obj.width_pix < x) return false;
		//if(obj.loc.y > y + height_pix) return false;
		//if(obj.loc.y + obj.height_pix < y) return false;

		if(ox > x_ + radius) continue;
		if(ox + w < x_ - radius) continue;
		if(oy > y_ + radius) continue;
		if(oy + h < y_ - radius) continue;

		//if(ox + w > x_ - radius) continue;
		//if(oy + h > y_ - radius) continue;
		//if(ox < x_ + radius) continue;
		//if(oy < y_ + radius) continue;

		int particle_amount = 14 + (rand() % particles);

		if(ot == ROBOT_OBJECT)
			particle_amount /= 2;

		for(int i=0;i<particle_amount;i++)
			new_effect_list.push_back((ZEffect*)(new EUnitParticle(&ztime, ox + (rand() % w), oy + (rand() % h))));
	}
}

void ZPlayer::RenderScreen()
{
	double &the_time = ztime.ztime;

	zod_rsec_begin();

	/* Ladebilder gehen einen voellig anderen Weg: Der ganze Block unten
	 * faellt aus, und DoSplash baut je Bild eine neue Schriftflaeche fuer
	 * "LOADING x%". Beides in denselben Topf zu werfen ergaebe einen
	 * Mittelwert aus zwei Zustaenden -- er sah aus wie 108 us Splashkosten
	 * im Spiel, und das waere frei erfunden gewesen. */
	const bool im_spiel = (graphics_loaded && zmap.Loaded());

	if(im_spiel)
	{
		/* Wo im Ausschnitt liegt ueberhaupt Karte? Alles, was zeichnet, muss
		 * sich darauf beschraenken -- ist die Karte kleiner als der
		 * Ausschnitt, bleibt aussen schwarzer Rand ohne Kartenpunkte.
		 *
		 * Einmal je Bild gesetzt, nicht in ZMap: Es gibt DREI ZMap-Instanzen
		 * (Server, Spieler, Bot), und nur die des Spielers beschreibt den
		 * Schirm. Ein Setzer in ZMap wuerde von den beiden anderen
		 * ueberschrieben -- still und nur manchmal. */
		{
			int fenster_sx, fenster_sy;

			zmap.GetViewShift(fenster_sx, fenster_sy);
			ZSDL_Surface::SetMapWindow(-fenster_sx, -fenster_sy,
			                           zmap.GetMapBasics().width  * 16,
			                           zmap.GetMapBasics().height * 16);
		}

		//render base
		//we do not keep track if the image is in the map area in opengl
		//so we just render it all full then put the hud back over it
		if(splash_fade >= 5 || use_opengl)
			zhud.ReRenderAll();

		zod_rsec_end(RSEC_HUD_NEU);

		zmap.DoRender(screen);

		zod_rsec_end(RSEC_KARTE);

		zmap.DoEffects(the_time, screen);

		zod_rsec_end(RSEC_KARTENEFFEKTE);

		RenderSmallMapFiller();

		zod_rsec_end(RSEC_FUELLER);

		//draw zones
		zmap.DoZoneEffects(the_time, screen);

		zod_rsec_end(RSEC_ZONEN);

		//render objects (teilt sich selbst weiter auf)
		RenderObjects();

		//mouse stuff
		RenderMouse();
		RenderPreviousCursor();

		zod_rsec_end(RSEC_MAUS);

		//render gui?
		RenderGUI();

		//place cannon?
		RenderPlaceCannon();

		//a font test
		//SDL_Surface *surface = ZFontEngine::GetFont(SMALL_WHITE_FONT).Render("Light * The quick brown fox jumps over the lazy dog 12:34:56");
		//SDL_Surface *surface = ZFontEngine::GetFont(GREEN_BUILDING_FONT).Render("Light * THE QUICK BROWN FOX JUMPED OVER THE LAZY DOG 10:34 +:++");
		//if(surface)
		//{
		//	SDL_BlitSurface( surface, nullptr, screen, nullptr);
		//	SDL_FreeSurface(surface);
		//}

		//vote
		zvote.DoRender(zmap);

		//comp messages
		zcomp_msg.DoRender(zmap, screen);

		//render news
		RenderNews();

		//render a main menu like login
		if(active_menu) active_menu->DoRender(zmap, screen);

		zod_rsec_end(RSEC_GUI);

		//(almost)last because opengl uses it to render over overflows
		zhud.DoRender(screen, init_w, init_h);

		zod_rsec_end(RSEC_HUD);

		//Bildrate oben links, nach dem HUD damit nichts sie ueberdeckt
		if(fps_an) RenderFps();

		zod_rsec_end(RSEC_FPS);

		//main menus go over everything
		RenderMainMenu();

		zod_rsec_end(RSEC_HAUPTMENUE);

		//render cursor
		if(!disable_zcursor) 
		{
			cursor.Render(zmap, screen, mouse_x, mouse_y);
			//if(mouse_x > screen->w - (HUD_WIDTH + 16) || mouse_y > screen->h - (HUD_HEIGHT + 16))
			/* Nur auffrischen, wenn sich der Zeiger auch wirklich bewegt hat.
			 * Vorher genuegte es, dass er im Randstreifen LAG: ein still
			 * abgelegter Mauszeiger kostete dann in jedem Bild einen
			 * vollstaendigen HUD-Neuaufbau. */
			if(mouse_x > init_w - (HUD_WIDTH + 16) || mouse_y > init_h - (HUD_HEIGHT + 16))
			{
				if(mouse_x != hud_mouse_x || mouse_y != hud_mouse_y)
				{
					hud_mouse_x = mouse_x;
					hud_mouse_y = mouse_y;
					zhud.ReRenderAll();
				}
			}
		}

		zod_rsec_end(RSEC_ZEIGER);

		//place some sounds
		PlayBuildingSounds();

		zod_rsec_end(RSEC_KLAENGE);
	}

	DoSplash();

	/* Danach folgt nur noch das Umschalten, und das wird getrennt gefuehrt.
	 * Mit dieser Marke ist `zeichnen` vollstaendig aufgeteilt: Die Summe der
	 * Abschnitte muss den gemeldeten Posten ergeben. Ein Rest bedeutet, dass
	 * etwas ausserhalb jeder Klammer liegt -- dann geht die Suche dort
	 * weiter, statt die vorhandenen Zahlen zu deuten. */
	zod_rsec_end(im_spiel ? RSEC_SPLASH : RSEC_LADEN);

	if(im_spiel) zod_rsec_spielbilder++;

	if(use_opengl)
		SDL_GL_SwapBuffers();
	else
	{
		//Das Umschalten wartet bei SDL_DOUBLEBUF auf den Strahlruecklauf.
		//Getrennt fuehren, sonst erschiene diese Wartezeit als Zeichenarbeit
		//und jede Messung der Renderarbeit waere wertlos.
		unsigned long flip_start = zod_time_measuring() ? zod_fineclock_ticks() : 0;

		SDL_Flip(screen);

		/* ZOD_SHOT=<datei>:<bildnummer> -- ein einzelnes Bild als BMP
		 * sichern. Gedacht fuer die Fehlersuche an der Darstellung: Alle
		 * automatischen Laeufe dieses Projekts sind bildschirmlos und
		 * belegen deshalb nie, wie es tatsaechlich AUSSIEHT. Mit dem
		 * Dummy-Treiber von SDL wird trotzdem in eine echte Flaeche
		 * gezeichnet, die sich hier abgreifen laesst. */
		{
			static int shot_done = 0;
			static long shot_frame = -1;
			static const char *shot_file = 0;

			if(!shot_done && shot_frame < 0)
			{
				const char *e = getenv("ZOD_SHOT");

#ifdef __amigaos__
				/* Unter libnix sieht getenv die per SetEnv gesetzten
				 * Variablen nicht -- dafuer gibt es GetVar der
				 * dos.library (dieselbe Falle wie bei ZOD_LOG). */
				static char amiga_var[256];

				if((!e || !*e) &&
				   GetVar((CONST_STRPTR)"ZOD_SHOT", (STRPTR)amiga_var,
				          sizeof(amiga_var), 0) > 0)
					e = amiga_var;
#endif

				if(e)
				{
					static char buf[256];

					snprintf(buf, sizeof(buf), "%s", e);

					char *c = strrchr(buf, ':');

					shot_frame = c ? atol(c + 1) : 300;
					if(c) *c = 0;
					shot_file = buf;
				}
				else shot_done = 1;
			}

			if(!shot_done && shot_file && zod_acc_frames >= shot_frame)
			{
				SDL_SaveBMP(screen, shot_file);
				ZLOG("Bildschirmfoto nach %ld Bildern: %s\n", (long)zod_acc_frames, shot_file);
				shot_done = 1;
			}
		}

		if(zod_time_measuring()) zod_time_add_flip(zod_fineclock_ticks() - flip_start);
	}
}

/* Den Bereich schwaerzen, den die Karte nicht ausfuellt.
 *
 * Diese Funktion gab es schon -- sie nahm aber an, die Karte beginne LINKS
 * BUENDIG (`box.x = map_w`). Seit ZMap::ClampShift eine zu schmale Karte
 * ZENTRIERT (shift_x wird dann negativ), liegt der Rand je zur Haelfte links
 * und rechts, und die alte Fassung hat den frisch geblitteten Hintergrund
 * rechts wieder uebermalt.
 *
 * Gefunden nicht durch Lesen, sondern am Bildschirmfoto: die Kartenspalten
 * 946..1023 waren schwarz, obwohl der Blit nachweislich 1024 Punkte breit
 * nach x=78 ging (mit einer Sonde auf das von SDL_BlitSurface
 * zurueckgeschriebene Rechteck belegt).
 *
 * Gerechnet wird ueber die Schirmlage des ersten Kartenpunktes. Bei
 * gewoehnlichem Rollen ist shift >= 0, `karte_x0` also <= 0 und
 * `karte_x0 + map_w` >= view_w -- dann faellt wie bisher kein einziger
 * Kasten an. */
#ifdef __amigaos__
/* port/amiga/sdl_video.cpp -- Schmutzspur des VORIGEN Bildes */
extern "C" int zod_dirty_partial_ok(void);
extern "C" int zod_dirty_prev_runs(int blockzeile, short *x0, short *x1, int max);
extern "C" int zod_dirty_block_shift(void);
extern "C" int zod_dirty_grid_h(void);

/* Gegenprobe, dass der Filter greift -- und dass er nicht ALLES wegfiltert:
 * ohne Stueckzahl ist ein wirkungsloser Filter von einem wirksamen nicht zu
 * unterscheiden, und ein zu scharfer faellt nur am Bildschirm auf. */
static unsigned long zod_fueller_ganz = 0, zod_fueller_spar = 0;

extern "C" void zod_fueller_report(void)
{
	if(zod_fueller_ganz + zod_fueller_spar)
		ZLOG("  Schwarzer Rand: %ld mal gefuellt, %ld mal gespart\n",
		     (long)zod_fueller_ganz, (long)zod_fueller_spar);
}

/* Hat im VORIGEN Bild irgendetwas in das Rechteck x0..x1, y0..y1 gezeichnet?
 *
 * Die Zeilen MUESSEN mitgeprueft werden: fuer die waagerechten Balken ist der
 * x-Bereich die ganze Breite, und die ist praktisch immer schmutzig -- eine
 * Pruefung nur ueber x waere dort wertlos. */
static bool zod_rand_beruehrt(int x0, int x1, int y0, int y1)
{
	const int schub = zod_dirty_block_shift();
	const int bz_max = zod_dirty_grid_h();

	int bz_von = y0 >> schub;
	int bz_bis = y1 >> schub;

	if(bz_von < 0) bz_von = 0;
	if(bz_bis >= bz_max) bz_bis = bz_max - 1;

	for(int bz = bz_von; bz <= bz_bis; bz++)
	{
		short lx0[32], lx1[32];
		const int n = zod_dirty_prev_runs(bz, lx0, lx1, 32);

		for(int i = 0; i < n; i++)
			if(lx1[i] >= x0 && lx0[i] <= x1) return true;
	}

	return false;
}
#endif

void ZPlayer::RenderSmallMapFiller()
{
	SDL_Rect box;
	int sx, sy;

	const int map_w = zmap.GetMapBasics().width  * 16;
	const int map_h = zmap.GetMapBasics().height * 16;
	const int view_w = init_w - HUD_WIDTH;
	const int view_h = init_h - HUD_HEIGHT;

	zmap.GetViewShift(sx, sy);

	/* Schirmlage des Kartenpunktes 0 (das Ziel des Kartenblits). */
	const int karte_x0 = -sx;
	const int karte_y0 = -sy;

#ifdef __amigaos__
	/* Die schwarzen Balken aendern sich NIE, solange Verschiebung, Ausschnitt
	 * und Kartengroesse gleich bleiben. Sie trotzdem in jedem Bild neu zu
	 * fuellen kostete auf der V1200 bei 1280x720 gemessen 2279 us je Bild --
	 * und schlimmer: die Fuellung vermerkt sich in der Schmutzspur, also
	 * wurde derselbe unveraenderte schwarze Rand auch noch in jedem Bild in
	 * den Schirm kopiert.
	 *
	 * Neu gefuellt wird deshalb nur, wenn sich die Geometrie geaendert hat
	 * ODER im VORIGEN Bild etwas in den Rand hineingezeichnet hat (der
	 * Mauszeiger ist der Regelfall). Die Teilwiederherstellung der Karte
	 * klammert den Rand aus, es holt ihn also sonst niemand zurueck. */
	{
		static int letzte_sx = 0x7fffffff, letzte_sy = 0x7fffffff;
		static int letzte_vw = -1, letzte_vh = -1;
		static int letzte_mw = -1, letzte_mh = -1;

		const bool geometrie_neu = (sx != letzte_sx || sy != letzte_sy ||
		                            view_w != letzte_vw || view_h != letzte_vh ||
		                            map_w != letzte_mw || map_h != letzte_mh);

		letzte_sx = sx; letzte_sy = sy;
		letzte_vw = view_w; letzte_vh = view_h;
		letzte_mw = map_w; letzte_mh = map_h;

		if(!geometrie_neu && zod_dirty_partial_ok())
		{
			const bool links  = (karte_x0 > 0) &&
			                    zod_rand_beruehrt(0, karte_x0 - 1, 0, view_h - 1);
			const bool rechts = (karte_x0 + map_w < view_w) &&
			                    zod_rand_beruehrt(karte_x0 + map_w, view_w - 1,
			                                      0, view_h - 1);
			const bool oben   = (karte_y0 > 0) &&
			                    zod_rand_beruehrt(0, view_w - 1, 0, karte_y0 - 1);
			const bool unten  = (karte_y0 + map_h < view_h) &&
			                    zod_rand_beruehrt(0, view_w - 1,
			                                      karte_y0 + map_h, view_h - 1);

			if(!links && !rechts && !oben && !unten)
			{
				zod_fueller_spar++;
				return;
			}
		}
	}
#endif

#ifdef __amigaos__
	zod_fueller_ganz++;
#endif

	box.y = 0;
	box.h = (Uint16)view_h;

	if(karte_x0 > 0)
	{
		box.x = 0;
		box.w = (Uint16)karte_x0;
		ZSDL_FillRect(&box, 0, 0, 0);
	}

	if(karte_x0 + map_w < view_w)
	{
		box.x = (short)(karte_x0 + map_w);
		box.w = (Uint16)(view_w - (karte_x0 + map_w));
		ZSDL_FillRect(&box, 0, 0, 0);
	}

	box.x = 0;
	box.w = (Uint16)view_w;

	if(karte_y0 > 0)
	{
		box.y = 0;
		box.h = (Uint16)karte_y0;
		ZSDL_FillRect(&box, 0, 0, 0);
	}

	if(karte_y0 + map_h < view_h)
	{
		box.y = (short)(karte_y0 + map_h);
		box.h = (Uint16)(view_h - (karte_y0 + map_h));
		ZSDL_FillRect(&box, 0, 0, 0);
	}
}

void ZPlayer::RenderMouse()
{
	double &the_time = ztime.ztime;
	static int draw_shift = 0;
	static double next_shift_time = 0;
	const double shift_tick = 0.1;
	SDL_Rect dim;
	SDL_Rect to_rect;
	int shift_x, shift_y;
	int x, y;

	if(lbutton.down && !lbutton.started_over_hud && !lbutton.started_over_gui)
	{
		//move forward
		if(the_time > next_shift_time)
		{
			draw_shift++;
			if(draw_shift>=4) draw_shift = 0;

			next_shift_time = the_time + shift_tick;
		}

		zmap.GetViewShift(shift_x, shift_y);
		x = lbutton.map_x - shift_x;
		y = lbutton.map_y - shift_y;

		if(x < mouse_x) 
		{
			dim.x = x;
			dim.w = mouse_x - x;
		}
		else 
		{
			dim.x = mouse_x;
			dim.w = x - mouse_x;
		}

		if(y < mouse_y)
		{
			dim.y = y;
			dim.h = mouse_y - y;
		}
		else 
		{
			dim.y = mouse_y;
			dim.h = y - mouse_y;
		}

		//set max distants we can travel while drawing base on the actual screen
		//int max_x = screen->w - HUD_WIDTH - 1;
		//int max_y = screen->h - HUD_HEIGHT - 1;
		int max_x = init_w - HUD_WIDTH - 1;
		int max_y = init_h - HUD_HEIGHT - 1;
		int start_x, start_y;

		if(dim.y < 0) start_y = 0;
		else start_y = dim.y;

		if(dim.x < 0) start_x = 0;
		else start_x = dim.x;

		//to optimise, force use the var - but set it to the orig if it is within the max's
		if(dim.w+dim.x < max_x) max_x = dim.w+dim.x;
		if(dim.h+dim.y < max_y) max_y = dim.h+dim.y;

		to_rect.y = dim.y;
		//if(to_rect.y < screen->h - HUD_HEIGHT - 1 && to_rect.y > 0)
		if(to_rect.y < init_h - HUD_HEIGHT - 1 && to_rect.y > 0)
		for(to_rect.x=start_x+(4-draw_shift);to_rect.x<max_x;to_rect.x+=4)
			selection_img[our_team].BlitSurface(nullptr, &to_rect);
			//SDL_BlitSurface( selection_img[our_team], nullptr, screen, &to_rect);
		
		to_rect.y = dim.y + dim.h;
		//if(to_rect.y < screen->h - HUD_HEIGHT - 1 && to_rect.y > 0)
		if(to_rect.y < init_h - HUD_HEIGHT - 1 && to_rect.y > 0)
		for(to_rect.x=start_x+draw_shift;to_rect.x<max_x;to_rect.x+=4)
			selection_img[our_team].BlitSurface(nullptr, &to_rect);
			//SDL_BlitSurface( selection_img[our_team], nullptr, screen, &to_rect);

		to_rect.x = dim.x;
		//if(to_rect.x < screen->w - HUD_WIDTH - 1 && to_rect.x > 0)
		if(to_rect.x < init_w - HUD_WIDTH - 1 && to_rect.x > 0)
		for(to_rect.y=start_y+draw_shift;to_rect.y<max_y;to_rect.y+=4)
			selection_img[our_team].BlitSurface(nullptr, &to_rect);
			//SDL_BlitSurface( selection_img[our_team], nullptr, screen, &to_rect);

		to_rect.x = dim.x + dim.w;
		//if(to_rect.x < screen->w - HUD_WIDTH - 1 && to_rect.x > 0)
		if(to_rect.x < init_w - HUD_WIDTH - 1 && to_rect.x > 0)
		for(to_rect.y=start_y+(4-draw_shift);to_rect.y<max_y;to_rect.y+=4)
			selection_img[our_team].BlitSurface(nullptr, &to_rect);
			//SDL_BlitSurface( selection_img[our_team], nullptr, screen, &to_rect);

		//this for now, till we get the official selection box system up
		//draw_box(screen, dim, team_color[our_team], screen->w - HUD_WIDTH, screen->h - HUD_HEIGHT);
	}
}

/* Zaehler, definiert in zobject.cpp */
extern "C" unsigned long zod_lauf_knoepfe, zod_lauf_del, zod_lauf_elem;
extern "C" unsigned long zod_lauf_elem_knoepfe, zod_lauf_elem_del;

void ZPlayer::ReSetupButtons()
{
	vector<ZObject*>::iterator i;

	zod_lauf_knoepfe++;
	zod_lauf_elem += (unsigned long)object_list.size();
	zod_lauf_elem_knoepfe += (unsigned long)object_list.size();
	bool robot_available = false;
	bool vehicle_available = false;
	bool gun_available = false;
	bool building_available = false;
	bool change_made = false;
	int tbut;

	//check button availability
	if(our_team != NULL_TEAM)
		for(i=object_list.begin();i!=object_list.end();i++)
			if((*i)->GetOwner() == our_team)
		{
			unsigned char ot, oid;

			(*i)->GetObjectID(ot, oid);

			switch(ot)
			{
			case BUILDING_OBJECT:
				building_available = true;
				break;
			case CANNON_OBJECT:
				gun_available = true;
				break;
			case VEHICLE_OBJECT:
				vehicle_available = true;
				break;
			case ROBOT_OBJECT:
				robot_available = true;
				break;
			}
		}

	//set button availability
	tbut = B_BUTTON;
	if(building_available)
	{
		if(zhud.GetButton(tbut).CurrentState() == B_INACTIVE)
		{
			zhud.GetButton(tbut).SetState(B_ACTIVE);
			change_made = true;
		}
	}
	else
	{
		if(zhud.GetButton(tbut).CurrentState() != B_INACTIVE)
		{
			zhud.GetButton(tbut).SetState(B_INACTIVE);
			change_made = true;
		}
	}

	tbut = G_BUTTON;
	if(gun_available)
	{
		if(zhud.GetButton(tbut).CurrentState() == B_INACTIVE)
		{
			zhud.GetButton(tbut).SetState(B_ACTIVE);
			change_made = true;
		}
	}
	else
	{
		if(zhud.GetButton(tbut).CurrentState() != B_INACTIVE)
		{
			zhud.GetButton(tbut).SetState(B_INACTIVE);
			change_made = true;
		}
	}

	tbut = V_BUTTON;
	if(vehicle_available)
	{
		if(zhud.GetButton(tbut).CurrentState() == B_INACTIVE)
		{
			zhud.GetButton(tbut).SetState(B_ACTIVE);
			change_made = true;
		}
	}
	else
	{
		if(zhud.GetButton(tbut).CurrentState() != B_INACTIVE)
		{
			zhud.GetButton(tbut).SetState(B_INACTIVE);
			change_made = true;
		}
	}

	tbut = R_BUTTON;
	if(robot_available)
	{
		if(zhud.GetButton(tbut).CurrentState() == B_INACTIVE)
		{
			zhud.GetButton(tbut).SetState(B_ACTIVE);
			change_made = true;
		}
	}
	else
	{
		if(zhud.GetButton(tbut).CurrentState() != B_INACTIVE)
		{
			zhud.GetButton(tbut).SetState(B_INACTIVE);
			change_made = true;
		}
	}

	if(change_made)
		zhud.ReRenderAll();
}

void ZPlayer::HandleButton(hud_buttons button)
{
	switch(button)
	{
	case A_BUTTON: A_Button(); break;
	case B_BUTTON: B_Button(); break;
	case D_BUTTON: D_Button(); break;
	case G_BUTTON: G_Button(); break;
	case MENU_BUTTON: Menu_Button(); break;
	case R_BUTTON: R_Button(); break;
	case T_BUTTON: T_Button(); break;
	case V_BUTTON: V_Button(); break;
	case Z_BUTTON: Z_Button(); break;
	}
}

void ZPlayer::A_Button()
{

}

void ZPlayer::B_Button()
{
	if(gui_factory_list) gui_factory_list->ToggleShow();
}

void ZPlayer::D_Button()
{

}

void ZPlayer::G_Button()
{
	//RandomlySelectUnitType(CANNON_OBJECT);
	OrderlySelectUnitType(CANNON_OBJECT);
}

void ZPlayer::Menu_Button()
{
	LoadMainMenu(GMM_MAIN_MAIN);
}

void ZPlayer::R_Button()
{
	//RandomlySelectUnitType(ROBOT_OBJECT);
	OrderlySelectUnitType(ROBOT_OBJECT);
}

void ZPlayer::T_Button()
{

}

void ZPlayer::V_Button()
{
	//RandomlySelectUnitType(VEHICLE_OBJECT);
	OrderlySelectUnitType(VEHICLE_OBJECT);
}

void ZPlayer::Z_Button()
{

}

void ZPlayer::OrderlySelectUnitType(int type)
{
	ZObject* the_choice;
	static double last_time = -100;
    double the_time = COMMON::current_time();
	static int last_ref_id_default = -1;
	static int last_ref_id_cannon = -1;
	static int last_ref_id_robot = -1;
	static int last_ref_id_vehicle = -1;
	int *last_ref_id = &last_ref_id_default;

	if(our_team == NULL_TEAM) return;

	//different id for each type
	switch(type)
	{
		case CANNON_OBJECT: last_ref_id=&last_ref_id_cannon; break;
		case ROBOT_OBJECT: last_ref_id=&last_ref_id_robot; break;
		case VEHICLE_OBJECT: last_ref_id=&last_ref_id_vehicle; break;
	}

	the_choice = nullptr;

	//do next in line like normal
	if(the_time - last_time < 7)
	{
		the_choice = ZObject::NextSelectableObjectAboveID(object_list, type, our_team, *last_ref_id);

		if(!the_choice) the_choice = ZObject::NextSelectableObjectAboveID(object_list, type, our_team, -1);
	}
	else
	{
		//choose the nearest to mouse
		int map_x, map_y;

		zmap.GetMapCoords(mouse_x, mouse_y, map_x, map_y);

		the_choice = ZObject::NearestSelectableObject(object_list, type, our_team, map_x, map_y);
	}

	//make the selection
	if(the_choice)
	{
		int ox, oy;

		select_info.Clear();
		select_info.selected_list.push_back(the_choice);
		select_info.SetupGroupDetails();
		DetermineCursor();
		GiveHudSelected();

		the_choice->GetCenterCords(ox, oy);
		FocusCameraTo(ox, oy);

		*last_ref_id = the_choice->GetRefID();
		last_time = the_time;
	}
}

void ZPlayer::RandomlySelectUnitType(int type)
{
	vector<ZObject*>::iterator i;
	vector<ZObject*> temp_choice_list;
	ZObject* the_choice = nullptr;

	if(our_team == NULL_TEAM) return;

	for(i=object_list.begin();i!=object_list.end();i++)
		if((*i)->GetOwner() == our_team)
	{
		unsigned char ot, oid;

		(*i)->GetObjectID(ot, oid);

		if(ot == type) temp_choice_list.push_back(*i);
	}

	if(temp_choice_list.size())
		the_choice = temp_choice_list[rand() % temp_choice_list.size()];

	if(the_choice)
	{
		int ox, oy;

		select_info.Clear();
		select_info.selected_list.push_back(the_choice);
		select_info.SetupGroupDetails();
		DetermineCursor();
		GiveHudSelected();

		the_choice->GetCenterCords(ox, oy);
		FocusCameraTo(ox, oy);
	}
}

void ZPlayer::FocusCameraToFort()
{
	int shift_x, shift_y, view_w, view_h;
	int goto_x, goto_y;

	if(!zmap.Loaded()) return;
	if(our_team == NULL_TEAM) return;

	//find our fort
	for(vector<ZObject*>::iterator i=object_list.begin(); i!=object_list.end(); i++)
	{
		unsigned char ot, oid;

		if((*i)->GetOwner() != our_team) continue;

		(*i)->GetObjectID(ot, oid);

		if(ot != BUILDING_OBJECT) continue;
		if(!(oid == FORT_FRONT || oid == FORT_BACK)) continue;

		zmap.GetViewShiftFull(shift_x, shift_y, view_w, view_h);

		(*i)->GetCenterCords(goto_x, goto_y);

		goto_x = goto_x - (view_w >> 1);
		goto_y = goto_y - (view_w >> 1);

		zmap.SetViewShift(goto_x, goto_y);
		return;
	}
}

void ZPlayer::FocusCameraTo(int map_x, int map_y)
{
	int shift_x, shift_y, view_w, view_h;
	double dx, dy;

	zmap.GetViewShiftFull(shift_x, shift_y, view_w, view_h);

	//set desired location
	focus_to_x = map_x - (view_w >> 1);
	focus_to_y = map_y - (view_h >> 1);

	//are we already there?
	if(shift_x == focus_to_x && shift_y == focus_to_y) return;

	//set original distance
	dx = focus_to_x - shift_x;
	dy = focus_to_y - shift_y;
	focus_to_original_distance = sqrt((dx *dx) + (dy * dy));

    last_focus_to_time = COMMON::current_time();

	//final_focus_to_time = last_focus_to_time + (focus_to_original_distance / 40);
	final_focus_to_time = last_focus_to_time + 0.7;

	do_focus_to = true;
}

void ZPlayer::ProcessFocusCamerato()
{
    double the_time = COMMON::current_time();
	int shift_x, shift_y, view_w, view_h;
	double dx, dy;
	double end_dx, end_dy;
	bool shifted_x, shifted_y;

	if(!do_focus_to) return;

	zmap.GetViewShiftFull(shift_x, shift_y, view_w, view_h);

	//ZLOG("shift_x:%d shift_y:%d\n", shift_x, shift_y);

	dx = focus_to_x - shift_x;
	dy = focus_to_y - shift_y;

	//exit conditions
	if((!dx && !dy))
	{
		do_focus_to = false;
		return;
	}

	//normal move
	end_dx = dx * 0.1;
	end_dy = dy * 0.1;

	if(!((int)end_dx) && dx)
	{
		if(dx > 0) end_dx = 1;
		else end_dx = -1;
	}

	if(!((int)end_dy) && dy)
	{
		if(dy > 0) end_dy = 1;
		else end_dy = -1;
	}
	
	if(end_dx > 0)
		shifted_x = zmap.ShiftViewRight((int)end_dx);
	else
		shifted_x = zmap.ShiftViewLeft((int)-end_dx);

	if(end_dy > 0)
		shifted_y = zmap.ShiftViewDown((int)end_dy);
	else
		shifted_y = zmap.ShiftViewUp((int)-end_dy);

	if(!shifted_x && !shifted_y)
		do_focus_to = false;
	else if(!shifted_x && !dy)
		do_focus_to = false;
	else if(!shifted_y && !dx)
		do_focus_to = false;

	return;
}

void ZPlayer::StartMouseScrolling(int new_mouse_x, int new_mouse_y)
{
	if(SDL_WM_GrabInput(SDL_GRAB_QUERY) == SDL_GRAB_OFF) return;

	if(!(mouse_x < 10) && (new_mouse_x < 10)) 
	{
		horz_scroll_over = 0;
        last_horz_scroll_time = COMMON::current_time();
	}
	else if(!(mouse_x > screen->w - 10) && (new_mouse_x > screen->w - 10)) 
	{
		horz_scroll_over = 0;
        last_horz_scroll_time = COMMON::current_time();
	}

	if(!(mouse_y < 10) && (new_mouse_y < 10)) 
	{
		vert_scroll_over = 0;
        last_vert_scroll_time = COMMON::current_time();
	}
	else if(!(mouse_y > screen->h - 10) && (new_mouse_y > screen->h - 10)) 
	{
		vert_scroll_over = 0;
        last_vert_scroll_time = COMMON::current_time();
	}
}

bool ZPlayer::DoMouseScrollRight()
{
	return (mouse_x > screen->w - 10 && SDL_WM_GrabInput(SDL_GRAB_QUERY) == SDL_GRAB_ON);
}

bool ZPlayer::DoMouseScrollLeft()
{
	return (mouse_x < 10 && SDL_WM_GrabInput(SDL_GRAB_QUERY) == SDL_GRAB_ON);
}

bool ZPlayer::DoMouseScrollUp()
{
	return (mouse_y < 10 && SDL_WM_GrabInput(SDL_GRAB_QUERY) == SDL_GRAB_ON);
}

bool ZPlayer::DoMouseScrollDown()
{
	return (mouse_y > screen->h - 10 && SDL_WM_GrabInput(SDL_GRAB_QUERY) == SDL_GRAB_ON);
}

bool ZPlayer::DoKeyScrollRight()
{
	return (right_down && !left_down);
}

bool ZPlayer::DoKeyScrollLeft()
{
	return (!right_down && left_down);
}

bool ZPlayer::DoKeyScrollUp()
{
	return (up_down && !down_down);
}

bool ZPlayer::DoKeyScrollDown()
{
	return (!up_down && down_down);
}

void ZPlayer::ProcessScroll()
{
    double the_time = COMMON::current_time();
	const double shift_speed = 400;
	double time_diff;
	double the_shift;

	//no scrolling when we are
	//currently flying towards a location
	//(such as when jumping to a selected unit)
	if(do_focus_to) return;

	//dont scroll if it isn't all loaded
	if(!graphics_loaded) return;

	if(DoKeyScrollUp() || DoMouseScrollUp())//up_down && !down_down)
	{
		time_diff = the_time - last_vert_scroll_time;

		the_shift = time_diff * shift_speed;
		the_shift += vert_scroll_over;

		if(the_shift >= 1)
		{
			last_vert_scroll_time = the_time;
			vert_scroll_over = the_shift - (int)the_shift;
			zmap.ShiftViewUp((int)the_shift);
		}
	}
	else if(DoKeyScrollDown() || DoMouseScrollDown())//!up_down && down_down)
	{
		time_diff = the_time - last_vert_scroll_time;

		the_shift = time_diff * shift_speed;
		the_shift += vert_scroll_over;

		if(the_shift >= 1)
		{
			last_vert_scroll_time = the_time;
			vert_scroll_over = the_shift - (int)the_shift;
			zmap.ShiftViewDown((int)the_shift);
		}
	}

	if(DoKeyScrollRight() || DoMouseScrollRight())//right_down && !left_down)
	{
		time_diff = the_time - last_horz_scroll_time;

		the_shift = time_diff * shift_speed;
		the_shift += horz_scroll_over;



		if(the_shift >= 1)
		{
			last_horz_scroll_time = the_time;
			horz_scroll_over = the_shift - (int)the_shift;
			zmap.ShiftViewRight((int)the_shift);
		}
	}
	else if(DoKeyScrollLeft() || DoMouseScrollLeft())//!right_down && left_down)
	{
		time_diff = the_time - last_horz_scroll_time;

		the_shift = time_diff * shift_speed;
		the_shift += horz_scroll_over;

		if(the_shift >= 1)
		{
			last_horz_scroll_time = the_time;
			horz_scroll_over = the_shift - (int)the_shift;
			zmap.ShiftViewLeft((int)the_shift);
		}
	}
}

void ZPlayer::RenderPreviousCursor()
{
	double &the_time = ztime.ztime;

	if(the_time < pcursor_death_time)
	{
		//int shift_x, shift_y;
		//zmap.GetViewShift(shift_x, shift_y);

		//shift_x = pcursor_x - shift_x;
		//shift_y = pcursor_y - shift_y;

		//Pcursor.Render(zmap, screen, shift_x, shift_y, true);
		Pcursor.Render(zmap, screen, pcursor_x, pcursor_y, true);
	}
}

void ZPlayer::SetPcursor()
{
	switch(cursor.GetCursor())
	{
	case PLACE_C:
		Pcursor.SetCursor(PLACED_C);
		break;
	case ATTACK_C:
		Pcursor.SetCursor(ATTACKED_C);
		break;
	case GRAB_C:
		Pcursor.SetCursor(GRABBED_C);
		break;
	case GRENADE_C:
		Pcursor.SetCursor(GRENADED_C);
		break;
	case REPAIR_C:
		Pcursor.SetCursor(REPAIRED_C);
		break;
	case ENTER_C:
		Pcursor.SetCursor(ENTERED_C);
		break;
	case EXIT_C:
		Pcursor.SetCursor(EXITED_C);
		break;
	case CANNON_C:
		Pcursor.SetCursor(CANNONED_C);
		break;
	default:
		Pcursor.SetCursor(PLACED_C);
		break;
	}
}

void ZPlayer::ShowPcursor(int mx, int my)
{
	double &the_time = ztime.ztime;

	pcursor_death_time = the_time + 3.0;
	pcursor_x = mx;
	pcursor_y = my;
}

/* Bildratenanzeige oben links -- nur Ziffern, laufend.
 *
 * Angezeigt wird die AKTUELLE Bildrate: Kehrwert der Dauer des letzten
 * Bildes, in JEDEM Bild neu. Die Zahl folgt damit unmittelbar dem, was
 * gerade passiert -- bricht die Bildrate bei einer Explosion ein, steht es
 * sofort da.
 *
 * Die zehn Ziffern werden EINMAL vorgerendert und danach nur noch geblittet.
 * Der naheliegende Weg (je Bild ZFont::Render auf die Zahl) legte bei jeder
 * Aenderung eine Flaeche an, fuellte sie, setzte den Farbschluessel und
 * blittete Zeichen fuer Zeichen -- bei einer Anzeige, die sich staendig
 * aendert, waere das genau die Last, die sie messen soll.
 *
 * Gemessen wird ueber die E-Clock (1,41 us). current_time() kaeme nicht in
 * Frage: es loest auf dem Amiga 20 ms auf, ein Bild bei 60 fps dauert 16 ms
 * und waere damit nicht messbar.
 */
/* Vom Hauptlauf gemeldete Schlafdauer des letzten Bildes (E-Clock-Ticks).
 *
 * Die Engine schlaeft, sobald im Budget von 1/60 s etwas uebrig ist
 * (main.cpp, ZOD_FRAME_TARGET) -- und auf AmigaOS rundet dieser Schlaf auf
 * das 20-ms-Raster auf. Die Wanduhrzeit je Bild misst damit die Taktbremse,
 * nicht die Maschine: Die Anzeige kaeme nie ueber 60, und jede Verbesserung
 * verschwaende im Schlaf.
 *
 * Deshalb zeigt die Bildratenanzeige die ARBEIT je Bild -- also das, was die
 * Maschine leisten koennte. Sie faellt bei einer Explosion sofort und steigt,
 * wenn etwas schneller wird; beides waere unter der Bremse unsichtbar. */
unsigned long zod_fps_schlaf = 0;

extern "C" void zod_fps_add_sleep(unsigned long ticks)
{
	zod_fps_schlaf += ticks;
}

/* Verteilung der Bildzeiten ueber den ganzen Lauf. */
unsigned long zod_frame_count = 0;
unsigned long zod_frame_worst = 0;
unsigned long zod_frame_over20 = 0;
unsigned long zod_frame_over50 = 0;
unsigned long zod_frame_over100 = 0;

extern "C" void zod_frame_report(unsigned long *count, unsigned long *worst,
                                 unsigned long *o20, unsigned long *o50,
                                 unsigned long *o100, unsigned long *freq)
{
	*count = zod_frame_count; *worst = zod_frame_worst;
	*o20 = zod_frame_over20; *o50 = zod_frame_over50; *o100 = zod_frame_over100;
	*freq = zod_fineclock_freq();
}

void ZPlayer::RenderFps()
{
	const unsigned long freq = zod_fineclock_freq();

	if(!freq) return;

	/* Ziffern einmal vorbereiten. */
	if(!fps_digits_ready)
	{
		for(int d = 0; d < 10; d++)
		{
			char z[2] = { (char)('0' + d), 0 };

			fps_digit[d].LoadBaseImage(ZFontEngine::GetFont(BIG_WHITE_FONT).Render(z));
		}

		fps_digits_ready = true;
	}

	const unsigned long jetzt = zod_fineclock_ticks();
	int wert = 0;

	if(fps_last_tick)
	{
		/* Vorzeichenlose Subtraktion: auch ueber den Ueberlauf des unteren
		 * Langworts hinweg der richtige Abstand.
		 *
		 * EINMAL rechnen und fuer beides benutzen -- Anzeige und Verteilung.
		 * (Hier stand die Rechnung kurzzeitig zweimal, und die zweite lief
		 * NACH dem Nullsetzen des Schlafs, haette also die Wanduhrzeit
		 * gezaehlt.) */
		unsigned long arbeit = jetzt - fps_last_tick;

		if(arbeit > zod_fps_schlaf) arbeit -= zod_fps_schlaf;
		else                        arbeit = 1;

		if(arbeit) wert = (int)(freq / arbeit);

		/* Verteilung ueber den ganzen Lauf -- ebenfalls auf die ARBEIT
		 * bezogen, sonst zaehlte sie die Taktbremse als Ruckeln mit. */
		zod_frame_count++;

		if(arbeit > zod_frame_worst) zod_frame_worst = arbeit;

		if(arbeit > freq / 50)  zod_frame_over20++;   /*  > 20 ms */
		if(arbeit > freq / 20)  zod_frame_over50++;   /*  > 50 ms */
		if(arbeit > freq / 10)  zod_frame_over100++;  /* > 100 ms */
	}

	zod_fps_schlaf = 0;
	fps_last_tick = jetzt;

	if(wert > 999) wert = 999;

	/* Ziffern von links nach rechts setzen. */
	char text[8];
	int n = snprintf(text, sizeof(text), "%d", wert);
	int x = 3;

	for(int i = 0; i < n; i++)
	{
		const int d = text[i] - '0';

		if(d < 0 || d > 9) continue;

		SDL_Surface *bild = fps_digit[d].GetBaseSurface();

		if(!bild) continue;

		SDL_Rect to_rect;

		to_rect.x = (Sint16)x;
		to_rect.y = 2;

		fps_digit[d].BlitSurface(nullptr, &to_rect);

		x += bild->w;
	}
}

void ZPlayer::RenderNews()
{
    double the_time = COMMON::current_time();
	const int y_int = 15;
	const double start_fade_time = 5;
	const int max_news_history = 50;
	double time_left;
	int max_news_width;
	SDL_Rect from_rect, to_rect;

	//max_news_width = screen->w - (5 + 100);
	max_news_width = init_w - (5 + 100);

	from_rect.x = 0;
	from_rect.y = 0;
	from_rect.h = 40;
	to_rect.x = 5;
	//to_rect.y = screen->h - (36 + y_int);
	to_rect.y = init_h - (36 + y_int);
	to_rect.w = 0;
	to_rect.h = 0;

	if(gui_factory_list && gui_factory_list->IsVisible()) to_rect.x += 142;

	//cut down to size
	while(news_list.size() > max_news_history)
	{
		//vector<news_entry*>::iterator i;

		////i = news_list.end()--;
		//i = news_list.begin() + (news_list.size()-1);

		//delete *i;
		//news_list.erase(i);
		delete news_list.back();
		news_list.pop_back();
	}

	for(vector<news_entry*>::iterator i=news_list.begin(); i!=news_list.end();)
	{
		news_entry *cur_entry;

		cur_entry = *i;

		time_left = cur_entry->death_time - the_time;

		//do not draw this news piece?
		if(!show_chat_history && time_left <= 0)
		{
			//delete *i;
			//i = news_list.erase(i);
			i++;
			continue;
		}

		//start fading?
		if(!show_chat_history && time_left < start_fade_time)
		{
			double fade_alpha;

			fade_alpha = (time_left / start_fade_time) * 255;

			//SDL_SetAlpha(i->text_image,SDL_RLEACCEL | SDL_SRCALPHA,(Uint8)fade_alpha);
			cur_entry->text_image.SetAlpha(fade_alpha);
		}
		else
			cur_entry->text_image.SetAlpha(255);

		//make sure it does not draw over the hud

		from_rect.w = max_news_width;

		//      ZLOG("rendering text %s [%d %d %d %d] [%d %d %d %d]\n", i->message.c_str(), from_rect.x, from_rect.y, from_rect.w, from_rect.h, to_rect.x, to_rect.y, to_rect.w, to_rect.h);

		cur_entry->text_image.BlitSurface(&from_rect, &to_rect);
		//SDL_BlitSurface( i->text_image, &from_rect, screen, &to_rect);

		to_rect.y -= y_int;
		i++;
	}
}

void ZPlayer::RenderObjects()
{
	//draw effects pre stuff
	for(vector<ZEffect*>::iterator i=effect_list.begin(); i!=effect_list.end(); i++)
		(*i)->DoPreRender(zmap, screen);

	zod_rsec_end(RSEC_EFF_VOR);

	zod_rsec_olist_all  = (unsigned long)ols.prender_olist.size();
	zod_rsec_olist_pre  = (unsigned long)ols.prerender_olist.size();
	zod_rsec_olist_post = (unsigned long)ols.aftereffect_olist.size();

	//draw objects pre stuff -- nur die Klassen, die eines haben
	for(vector<ZObject*>::iterator i=ols.prerender_olist.begin(); i!=ols.prerender_olist.end(); i++)
		(*i)->DoPreRender(zmap, screen);

	zod_rsec_end(RSEC_OBJ_VOR);

	//draw rallypoints of "selected" building
	if(gui_window && gui_window->GetBuildingObj())
		gui_window->GetBuildingObj()->DoRenderWaypoints(zmap, screen, object_list, true);

	//draw object's waypoints
	for(vector<ZObject*>::iterator i=ols.non_mapitem_olist.begin(); i!=ols.non_mapitem_olist.end(); i++)
		(*i)->DoRenderWaypoints(zmap, screen, object_list);

	zod_rsec_end(RSEC_WEGPUNKTE);

	//draw objects
	for(vector<ZObject*>::iterator i=ols.prender_olist.begin(); i!=ols.prender_olist.end(); i++)
		(*i)->DoRender(zmap, screen);

	zod_rsec_end(RSEC_OBJEKTE);

	//draw after effects -- nur die Klassen, die eines haben
	for(vector<ZObject*>::iterator i=ols.aftereffect_olist.begin(); i!=ols.aftereffect_olist.end(); i++)
		(*i)->DoAfterEffects(zmap, screen);

	zod_rsec_end(RSEC_OBJ_NACH);

	//effects
	/* Die Blitzahlen NUR dieses Abschnitts einklammern. Ohne sie ist nicht zu
	 * entscheiden, ob die Zeit im Blitten von Bildpunkten steckt oder im
	 * Aufwand je Effekt -- und genau daran habe ich hier schon einmal falsch
	 * geraten. Die Zaehler sind Ganzzahladditionen, kosten also nichts. */
	{
		unsigned long b0_c, b0_p, b0_r;

		zod_blit_now(&b0_c, &b0_p, &b0_r);

		for(vector<ZEffect*>::iterator i=effect_list.begin(); i!=effect_list.end(); i++)
			(*i)->DoRender(zmap, screen);

		{
			unsigned long b1_c, b1_p, b1_r;

			zod_blit_now(&b1_c, &b1_p, &b1_r);

			zod_effblit_calls = b1_c - b0_c;
			zod_effblit_px    = b1_p - b0_p;
			zod_effblit_rows  = b1_r - b0_r;
		}
	}

	zod_rsec_end(RSEC_EFFEKTE);

	//animals
	for(vector<ZObject*>::iterator i=bird_list.begin(); i!=bird_list.end();i++)
		(*i)->DoRender(zmap, screen);

	zod_rsec_end(RSEC_VOEGEL);

	//draw selection stuff
	for(vector<ZObject*>::iterator i=select_info.selected_list.begin(); i!=select_info.selected_list.end(); i++)
	{
		(*i)->RenderSelection(zmap, screen);
		(*i)->RenderAttackRadius(zmap, screen, select_info.selected_list);
	}

	////draw attack radius for the chosen one
	//if(zhud.GetSelectedObject())
	//	zhud.GetSelectedObject()->RenderAttackRadius(zmap, screen);
	
	//draw hover names
	if(hover_object)
	{
		//if it has a leader render it instead
		if(hover_object->GetGroupLeader())
		{
			hover_object->GetGroupLeader()->RenderHover(zmap, screen, our_team);
		}
		else
		{
			hover_object->RenderHover(zmap, screen, our_team);
		}
	}

	zod_rsec_end(RSEC_AUSWAHL);
}

void ZPlayer::RenderGUI()
{
	if(gui_window) gui_window->DoRender(zmap, screen);

	if(gui_factory_list) gui_factory_list->DoRender(zmap, screen);
}

void ZPlayer::RenderMainMenu()
{
	//render so first in list is rendered last
	for(int i=gui_menu_list.size()-1;i>=0;i--)
		gui_menu_list[i]->DoRender(zmap, screen);
}

void ZPlayer::DetermineCursor()
{
	//only one cursor when we are selecting
	if(lbutton.down) cursor.SetCursor(CURSOR_C);

	else if(select_info.selected_list.size())
	{
		if(hover_object)
		{
			unsigned char ot, oid;
			hover_object->GetObjectID(ot, oid);

			if(select_info.can_repair && hover_object->CanBeRepairedByCrane(our_team))
			{
				cursor.SetCursor(REPAIR_C);
			}
			else if(select_info.can_be_repaired && hover_object->CanRepairUnit(our_team))
			{
				cursor.SetCursor(REPAIR_C);
			}
			else if(hover_object->GetOwner() != our_team)
			{
				if(select_info.can_move)
				{
					if(ot == MAP_ITEM_OBJECT && oid == GRENADES_ITEM && select_info.can_pickup_grenades)
						cursor.SetCursor(GRAB_C);
					else if(ot == MAP_ITEM_OBJECT && oid == FLAG_ITEM && select_info.can_move)
						cursor.SetCursor(GRAB_C);
					else if((ot == CANNON_OBJECT || ot == VEHICLE_OBJECT) && hover_object->GetOwner() == NULL_TEAM && select_info.can_equip)
						cursor.SetCursor(ENTER_C);
					else
					{
						if(hover_object_can_enter_fort)
							cursor.SetCursor(PLACE_C);
						else
						{
							if(!select_info.can_attack || (!select_info.have_explosives && hover_object->AttackedOnlyByExplosives()))
								cursor.SetCursor(NONO_C);
							else
								cursor.SetCursor(ATTACK_C);
						}
					}
				}
				else
				{
					//we have an object under us, but we only have cannons selected
					if(!(ot == MAP_ITEM_OBJECT && oid == FLAG_ITEM))
					{
						if(hover_object_can_enter_fort)
							cursor.SetCursor(PLACE_C);
						else
						{
							if(!select_info.can_attack || (!select_info.have_explosives && hover_object->AttackedOnlyByExplosives()))
								cursor.SetCursor(NONO_C);
							else
								cursor.SetCursor(ATTACK_C);
						}
					}
					else
						cursor.SetCursor(CANNON_C);
				}

			}
			else
			{
				int i = GetObjectIndex(hover_object, select_info.selected_list);

				//if(ot == CANNON_OBJECT && i!= -1)
				if(hover_object->CanEjectDrivers() && select_info.selected_list.size() == 1 && i!= -1)
					cursor.SetCursor(EXIT_C);
				else
					cursor.SetCursor(PLACE_C);
			}
		}
		else
		{
			if(select_info.can_move)
				cursor.SetCursor(PLACE_C);
			else
				cursor.SetCursor(CANNON_C);
		}
	}
	else
	{
		//normal cursor
		cursor.SetCursor(CURSOR_C);
	}
}

void ZPlayer::ExitProgram()
{
	Mix_CloseAudio();
	exit(0);
}

void ZPlayer::ProcessSDL()
{
	SDL_Event event;
	key_event the_key;
	int shift_x, shift_y;
	
	while(SDL_PollEvent(&event))
		switch( event.type ) 
	{
		case SDL_QUIT:
			ExitProgram();
			break;
		case SDL_VIDEORESIZE:
			init_w = event.resize.w;
			init_h = event.resize.h;
			//ehandler.AddEvent(new Event(SDL_EVENT, RESIZE_EVENT, 0, nullptr, 0));
			ehandler.ProcessEvent(SDL_EVENT, RESIZE_EVENT, nullptr, 0, 0);
			break;
		case SDL_MOUSEMOTION:
			StartMouseScrolling(event.motion.x, event.motion.y);
			mouse_x = event.motion.x;
			mouse_y = event.motion.y;
			//ehandler.AddEvent(new Event(SDL_EVENT, MOTION_EVENT, 0, nullptr, 0));
			ehandler.ProcessEvent(SDL_EVENT, MOTION_EVENT, nullptr, 0, 0);
			break;
		case SDL_MOUSEBUTTONDOWN:
			zmap.GetViewShift(shift_x, shift_y);
			switch(event.button.button)
			{
			case SDL_BUTTON_LEFT:
				lbutton.x = event.button.x;
				lbutton.y = event.button.y;
				lbutton.map_x = lbutton.x + shift_x;
				lbutton.map_y = lbutton.y + shift_y;
				//ehandler.AddEvent(new Event(SDL_EVENT, LCLICK_EVENT, 0, nullptr, 0));
				ehandler.ProcessEvent(SDL_EVENT, LCLICK_EVENT, nullptr, 0, 0);
				break;
			case SDL_BUTTON_RIGHT:
				rbutton.x = event.button.x;
				rbutton.y = event.button.y;
				rbutton.map_x = rbutton.x + shift_x;
				rbutton.map_y = rbutton.y + shift_y;
				//ehandler.AddEvent(new Event(SDL_EVENT, RCLICK_EVENT, 0, nullptr, 0));
				ehandler.ProcessEvent(SDL_EVENT, RCLICK_EVENT, nullptr, 0, 0);
				break;
			case SDL_BUTTON_MIDDLE:
				mbutton.x = event.button.x;
				mbutton.y = event.button.y;
				mbutton.map_x = ((init_w - HUD_WIDTH) >> 1) + shift_x;
				mbutton.map_y = ((init_h - HUD_HEIGHT) >> 1) + shift_y;
				//ehandler.AddEvent(new Event(SDL_EVENT, MCLICK_EVENT, 0, nullptr, 0));
				ehandler.ProcessEvent(SDL_EVENT, MCLICK_EVENT, nullptr, 0, 0);
				break;
			case SDL_BUTTON_WHEELUP:
				//ehandler.AddEvent(new Event(SDL_EVENT, WHEELUP_EVENT, 0, nullptr, 0));
				ehandler.ProcessEvent(SDL_EVENT, WHEELUP_EVENT, nullptr, 0, 0);
				break;
			case SDL_BUTTON_WHEELDOWN:
				//ehandler.AddEvent(new Event(SDL_EVENT, WHEELDOWN_EVENT, 0, nullptr, 0));
				ehandler.ProcessEvent(SDL_EVENT, WHEELDOWN_EVENT, nullptr, 0, 0);
				break;
			}
			break;
		case SDL_MOUSEBUTTONUP:
			switch(event.button.button)
			{
			case SDL_BUTTON_LEFT:
				//ehandler.AddEvent(new Event(SDL_EVENT, LUNCLICK_EVENT, 0, nullptr, 0));
				ehandler.ProcessEvent(SDL_EVENT, LUNCLICK_EVENT, nullptr, 0, 0);
				break;
			case SDL_BUTTON_RIGHT:
				//ehandler.AddEvent(new Event(SDL_EVENT, RUNCLICK_EVENT, 0, nullptr, 0));
				ehandler.ProcessEvent(SDL_EVENT, RUNCLICK_EVENT, nullptr, 0, 0);
				break;
			case SDL_BUTTON_MIDDLE:
				//ehandler.AddEvent(new Event(SDL_EVENT, MUNCLICK_EVENT, 0, nullptr, 0));
				ehandler.ProcessEvent(SDL_EVENT, MUNCLICK_EVENT, nullptr, 0, 0);
				break;
			}
			break;
		case SDL_KEYDOWN:
			the_key.the_key = event.key.keysym.sym;
			the_key.the_unicode = event.key.keysym.unicode;
			//ehandler.AddEvent(new Event(SDL_EVENT, KEYDOWN_EVENT_, 0, (char*)&the_key, sizeof(key_event)));
			ehandler.ProcessEvent(SDL_EVENT, KEYDOWN_EVENT_, (char*)&the_key, sizeof(key_event), 0);
			break;
		case SDL_KEYUP:
			the_key.the_key = event.key.keysym.sym;
			the_key.the_unicode = event.key.keysym.unicode;
			//ehandler.AddEvent(new Event(SDL_EVENT, KEYUP_EVENT_, 0, (char*)&the_key, sizeof(key_event)));
			ehandler.ProcessEvent(SDL_EVENT, KEYUP_EVENT_, (char*)&the_key, sizeof(key_event), 0);
			break;
	}
}

void ZPlayer::DoSplash()
{
	const float fade_per_second = 5;
	static double last_time;
	static bool did_init = false;

	if(!splash_screen.GetBaseSurface()) return;

	//draw?
	if(splash_fade >= 5)
	{
		SDL_Rect to_rect;
		
		//decrease fade?
		if(graphics_loaded)// && zmap.Loaded())
		{
			if(!did_init)
			{
                last_time = COMMON::current_time();
				did_init = true;
			}
			
            splash_fade -= (float)(COMMON::current_time() - last_time) * fade_per_second;
			if(splash_fade < 0) splash_fade = 0;
			
			switch(sound_setting)
			{
			case SOUND_25: Mix_VolumeMusic((80 / 4) * splash_fade / 255); break;
			case SOUND_50: Mix_VolumeMusic((80 / 2) * splash_fade / 255); break;
			case SOUND_75: Mix_VolumeMusic((80 * 3 / 4) * splash_fade / 255); break;
			case SOUND_100: Mix_VolumeMusic((80) * splash_fade / 255); break;
			}
			//Mix_VolumeMusic((int)(128.0 * splash_fade / 255));

			//if(splash_screen)
			//SDL_SetAlpha(splash_screen,SDL_RLEACCEL | SDL_SRCALPHA,(Uint8)splash_fade);
			splash_screen.SetAlpha(splash_fade);
		}
		
		//render
		//to_rect.x = (screen->w - splash_screen->w) >> 1;
		//to_rect.y = (screen->h - splash_screen->h) >> 1;
		to_rect.x = (init_w - splash_screen.GetBaseSurface()->w) >> 1;
		to_rect.y = (init_h - splash_screen.GetBaseSurface()->h) >> 1;

		splash_screen.BlitSurface(nullptr, &to_rect);
		//zmap.RenderZSurface(&splash_screen, init_w >> 1, init_h >> 1, true);
		
		//if(splash_screen)
		//SDL_BlitSurface(splash_screen, nullptr, screen, &to_rect);

		//what the fuck loading
		{
			ZSDL_Surface loading_text;
			char loading_c[500];

			if(loaded_percent > 100) loaded_percent = 100;
			snprintf(loading_c, sizeof(loading_c),"LOADING %d%c", loaded_percent, '%');

			loading_text.LoadBaseImage(ZFontEngine::GetFont(LOADING_WHITE_FONT).Render(loading_c));
			loading_text.MakeAlphable();
			loading_text.SetAlpha(splash_fade / 1.5);
			if(loading_text.GetBaseSurface())
			{
				/*to_rect.x += splash_screen.GetBaseSurface()->w;
				to_rect.y += splash_screen.GetBaseSurface()->h;*/
				to_rect.x += 430;
				to_rect.y += 300;

				//to_rect.x -= 200;
				//to_rect.y -= loading_text.GetBaseSurface()->h;
				loading_text.BlitSurface(nullptr, &to_rect);
			}
		}


		//load the normal music
		if(splash_fade < 5)
		{
			ZMusicEngine::PlayPlanetMusic(zmap.GetMapBasics().terrain_type);
			switch(sound_setting)
			{
			case SOUND_25: Mix_VolumeMusic((80 / 4)); break;
			case SOUND_50: Mix_VolumeMusic((80 / 2)); break;
			case SOUND_75: Mix_VolumeMusic((80 * 3 / 4)); break;
			case SOUND_100: Mix_VolumeMusic((80)); break;
			}
		}
			//if(music_on) zmap.PlayMusic();
	}
	
}

void ZPlayer::SelectZObject(ZObject *obj)
{
	if(!obj) return;

	//select leader instead
	if(obj->GetGroupLeader()) obj = obj->GetGroupLeader();

	if(!obj->Selectable()) return;
	if(obj->GetOwner() != our_team) return;

	select_info.Clear();

	select_info.selected_list.push_back(obj);
	select_info.SetupGroupDetails();

	DetermineCursor();
	GiveHudSelected();
	ClearDevWayPointsOfSelected();
}

void ZPlayer::SelectAllOfType(int type)
{
	if(our_team == NULL_TEAM) return;
	if(type != -1 && !(type == ROBOT_OBJECT || type == VEHICLE_OBJECT || type == CANNON_OBJECT)) return;

	select_info.Clear();

	for(vector<ZObject*>::iterator i=ols.passive_engagable_olist.begin(); i!=ols.passive_engagable_olist.end(); ++i)
	{
		unsigned char ot, oid;

		(*i)->GetObjectID(ot, oid);

		if(type != -1)
		{
			if(ot != type) continue;
		}
		else
		{
			if(ot != ROBOT_OBJECT && ot != VEHICLE_OBJECT) continue;
		}
		if((*i)->GetOwner() != our_team) continue;
		if(!(*i)->Selectable()) continue;

		select_info.selected_list.push_back(*i);
	}

	select_info.SetupGroupDetails();

	DetermineCursor();
	GiveHudSelected();
}

void ZPlayer::CollectSelectables()
{
	int shift_x, shift_y;
	int mouse_x_map, mouse_y_map;
	int map_left, map_right, map_top, map_bottom;

	if(!lbutton.down) return;

	select_info.Clear();

	zmap.GetViewShift(shift_x, shift_y);

	mouse_x_map = mouse_x + shift_x;
	mouse_y_map = mouse_y + shift_y;

	if(mouse_x_map < lbutton.map_x)
	{
		map_left = mouse_x_map;
		map_right = lbutton.map_x;
	}
	else
	{
		map_left = lbutton.map_x;
		map_right = mouse_x_map;
	}

	if(mouse_y_map < lbutton.map_y)
	{
		map_top = mouse_y_map;
		map_bottom = lbutton.map_y;
	}
	else
	{
		map_top = lbutton.map_y;
		map_bottom = mouse_y_map;
	}

	//are we only selecting one unit?
	if(abs(lbutton.map_x - mouse_x_map) <= 1 && abs(lbutton.map_y - mouse_y_map) <= 1)
	{
		vector<ZObject*> choice_list;

		for(vector<ZObject*>::iterator i=object_list.begin(); i!=object_list.end(); i++)
		{
			ZObject *pot_obj;

			pot_obj = *i;

			//ordering of checks is important, arg
			if(pot_obj->GetOwner() != our_team) continue;
			if(!pot_obj->WithinSelection(map_left, map_right, map_top, map_bottom)) continue;

			//if it is a minion, select its leader for a potential selection choice
			if(pot_obj->GetGroupLeader()) pot_obj = pot_obj->GetGroupLeader();

			if(!pot_obj->Selectable()) continue;

			//it already in the list?
			for(vector<ZObject*>::iterator j=choice_list.begin(); j!=choice_list.end(); j++)
				if(*j == pot_obj) 
				{
					pot_obj = nullptr;
					break;
				}

			//put it in the list
			if(pot_obj) choice_list.push_back(pot_obj);
		}

		if(choice_list.size())
			select_info.selected_list.push_back(choice_list[rand() % choice_list.size()]);

	}
	else //selecting all possible units
	{
		//so who have we selected
		for(vector<ZObject*>::iterator i=object_list.begin(); i!=object_list.end(); i++)
		{
			if(!(*i)->Selectable()) continue;
			if((*i)->GetOwner() != our_team) continue;
			if(!(*i)->WithinSelection(map_left, map_right, map_top, map_bottom)) continue;
			select_info.selected_list.push_back(*i);
			//ZLOG("selected:%s\n", (*i)->GetObjectName().c_str());
		}
	}

	//now do this basically to get the correct mouse cursor
	select_info.SetupGroupDetails();
}

bool ZPlayer::CouldCollectSelectables()
{
	int shift_x, shift_y;
	int mouse_x_map, mouse_y_map;
	int map_left, map_right, map_top, map_bottom;
	bool would_be_single_unit;

	zmap.GetViewShift(shift_x, shift_y);

	mouse_x_map = mouse_x + shift_x;
	mouse_y_map = mouse_y + shift_y;

	if(mouse_x_map < lbutton.map_x)
	{
		map_left = mouse_x_map;
		map_right = lbutton.map_x;
	}
	else
	{
		map_left = lbutton.map_x;
		map_right = mouse_x_map;
	}

	if(mouse_y_map < lbutton.map_y)
	{
		map_top = mouse_y_map;
		map_bottom = lbutton.map_y;
	}
	else
	{
		map_top = lbutton.map_y;
		map_bottom = mouse_y_map;
	}

	if(abs(lbutton.map_x - mouse_x_map) <= 1 && abs(lbutton.map_y - mouse_y_map) <= 1)
		would_be_single_unit = true;
	else
		would_be_single_unit = false;

	//so who have we selected
	for(vector<ZObject*>::iterator i=object_list.begin(); i!=object_list.end(); i++)
	{
		ZObject *obj;

		obj = *i;

		//the group leader would be chosen
		//if we were only selecting one unit
		if(would_be_single_unit && obj->GetGroupLeader()) obj = obj->GetGroupLeader();
		
		if(!obj->Selectable()) continue;
		if(obj->GetOwner() != our_team) continue;
		if(!obj->WithinSelection(map_left, map_right, map_top, map_bottom)) continue;
		
		return true;
	}

	return false;
}

void ZPlayer::ClearDevWayPointsOfSelected()
{
	for(vector<ZObject*>::iterator i=select_info.selected_list.begin(); i!=select_info.selected_list.end(); i++)
		(*i)->GetWayPointDevList().clear();
}

void ZPlayer::MapCoordsOfMouseWithHud(int &map_x, int &map_y)
{
	zmap.GetMapCoords(mouse_x, mouse_y, map_x, map_y);
	zhud.OverMiniMap(mouse_x, mouse_y, init_w, init_h, map_x, map_y);
}

void ZPlayer::LoadControlGroup(int n)
{
	if(n<0) return;
	if(n>=10) return;

	//select the group or jump to them?
	if(select_info.GroupIsSelected(n))
	{
		int tx, ty;

		if(select_info.AverageCoordsOfSelected(tx, ty)) FocusCameraTo(tx, ty);
	}
	else
	{
		select_info.LoadGroup(n);
		DetermineCursor();
		ClearDevWayPointsOfSelected();
		GiveHudSelected();
	}
}

bool ZPlayer::UnitNearHostiles(ZObject *obj)
{
	if(!obj) return false;

	for(vector<ZObject*>::iterator i=ols.passive_engagable_olist.begin(); i!=ols.passive_engagable_olist.end(); ++i)
	{
		ZObject *eobj = *i;

		if(obj==eobj) continue;
		if(obj->GetOwner() == eobj->GetOwner()) continue;
		if(eobj->GetOwner() == NULL_TEAM) continue;
		if(eobj->IsDestroyed()) continue;
		if(!obj->WithinAgroRadius(eobj)) continue;

		return true;
	}

	return false;
}

void ZPlayer::AddDevWayPointToSelected()
{
	waypoint new_waypoint;
	bool coords_from_mini_map;

	//MapCoordsOfMouseWithHud(new_waypoint.x, new_waypoint.y);
	zmap.GetMapCoords(mouse_x, mouse_y, new_waypoint.x, new_waypoint.y);
	coords_from_mini_map = zhud.OverMiniMap(mouse_x, mouse_y, init_w, init_h, new_waypoint.x, new_waypoint.y);

	//if(hover_object && !coords_from_mini_map)
	//	new_waypoint.ref_id = hover_object->GetRefID();
	//else
	//	new_waypoint.ref_id = -1;

	//new_waypoint.mode = MOVE_WP;
	new_waypoint.player_given = true;

	for(vector<ZObject*>::iterator i=select_info.selected_list.begin(); i!=select_info.selected_list.end(); i++)
	{
		unsigned char ot, oid;
		unsigned char hot, hoid;
		int hcx, hcy;

		new_waypoint.mode = MOVE_WP;
		new_waypoint.ref_id = -1;
		new_waypoint.attack_to = true;

		if(UnitNearHostiles(*i)) new_waypoint.attack_to = false;
		if(CtrlDown()) new_waypoint.attack_to = true;
		if(AltDown()) new_waypoint.attack_to = false;

		(*i)->GetObjectID(ot, oid);

		//set the mode
		if(hover_object && !coords_from_mini_map)
		{
			new_waypoint.ref_id = hover_object->GetRefID();

			hover_object->GetObjectID(hot, hoid);
			hover_object->GetCenterCords(hcx, hcy);

			if((*i)->CanBeRepaired() && hover_object->CanRepairUnit(our_team))
			{
				new_waypoint.mode = UNIT_REPAIR_WP;
				new_waypoint.x = hcx - 8;
				new_waypoint.y = hcy;
			}
			else
			{
				switch(ot)
				{
				case CANNON_OBJECT:
					if(hot == MAP_ITEM_OBJECT && hoid == FLAG_ITEM) break;

					//enemy for attacking
					if(hover_object->GetOwner() != our_team)
						new_waypoint.mode = ATTACK_WP;
					break;
				case VEHICLE_OBJECT:
					if(hot == MAP_ITEM_OBJECT && hoid == FLAG_ITEM) break;

					//it a crane?
					if(oid == CRANE && hover_object->CanBeRepairedByCrane(our_team))
					{
						new_waypoint.mode = CRANE_REPAIR_WP;
						hover_object->GetCraneCenter(new_waypoint.x, new_waypoint.y);
						break;
					}

					//enter fort?
					if(hover_object_can_enter_fort)
					{
						new_waypoint.mode = ENTER_FORT_WP;
						break;
					}

					//they attack everything, unless you are selecting an APC
					if(hover_object->GetOwner() != our_team)
						new_waypoint.mode = ATTACK_WP;

					break;
				case ROBOT_OBJECT:
					if(hot == MAP_ITEM_OBJECT && hoid == FLAG_ITEM) break;

					//grenades?
					if((hot == MAP_ITEM_OBJECT && hoid == GRENADES_ITEM) && (*i)->CanPickupGrenades())
					{
						new_waypoint.mode = PICKUP_GRENADES_WP;
						break;
					}

					//capturable item?
					if(hover_object->GetOwner() == NULL_TEAM &&
						(hot == CANNON_OBJECT || hot == VEHICLE_OBJECT))
					{
						new_waypoint.mode = ENTER_WP;
						break;
					}

					//enter fort?
					if(hover_object_can_enter_fort)
					{
						new_waypoint.mode = ENTER_FORT_WP;
						break;
					}

					//attack
					if(hover_object->GetOwner() != our_team)
						new_waypoint.mode = ATTACK_WP;


					break;
				}
			}
		}

		//add it to the list
		(*i)->GetWayPointDevList().push_back(new_waypoint);
	}

	//setting a rally point?
	if(gui_window && gui_window->GetBuildingObj() && !select_info.selected_list.size())
	{
		waypoint new_rallypoint;

		MapCoordsOfMouseWithHud(new_rallypoint.x, new_rallypoint.y);
		new_rallypoint.ref_id = -1;
		new_rallypoint.mode = MOVE_WP;
		new_rallypoint.player_given = true;
		new_rallypoint.attack_to = true;

		gui_window->GetBuildingObj()->GetWayPointDevList().push_back(new_rallypoint);
	}

	SetPcursor();
}

bool ZPlayer::DevWayPointsNoWay()
{
	if(select_info.selected_list.size() != 1) return false;

	ZObject *obj;

	obj = select_info.selected_list[0];

	if(!obj->GetWayPointDevList().size()) return false;
	if(obj->GetWayPointDevList()[0].mode != ATTACK_WP) return false;

	ZObject *vobj;

	vobj = ZObject::GetObjectFromID(obj->GetWayPointDevList()[0].ref_id, object_list);

	if(!vobj) return false;

	unsigned char a_ot, a_oid, v_ot, v_oid;

	obj->GetObjectID(a_ot, a_oid);
	vobj->GetObjectID(v_ot, v_oid);

	return zunitrating.CrossReference(a_ot, a_oid, v_ot, v_oid) == UCR_WILL_DIE;

}

void ZPlayer::SendDevWayPointsOfObj(ZObject *obj)
{
	char *data;
	int size;

	if(!obj) return;

	//are we registered?
	if(!is_registered)
	{
		unsigned char ot, oid;

		obj->GetObjectID(ot, oid);

		if(UnitRequiresActivation(ot, oid))
		{
			AddNewsEntry("move unit error: registration required, please visit www.nighsoft.com");
			return;
		}
	}

	CreateWaypointSendData(obj->GetRefID(), obj->GetWayPointDevList(), data, size);

	//no data made?
	if(!data) return;

	//send
	client_socket.SendMessage(SEND_WAYPOINTS, data, size);

	//free data
	free(data);
}

void ZPlayer::SendDevWayPointsOfSelected()
{
	vector<ZObject*>::iterator i;
	vector<waypoint>::iterator j;
	bool no_way = false;
	ZObject *remove_obj_from_selected = nullptr;

	no_way = DevWayPointsNoWay();

	if(AsciiDown('z'))
	{
		//send only the nearest to its target

		if(select_info.selected_list.size() && (*select_info.selected_list.begin())->GetWayPointDevList().size())
		{
			waypoint &wp = *(*select_info.selected_list.begin())->GetWayPointDevList().begin();
			
			remove_obj_from_selected = ZObject::NearestObjectToCoords(select_info.selected_list, wp.x, wp.y);

			if(remove_obj_from_selected) SendDevWayPointsOfObj(remove_obj_from_selected);
		}
	}
	else
	{
		for(i=select_info.selected_list.begin(); i!=select_info.selected_list.end(); i++)
			SendDevWayPointsOfObj(*i);
	}

	i=select_info.selected_list.begin();
	if(i!=select_info.selected_list.end() && (*i)->GetWayPointDevList().size())
	{
		j = (*i)->GetWayPointDevList().end();
		j--;

		ShowPcursor(j->x, j->y);
	}

	//ok its sent, so clear the lists
	ClearDevWayPointsOfSelected();

	//tell the hud we gave a command
	zhud.GiveSelectedCommand(no_way);

	//did a one unit per target?
	if(remove_obj_from_selected)
	{
		select_info.RemoveFromSelected(remove_obj_from_selected);
		if(zhud.GetSelectedObject() == remove_obj_from_selected || !select_info.selected_list.size()) GiveHudSelected();
		DetermineCursor();
	}

	//setting a rally point?
	if(gui_window && gui_window->GetBuildingObj() && !select_info.selected_list.size())
	{
		ZObject *obj;
		char *data;
		int size;

		obj = gui_window->GetBuildingObj();

		CreateWaypointSendData(obj->GetRefID(), obj->GetWayPointDevList(), data, size);

		//no data made?
		if(data)
		{
			//send
			client_socket.SendMessage(SEND_RALLYPOINTS, data, size);

			//free data
			free(data);
		}

		obj->GetWayPointDevList().clear();
	}
}

void ZPlayer::GiveHudSelected()
{
	if(select_info.selected_list.size())
	{
		int choice = rand()%select_info.selected_list.size();
		zhud.SetSelectedObject(select_info.selected_list[choice]);
	}
	else
		zhud.SetSelectedObject(nullptr);
}

void ZPlayer::DeleteObjectCleanUp(ZObject *obj)
{
	unsigned char ot, oid;

	if(!obj) return;

	obj->GetObjectID(ot, oid);

	//if(ot == MAP_ITEM_OBJECT && oid == ROCK_ITEM) ORock::SetupRockRenders(zmap, object_list);
	if(ot == MAP_ITEM_OBJECT && oid == ROCK_ITEM) ORock::EditRockRender(zmap, object_list, obj, false);

	obj->DeathMapEffects(zmap);

	if(hover_object == obj) hover_object = nullptr;

	select_info.DeleteObject(obj);

	zhud.DeleteObject(obj);

	vector<ZObject*>::iterator i;

	zod_lauf_del++;
	zod_lauf_elem += (unsigned long)object_list.size();
	zod_lauf_elem_del += (unsigned long)object_list.size();

	for(i=object_list.begin();i!=object_list.end();i++)
		(*i)->RemoveObject(obj);

	ProcessChangeObjectAmount();
}

/* NUR VORMERKEN -- ausgefuehrt wird einmal je Bild in FlushObjectAmount().
 *
 * Gerufen wird das aus den Paketbehandlern fuer "neues Objekt" und "Objekt
 * geloescht", und ein Bild leert IMMER alle anstehenden Pakete
 * (ProcessSocketEvents). Ein Robotertrupp oder eine Explosion mit vielen
 * Toten erzeugt damit einen Schwall, und jedes einzelne Paket loeste bisher
 * zwei volle Durchlaeufe ueber object_list aus -- auf einer grossen Karte
 * rund 3000 Elemente, mal Schwallgroesse, in EINEM Bild.
 *
 * Gleichwertig, weil beide Ergebnisse (Knopfzustand, team_units_available)
 * erst NACH der Paketschleife gelesen werden: ProcessGame und RenderScreen
 * laufen danach. Innerhalb der Schleife liest sie niemand. */
void ZPlayer::ProcessChangeObjectAmount()
{
	objekt_anzahl_faellig = true;
}

void ZPlayer::FlushObjectAmount()
{
	if(!objekt_anzahl_faellig) return;

	objekt_anzahl_faellig = false;

	//make the buttons available that let you cycle through your units
	ReSetupButtons();

	//reset hud etc
	CheckUnitLimitReached();
	zhud.SetUnitAmount(team_units_available[our_team]);
}

void ZPlayer::ClearAsciiStates()
{
	for(int i=0;i<ASCII_DOWN_MAX;i++) ascii_down[i] = false;
}

void ZPlayer::SetAsciiState(int c, bool is_down)
{
	c -= 'a';

	if(c<0) return;
	if(c>=ASCII_DOWN_MAX) return;

	ascii_down[c] = is_down;
}

bool ZPlayer::AsciiDown(int c)
{
	c -= 'a';

	if(c<0) return false;
	if(c>=ASCII_DOWN_MAX) return false;

	return ascii_down[c];
}

bool ZPlayer::ShiftDown()
{
	return lshift_down || rshift_down;
}

bool ZPlayer::CtrlDown()
{
	return lctrl_down || rctrl_down;
}

bool ZPlayer::AltDown()
{
	return lalt_down || ralt_down;
}

bool ZPlayer::IsOverHUD(int x, int y, int w, int h)
{
	if(x + w >= init_w - HUD_WIDTH) return true;
	if(y + h >= init_h - HUD_HEIGHT) return true;

	return false;
}

void ZPlayer::MainMenuMove(double px, double py)
{
	for(vector<ZGuiMainMenuBase*>::iterator i=gui_menu_list.begin(); i!=gui_menu_list.end(); ++i)
		(*i)->Move(px, py);
}

bool ZPlayer::MainMenuMotion()
{
	for(vector<ZGuiMainMenuBase*>::iterator i=gui_menu_list.begin(); i!=gui_menu_list.end(); ++i)
	{
		int px, py;

		(*i)->GetCoords(px, py);

		if((*i)->Motion(mouse_x, mouse_y))
		{
			int x,y,w,h;

			(*i)->GetCoords(x, y);
			(*i)->GetDimensions(w, h);

			if(IsOverHUD(x,y,w,h)) zhud.ReRenderAll();
			else if(IsOverHUD(px,py,w,h)) zhud.ReRenderAll();

			return true;
		}
	}

	return false;
}

bool ZPlayer::MainMenuWheelUp()
{
	for(vector<ZGuiMainMenuBase*>::iterator i=gui_menu_list.begin(); i!=gui_menu_list.end(); ++i)
		if((*i)->WheelUpButton())
			return true;

	return false;
}

bool ZPlayer::MainMenuWheelDown()
{
	for(vector<ZGuiMainMenuBase*>::iterator i=gui_menu_list.begin(); i!=gui_menu_list.end(); ++i)
		if((*i)->WheelDownButton())
			return true;

	return false;
}

bool ZPlayer::MainMenuKeyPress(int c)
{
	for(vector<ZGuiMainMenuBase*>::iterator i=gui_menu_list.begin(); i!=gui_menu_list.end(); ++i)
		if((*i)->KeyPress(c))
			return true;

	return false;
}

bool ZPlayer::MainMenuAbsorbLClick()
{
	for(vector<ZGuiMainMenuBase*>::iterator i=gui_menu_list.begin(); i!=gui_menu_list.end(); ++i)
		if((*i)->Click(mouse_x, mouse_y))
		{
			gmm_flag &the_flags = (*i)->GetGMMFlags();

			//move this to the front
			if(gui_menu_list.begin() != i)
			{
				//this method would be a hell of a no no
				//if we weren't immediately leaving the loop anyways
				ZGuiMainMenuBase* temp = *i;
				gui_menu_list.erase(i);
				gui_menu_list.insert(gui_menu_list.begin(), temp);
			}

			if(the_flags.set_volume) SetSoundSetting(the_flags.set_volume_value);

			if(the_flags.set_game_speed) 
			{
				float_packet the_data;

				the_data.game_speed = the_flags.set_game_speed_value;
				client_socket.SendMessage(SET_GAME_SPEED, (char*)&the_data, sizeof(float_packet));
			}

			return true;
		}

	return false;
}

bool ZPlayer::MainMenuAbsorbLUnClick()
{
	for(vector<ZGuiMainMenuBase*>::iterator i=gui_menu_list.begin(); i!=gui_menu_list.end(); ++i)
		if((*i)->UnClick(mouse_x, mouse_y))
		{
			gmm_flag &the_flags = (*i)->GetGMMFlags();

			if(the_flags.open_main_menu) LoadMainMenu(the_flags.open_main_menu_type, false, the_flags.warning_flags);

			if(the_flags.reshuffle_teams) client_socket.SendMessage(RESHUFFLE_TEAMS, nullptr, 0);

			if(the_flags.change_team) SendPlayerTeam(the_flags.change_team_type);

			if(the_flags.reset_map) client_socket.SendMessage(RESET_MAP, nullptr, 0);

			if(the_flags.quit_game) ExitProgram();

			if(the_flags.pause_game) SendSetPaused(true);

			if(the_flags.start_bot) 
			{
				int_packet the_data;

				the_data.team = the_flags.start_bot_team;
				client_socket.SendMessage(START_BOT_EVENT, (char*)&the_data, sizeof(int_packet));
			}

			if(the_flags.stop_bot) 
			{
				int_packet the_data;

				the_data.team = the_flags.stop_bot_team;
				client_socket.SendMessage(STOP_BOT_EVENT, (char*)&the_data, sizeof(int_packet));
			}

			if(the_flags.change_map) 
			{
				int_packet the_data;

				the_data.map_num = the_flags.change_map_number;
				client_socket.SendMessage(SELECT_MAP, (char*)&the_data, sizeof(int_packet));
			}

			return true;
		}

	return false;
}

bool ZPlayer::GuiAbsorbLClick()
{
	int shift_x, shift_y;
	int map_x, map_y;

	//do we have a menu window?
	if(active_menu)
	{
		zmap.GetViewShift(shift_x, shift_y);

		map_x = mouse_x + shift_x;
		map_y = mouse_y + shift_y;

		if(active_menu->Click(map_x, map_y))
			return true;
	}

	//what about factory list?
	if(gui_factory_list && gui_factory_list->IsVisible())
	{
		zmap.GetViewShift(shift_x, shift_y);

		map_x = mouse_x + shift_x;
		map_y = mouse_y + shift_y;

		if(gui_factory_list->Click(map_x, map_y))
		{
			if(gui_factory_list->GetGFlags().jump_to_building)
			{
				//run to it
				int ox, oy;
				ZObject *obj;

				obj = GetObjectFromID(gui_factory_list->GetGFlags().bref_id, object_list);

				if(obj)
				{
					obj->GetCenterCords(ox, oy);
					FocusCameraTo(ox, oy);

					//make a gui window for this building
					ObjectMakeGuiWindow(obj);
					//if(gui_window) DeleteCurrentGuiWindow();
					//if(gui_window = obj->MakeGuiWindow())
					//{
					//	//give it the build list
					//	gui_window->SetBuildList(&buildlist);

					//	//have its buildings dev rallypoints cleared
					//	if(gui_window->GetBuildingObj())
					//		gui_window->GetBuildingObj()->GetWayPointDevList().clear();
					//}
				}
			}

			return true;
		}
	}

	//do we have a gui window, and did it take the click?
	if(gui_window)
	{
		zmap.GetViewShift(shift_x, shift_y);

		map_x = mouse_x + shift_x;
		map_y = mouse_y + shift_y;

		if(gui_window->Click(map_x, map_y))
			return true;
		else
			DeleteCurrentGuiWindow();
	}
	

	//move forward?
	if(CouldCollectSelectables()) return false;

	//do we start another gui window?
	if(hover_object && ObjectMakeGuiWindow(hover_object)) return true;

	//if(hover_object && hover_object->GetOwner() != NULL_TEAM && hover_object->GetOwner() == our_team && (gui_window = hover_object->MakeGuiWindow()))
	//{
	//	////give it the build list
	//	//gui_window->SetBuildList(&buildlist);

	//	////have its buildings dev rallypoints cleared
	//	//if(gui_window->GetBuildingObj())
	//	//	gui_window->GetBuildingObj()->GetWayPointDevList().clear();

	//	return true;
	//}

	return false;
}

bool ZPlayer::GuiAbsorbLUnClick()
{
	int shift_x, shift_y;
	int map_x, map_y;

	//do we have a menu window?
	if(active_menu)
	{
		zmap.GetViewShift(shift_x, shift_y);

		map_x = mouse_x + shift_x;
		map_y = mouse_y + shift_y;

		if(active_menu->UnClick(map_x, map_y))
		{
			gui_flags &glfags = active_menu->GetGFlags();

			if(glfags.do_login)
			{
				login_name = active_menu->GetGFlags().login_name;
				login_password = active_menu->GetGFlags().login_password;

				SendLogin();
			}

			if(glfags.open_createuser)
			{
				active_menu = create_user_menu;
			}

			if(glfags.do_createuser)
			{
				SendCreateUser(
					active_menu->GetGFlags().user_name,
					active_menu->GetGFlags().login_name,
					active_menu->GetGFlags().login_password,
					active_menu->GetGFlags().email);
			}

			if(glfags.open_login)
			{
				active_menu = login_menu;
			}

			return true;
		}
	}

	//what about factory list?
	if(gui_factory_list && gui_factory_list->IsVisible())
	{
		zmap.GetViewShift(shift_x, shift_y);

		map_x = mouse_x + shift_x;
		map_y = mouse_y + shift_y;

		if(gui_factory_list->UnClick(map_x, map_y))
			return true;
	}

	//do we have a gui window, and did it take the click?
	if(gui_window)
	{
		zmap.GetViewShift(shift_x, shift_y);

		map_x = mouse_x + shift_x;
		map_y = mouse_y + shift_y;

		if(gui_window->UnClick(map_x, map_y))
		{
			if(gui_window->GetGFlags().send_new_production)
			{
				start_building_packet the_data;

				the_data.ref_id = gui_window->GetGFlags().pref_id;
				the_data.ot = gui_window->GetGFlags().pot;
				the_data.oid = gui_window->GetGFlags().poid;

				client_socket.SendMessage(START_BUILDING, (const char*)&the_data, sizeof(start_building_packet));
			}

			if(gui_window->GetGFlags().send_stop_production)
			{
				int the_data;

				the_data = gui_window->GetGFlags().pref_id;

				client_socket.SendMessage(STOP_BUILDING, (const char*)&the_data, sizeof(int));
			}

			if(gui_window->GetGFlags().place_cannon)
			{
				place_cannon_ref_id = gui_window->GetGFlags().cref_id;
				place_cannon_oid = gui_window->GetGFlags().coid;
				place_cannon_left = gui_window->GetGFlags().cleft;
				place_cannon_right = gui_window->GetGFlags().cright;
				place_cannon_top = gui_window->GetGFlags().ctop;
				place_cannon_bottom = gui_window->GetGFlags().cbottom;

				InitPlaceCannon();
			}

			if(gui_window->GetGFlags().send_new_queue_item)
			{
				add_building_queue_packet the_data;

				the_data.ref_id = gui_window->GetGFlags().qref_id;
				the_data.ot = gui_window->GetGFlags().qot;
				the_data.oid = gui_window->GetGFlags().qoid;

				client_socket.SendMessage(ADD_BUILDING_QUEUE, (const char*)&the_data, sizeof(add_building_queue_packet));
			}

			if(gui_window->GetGFlags().send_cancel_queue_item)
			{
				cancel_building_queue_packet the_data;

				the_data.ref_id = gui_window->GetGFlags().qcref_id;
				the_data.ot = gui_window->GetGFlags().qcot;
				the_data.oid = gui_window->GetGFlags().qcoid;
				the_data.list_i = gui_window->GetGFlags().qc_i;

				client_socket.SendMessage(CANCEL_BUILDING_QUEUE, (const char*)&the_data, sizeof(cancel_building_queue_packet));
			}

			return true;
		}
	}

	return false;
}

void ZPlayer::InitPlaceCannon()
{
	place_cannon_ok_img = nullptr;
	place_cannon_nok_img = nullptr;

	//get images
	switch(place_cannon_oid)
	{
	case GATLING:
		place_cannon_ok_img = CGatling::GetPlaceImage(our_team);
		place_cannon_nok_img = CGatling::GetNPlaceImage(our_team);
		break;
	case GUN:
		place_cannon_ok_img = CGun::GetPlaceImage(our_team);
		place_cannon_nok_img = CGun::GetNPlaceImage(our_team);
		break;
	case HOWITZER:
		place_cannon_ok_img = CHowitzer::GetPlaceImage(our_team);
		place_cannon_nok_img = CHowitzer::GetNPlaceImage(our_team);
		break;
	case MISSILE_CANNON:
		place_cannon_ok_img = CMissileCannon::GetPlaceImage(our_team);
		place_cannon_nok_img = CMissileCannon::GetNPlaceImage(our_team);
		break;
	default:
		return;
		break;
	}

	//make it real
	place_cannon = true;

	//set cords
	SetPlaceCannonCords();
}

void ZPlayer::SetPlaceCannonCords()
{
	int map_x, map_y;
	int shift_x, shift_y;

	if(!place_cannon) return;

	zmap.GetViewShift(shift_x, shift_y);

	map_x = mouse_x + shift_x;
	map_y = mouse_y + shift_y;

	place_cannon_tx = map_x / 16;
	place_cannon_ty = map_y / 16;

	//ok or not ok?
}

void ZPlayer::RenderPlaceCannon()
{
	SDL_Rect from_rect, to_rect;
	int map_x, map_y;
	int shift_x, shift_y;

	if(!place_cannon) return;

	map_x = (place_cannon_tx * 16);
	map_y = (place_cannon_ty * 16);

	zmap.RenderZSurface(&place_cannon_ok_img, map_x, map_y);
	//if(zmap.GetBlitInfo(place_cannon_ok_img, map_x, map_y, from_rect, to_rect))
	//		SDL_BlitSurface( place_cannon_ok_img, &from_rect, screen, &to_rect);
}

bool ZPlayer::DoPlaceCannon()
{
	struct place_cannon_packet the_data;

	if(!place_cannon) return false;

	place_cannon = false;

	the_data.ref_id = place_cannon_ref_id;
	the_data.oid = place_cannon_oid;
	the_data.tx = place_cannon_tx;
	the_data.ty = place_cannon_ty;

	client_socket.SendMessage(PLACE_CANNON, (const char*)&the_data, sizeof(place_cannon_packet));

	return true;
}


void ZPlayer::DeleteCurrentGuiWindow()
{
	if(gui_window)
	{
		//ZLOG("deleted gui window\n");
		delete gui_window;
		gui_window = nullptr;
	}
}

void ZPlayer::ProcessUnicode(int key)
{
	//ZLOG("ProcessUnicode:: unicode:%d\n", key);

	//main menu took it?
	if(MainMenuKeyPress(key))
	{

	}

	//exit out if the menu took the key
	if(active_menu && active_menu->KeyPress(key))
	{
		gui_flags &glfags = active_menu->GetGFlags();

		if(glfags.do_login)
		{
			login_name = active_menu->GetGFlags().login_name;
			login_password = active_menu->GetGFlags().login_password;

			SendLogin();
		}
		//SendCreateUser

		if(glfags.do_createuser)
		{
			SendCreateUser(
				active_menu->GetGFlags().user_name,
				active_menu->GetGFlags().login_name,
				active_menu->GetGFlags().login_password,
				active_menu->GetGFlags().email);
		}

		return;
	}

	if(key == 13)
	{
		if(collect_chat_message)
		{
			//send it
			client_socket.SendMessageAscii(SEND_CHAT, chat_message.c_str());

			chat_message.clear();
			collect_chat_message = false;
		}
		else
		{
			collect_chat_message = true;
		}

		zhud.ShowChatMessage(collect_chat_message);
	}
	else
	{
		if(collect_chat_message)
		{
			if(key == 8) //delete key
			{
				if(chat_message.length())
					chat_message.erase(chat_message.length()-1,1);

				//ZLOG("current chat message:'%s'\n", chat_message.c_str());
			}
			else
			{
				//add it to the string
				chat_message += key;
				//ZLOG("current chat message:'%s'\n", chat_message.c_str());
			}

			zhud.SetChatMessage(chat_message);
		}
		else if(key == '/')
		{
			//start chat message into a command
			collect_chat_message = true;

			chat_message = "/";
			zhud.ShowChatMessage(collect_chat_message);
			zhud.SetChatMessage(chat_message);
		}
		else if(key == 'p' || key == 'P')
		{
			LoadMainMenu(GMM_PLAYER_LIST, true);
			//DisplayPlayerList();
		}
		else if(key == 'h' || key == 'H')
		{
			//toggle on/off
			show_chat_history = !show_chat_history;
		}
		else if(key == 'm' || key == 'M')
		{
			if(SDL_WM_GrabInput(SDL_GRAB_QUERY) == SDL_GRAB_ON)
			{
				SDL_WM_GrabInput(SDL_GRAB_OFF);
				AddNewsEntry("mouse released");
			}
			else
			{
				SDL_WM_GrabInput(SDL_GRAB_ON);
				AddNewsEntry("mouse taken");
			}
		}
		else if(!AltDown() && (key == 'v' || key == 'V'))
		{
			V_Button();
		}
		else if(AltDown() && (key == 'v' || key == 'V'))
		{
			SetNextSoundSetting();
		}
		else if(key == 22) //means ctrl + v ?
		{
			SelectAllOfType(VEHICLE_OBJECT);
		}
		else if(key == 18) //means ctrl + r ?
		{
			SelectAllOfType(ROBOT_OBJECT);
		}
		else if(key == 3) //means ctrl + c ?
		{
			SelectAllOfType(CANNON_OBJECT);
		}
		else if(key == 1) //means ctrl + a ?
		{
			SelectAllOfType();
		}
		else if(key == 'r' || key == 'R')
		{
			R_Button();
		}
		else if(key == 'g' || key == 'G')
		{
			G_Button();
		}
		else if(key == 'b' || key == 'B')
		{
			//DisplayFactoryProductionList();
			if(gui_factory_list) gui_factory_list->ToggleShow();
		}
		else if(key == ' ')
		{
			DoSpaceBarEvent();
		}
	}

}

void ZPlayer::SendVoteYes()
{
	client_socket.SendMessage(VOTE_YES, nullptr, 0);
}

void ZPlayer::SendVoteNo()
{
	client_socket.SendMessage(VOTE_NO, nullptr, 0);
}

void ZPlayer::SendVotePass()
{
	client_socket.SendMessage(VOTE_PASS, nullptr, 0);
}

void ZPlayer::SendLogin()
{
	//ask if we need to display the login menu
	client_socket.SendMessage(REQUEST_LOGINOFF, nullptr, 0);

	//send a login if we have it
	if(login_name.length() && login_password.length())
	{
		string send_str;

		send_str = login_name + "," + login_password;

		client_socket.SendMessageAscii(SEND_LOGIN, send_str.c_str());
	}
}

void ZPlayer::SendCreateUser(string username, string lname, string lpass, string email)
{
	string send_str;

	send_str = username + "," + lname + "," + lpass + "," + email;

	client_socket.SendMessageAscii(CREATE_USER, send_str.c_str());
}

void ZPlayer::SendSetPaused(bool paused)
{
	update_game_paused_packet packet;

	packet.game_paused = paused;

	client_socket.SendMessage(SET_GAME_PAUSED, (const char*)&packet, sizeof(update_game_paused_packet));
}

void ZPlayer::InitMenus()
{
	active_menu = nullptr;

	login_menu = new GWLogin(&ztime);
	create_user_menu = new GWCreateUser(&ztime);
}

void ZPlayer::LoadMainMenu(int menu_type, bool kill_if_open, gmm_warning_flag warning_flags)
{
	ZGuiMainMenuBase *new_menu = nullptr;

	//already loaded?
	for(vector<ZGuiMainMenuBase*>::iterator i=gui_menu_list.begin(); i!=gui_menu_list.end(); ++i)
		if((*i)->GetMenuType() == menu_type)
		{
			if(kill_if_open)
			{
				delete *i;
				gui_menu_list.erase(i);
			}
			else
			{
				//move this to the front
				if(gui_menu_list.begin() != i)
				{
					//this method would be a hell of a no no
					//if we weren't immediately leaving the loop anyways
					ZGuiMainMenuBase* temp = *i;
					gui_menu_list.erase(i);
					gui_menu_list.insert(gui_menu_list.begin(), temp);
				}
			}

			return;
		}

	//load it
	switch(menu_type)
	{
	case GMM_MAIN_MAIN: new_menu = new GMMMainMenu(); break;
	case GMM_CHANGE_TEAMS: new_menu = new GMMChangeTeams(); break;
	case GMM_MANAGE_BOTS: new_menu = new GMMManageBots(); break;
	case GMM_PLAYER_LIST: new_menu = new GMMPlayerList(); break;
	case GMM_SELECT_MAP: new_menu = new GMMSelectMap(); break;
	case GMM_OPTIONS: new_menu = new GMMOptions(); break;
	case GMM_WARNING: new_menu = new GMMWarning(warning_flags); break;
	default: ZLOG("ZPlayer::LoadMainMenu: bad menu_type:%d\n", menu_type); break;
	}

	if(new_menu) 
	{
		new_menu->SetCenterCoords(init_w >> 1, init_h >> 1);
		new_menu->SetPlayerInfoList(&player_info);
		new_menu->SetSelectableMapList(&selectable_map_list);
		new_menu->SetPlayerTeam((int*)&our_team);
		new_menu->SetSoundSetting(&sound_setting);
		new_menu->SetZTime(&ztime);

		//gui_menu_list.push_back(new_menu);
		gui_menu_list.insert(gui_menu_list.begin(), new_menu);
	}
}

/* ====================================================================
 * Die Zwischensequenzen, in der Reihenfolge des Originals
 * ====================================================================
 *
 * Belegt aus der Tabelle in ZED.EXE (ab 0x183a2d) und aus levels.dat:
 * Die Kampagne hat FUENF Planeten zu je VIER Gebieten, in fester
 * Reihenfolge -- Desert, Volcanic, Arctic, Jungle, City. Je Planet gibt
 * es vier Gruppen zu vier Eintraegen:
 *
 *   lost1..4    Niederlage -- die Ziffer ist das Gebiet (1..4)
 *   wint1..3    Gebiet 1, 2 oder 3 gewonnen
 *   trav1..3    Weiterreise 1->2, 2->3, 3->4
 *   winpl       Gebiet 4 gewonnen = Planet erobert
 *   plnt        Ankunft auf dem Planeten, vor Gebiet 1
 *
 * Die Folge lautet also:
 *
 *   logo intro intro2  dplnt
 *   G1 dwint1 dtrav1  G2 dwint2 dtrav2  G3 dwint3 dtrav3  G4 dwinpl
 *   vplnt  G5 ... vwinpl
 *   aplnt  ... awinpl
 *   jplnt  ... jwinpl
 *   (City, ohne Ankunftsfilm)  ... G20  outro creds
 *
 * DIE FOLGE HAENGT NICHT AN DER KARTE, und das war mein Fehler im ersten
 * Entwurf: Dort kam der Planet aus terrain_type der geladenen Karte. Eine
 * Volcanic-Karte als erste Runde spielte damit sofort "Ankunft auf dem
 * Vulkanplaneten" -- vom Nutzer gemeldet und zu Recht beanstandet. Im
 * Original beginnt JEDE Kampagne auf dem Wuestenplaneten, und vplnt kommt
 * erst, wenn die Wueste erobert ist.
 *
 * Deshalb ist video_planet ein Platz in der KAMPAGNENFOLGE (0..4) und
 * nicht der Planet der Karte. Die Karte bestimmt weiterhin die Musik und
 * das Aussehen -- aber nicht, welcher Film laeuft.
 *
 * CITY IST DER SONDERFALL, und zwar im Original selbst: Es gibt weder
 * cplnt noch cwinpl -- beide Dateien fehlen auf der CD, und in der
 * Tabelle stehen auf allen vier Sonderplaetzen des City-Blocks
 * stattdessen "outro". City ist der letzte Planet; wer ihn erobert, sieht
 * den Abspann.
 *
 * EINE NIEDERLAGE LAESST DIE STUFE STEHEN -- im Original wiederholt man
 * dasselbe Gebiet.
 */

/* Reihenfolge der Kampagne. NICHT die Aufzaehlung planet_type benutzen:
 * die ist zwar zufaellig gleich sortiert, aber sie beschreibt das
 * Aussehen einer Karte, nicht den Ablauf der Kampagne. Zwei Dinge, die
 * gleich aussehen und Verschiedenes bedeuten, gehoeren getrennt. */
static const char zod_video_planet[5] = { 'd', 'v', 'a', 'j', 'c' };

#define ZOD_VIDEO_CITY 4        /* letzter Planet, ohne plnt und winpl */

/* Kartendatei -> Levelnummer und Levelbezeichnung fuer das Ladebild.
 *
 * Im Original steht dort "Level 01" und darunter "Virgin Soldiers". Der
 * Name kommt aus z/levels.dat der CD; ZExtract legt ihn als
 * levelnames.dat neben die Archive (40 Saetze zu 20 Byte, Level N bei
 * (N-1)*20).
 *
 * Die Nummer steckt im DATEINAMEN: p02_bb_orig01.map ist Level 1. Der
 * Name IN der Kartendatei taugt nicht -- dort steht durchweg
 * "clone_map". Und die Nummer aus der Position in map_list.txt zu
 * nehmen waere falsch, sobald jemand die Liste umstellt.
 */
void ZPlayer::SetLadeName(const char *p)
{
	const char *b;
	const char *o;
	int i;

	lade_name[0] = 0;
	lade_level = 0;

	if(!p || !*p) return;

	/* Hinter den letzten Trenner. AmigaOS kennt BEIDE -- '/' im Pfad und
	 * ':' hinter dem Volume. */
	b = p;

	for(const char *q = p; *q; q++)
		if(*q == '/' || *q == ':' || *q == 0x5C) b = q + 1;

	/* "orig" plus Ziffern: p02_bb_orig01.map -> 1 */
	for(o = b; o[0] && o[1] && o[2] && o[3]; o++)
		if(o[0] == 'o' && o[1] == 'r' && o[2] == 'i' && o[3] == 'g')
		{
			const char *z = o + 4;
			int n = 0, stellen = 0;

			while(*z >= '0' && *z <= '9' && stellen < 3)
			{
				n = n * 10 + (*z - '0');
				z++; stellen++;
			}

			if(stellen) lade_level = n;

			break;
		}

	/* Namen nachschlagen. Fehlt die Datei, bleibt es bei der Nummer --
	 * das Spiel laeuft dann wie vorher, nur ohne Bezeichnung. */
	if(lade_level >= 1 && lade_level <= 40)
	{
		char pfad[256];
		FILE *f;

		snprintf(pfad, sizeof(pfad), "%s/levelnames.dat", zod_pack_dir());

		f = fopen(pfad, "rb");

		if(f)
		{
			char satz[21];

			if(!fseek(f, (long)(lade_level - 1) * 20, SEEK_SET)
			&& fread(satz, 1, 20, f) == 20)
			{
				satz[20] = 0;

				for(i = 0; satz[i] && i < (int)sizeof(lade_name) - 1; i++)
					lade_name[i] = satz[i];

				lade_name[i] = 0;

				while(i > 0 && lade_name[i-1] == ' ') lade_name[--i] = 0;
			}

			fclose(f);
		}
	}

	ZLOG("Ladebild: Level %d, \"%s\" (aus %s)\n", lade_level, lade_name, b);
}

/* Auf die Entscheidung am Statistikbildschirm warten.
 *
 * DREI KNOEPFE, als Text sichtbar und anklickbar: RETRY, CONTINUE, QUIT.
 * Vorher entschied die Bildhaelfte -- das war nicht zu sehen und nicht zu
 * erraten; der Nutzer hat es zu Recht beanstandet.
 *
 * Getroffen wird ueber das Rechteck des jeweiligen Textes, umgerechnet in
 * BILDkoordinaten (zod_schirm_maus). Dazu die Anfangsbuchstaben R, C, Q.
 *
 * ZEITGRENZE, und sie ist kein Beiwerk: ohne sie bliebe jeder
 * unbeaufsichtigte Messlauf hier stehen, und der Mitschnitt saehe aus wie
 * ein Haenger. Nach 20 Sekunden ohne Eingabe geht es weiter wie bisher --
 * mit einer Protokollzeile, damit das nicht wie eine Entscheidung des
 * Nutzers aussieht.
 *
 * Rueckgabe: 0 = weiter, 1 = beenden, 2 = dieselbe Karte noch einmal.
 */
/* Welcher Knopf liegt unter (bx,by)? -1 = keiner.
 *
 * Die Hoehe ist die der grossen Schrift (19 Punkte) plus etwas Luft --
 * genau die Flaeche, die der Text belegt. Eine grosszuegigere Flaeche
 * waere hier falsch: die drei Knoepfe stehen dicht beieinander. */
int ZPlayer::KnopfUnter(int bx, int by)
{
	int t;

	for(t = 0; t < 3; t++)
		if(by >= knopf_y_o[t] && by < knopf_y_o[t] + 20
		&& bx >= knopf_x[t] && bx < knopf_x[t] + knopf_b[t])
			return t;

	return -1;
}

/* Die drei Knoepfe zeichnen; der unter dem Zeiger hell, die uebrigen
 * gedimmt -- so wie das Original sie zeigt (gemessen Platz 2 statt 3).
 *
 * Die Flaechen werden VORHER aus dem Bild zurueckgeholt, sonst bliebe
 * die helle Fassung als Schmutz unter der gedimmten stehen. */
void ZPlayer::ZeichneKnoepfe(int bx, int by)
{
	static const char *wort[3] = { "QUIT", "CONTINUE", "RETRY" };
	int t, unter = KnopfUnter(bx, by);

	zod_schirm_schrift("gross", 1);

	for(t = 0; t < 3; t++)
	{
		zod_schirm_putzen(knopf_x[t], knopf_y_o[t], knopf_b[t], 20);
		zod_schirm_hell(t == unter);
		zod_schirm_text(knopf_x[t], knopf_y_o[t], wort[t]);
	}

	zod_schirm_hell(1);
}

int ZPlayer::StatistikWarten()
{
	int warte = 0;

	for(;;)
	{
		SDL_Event ev;

		while(SDL_PollEvent(&ev))
		{
			int wahl = -1;

			if(ev.type == SDL_QUIT) wahl = 1;
			else if(ev.type == SDL_KEYDOWN)
			{
				int k = ev.key.keysym.sym;

				if(k == 'q' || k == 'Q')      wahl = 1;
				else if(k == 'r' || k == 'R') wahl = 2;
				else                          wahl = 0;
			}
			else if(ev.type == SDL_MOUSEMOTION)
			{
				/* RUECKMELDUNG. Ohne sie ist nicht zu sehen, dass die
				 * Woerter Knoepfe sind -- der Nutzer hat genau das
				 * gemeldet ("kann keine der drei Optionen
				 * auswaehlen"). Neu gezeichnet wird nur, wenn sich
				 * der Knopf unter dem Zeiger AENDERT; sonst liefe
				 * das bei jeder Mausbewegung. */
				int bx, by, t;

				zod_schirm_maus(ev.motion.x, ev.motion.y, &bx, &by);

				t = KnopfUnter(bx, by);

				if(t != knopf_unter)
				{
					knopf_unter = t;
					ZeichneKnoepfe(bx, by);
					zod_schirm_ausgeben();
				}
			}
			else if(ev.type == SDL_MOUSEBUTTONDOWN)
			{
				int bx, by;

				if(zod_schirm_maus(ev.button.x, ev.button.y, &bx, &by))
				{
					int t;

					/* Klick DANEBEN tut nichts. Vorher galt er als
					 * "weiter" -- damit uebersprang ein
					 * versehentlicher Klick den ganzen Bildschirm,
					 * und genau das ist dem Nutzer passiert. */
					t = KnopfUnter(bx, by);

					if(t >= 0) wahl = knopf_wahl[t];
				}
			}

			if(wahl >= 0)
			{
				static const char *wie[3] = { "CONTINUE", "QUIT", "RETRY" };

				ZLOG("Statistik: %s gewaehlt\n", wie[wahl]);

				return wahl;
			}
		}

		if(warte >= 20000)
		{
			ZLOG("Statistik: keine Eingabe nach 20 s -- weiter\n");

			return 0;
		}

		SDL_Delay(50);
		warte += 50;
	}
}

/* Statistikbildschirm des Originals.
 *
 * NACHGEBAUT nach einem DOSBox-Bildschirmfoto des Nutzers. Vorher hatte
 * ich Anordnung und Texte geraten, und beides war falsch:
 *
 *      YOU LOST              gross, zentriert, oben
 *   BATTLE LASTED: 0:02:44   Fliesstext, linksbuendig
 *   UNITS KILLED  :     004  Zahl dreistellig und RECHTSBUENDIG
 *   UNITS LOST    :     004
 *   QUIT                     unten links
 *                    RETRY   unten rechts
 *
 * ZWEI SCHRIFTGROESSEN, beide aus SPRITES.RSC: font19 (24x24, Serifen)
 * fuer Titel und Knoepfe, big_white (16x16) fuer den Fliesstext. Dass
 * es die Originalschriften sind, ist kein Luxus -- CHARS.BIN, das ich
 * vorher benutzt hatte, ist ein 8x8-Terminalzeichensatz und sieht
 * nirgends danach aus.
 *
 * DER PLANET IST HIER DER DER KARTE, nicht der Stand in der Kampagne --
 * anders als bei den Filmen. Das Bild zeigt das Schlachtfeld, auf dem
 * gerade gekaempft wurde.
 */
void ZPlayer::ZeigeStatistik()
{
	char name[64];
	char zeile[64];
	char zahl[16];
	int planet = zmap.GetMapBasics().terrain_type;
	long dauer, std, min, sek;

	if(planet < 0 || planet >= MAX_PLANET_TYPES) planet = DESERT;

	if(end_video_gewonnen)
		zod_schirm_name(name, sizeof(name), "win", planet,
		                (int)our_team - 1);
	else
	{
		/* BEIM VERLIEREN IST DAS BILD ZUFAELLIG -- vom Nutzer im
		 * Original beobachtet: zweimal dieselbe Karte verloren, zweimal
		 * ein anderes Bild.
		 *
		 * Fuenf Moeglichkeiten je Planet: das teamfarbige und die vier
		 * teamneutralen (?LOSE1..4 -- General Zod, ein Wrack mit
		 * Geiern, das brennende Fort). Gewaehlt wird nur unter denen,
		 * die wirklich im Archiv liegen; wer eine unvollstaendige CD
		 * hat, bekommt sonst ein schwarzes Bild. */
		char auswahl[5][64];
		int n = 0, i;

		zod_schirm_name(auswahl[n], sizeof(auswahl[0]), "lose", planet,
		                (int)our_team - 1);

		if(zod_schirm_da(auswahl[n])) n++;

		for(i = 1; i <= 4; i++)
		{
			char anlass[8];

			snprintf(anlass, sizeof(anlass), "lose%d", i);

			/* Die teamneutralen tragen KEINE Farbe im Namen --
			 * zod_schirm_name haengt eine an, also hier von Hand. */
			snprintf(auswahl[n], sizeof(auswahl[0]),
			         "assets/screens/%s_%s", anlass,
			         planet == DESERT   ? "desert"
			       : planet == VOLCANIC ? "volcanic"
			       : planet == ARCTIC   ? "arctic"
			       : planet == JUNGLE   ? "jungle" : "city");

			if(n < 5 && zod_schirm_da(auswahl[n])) n++;
		}

		if(n == 0)
		{
			/* Gar keins da -- dann wenigstens den erwarteten Namen
			 * melden, damit die Fehlanzeige im Protokoll steht. */
			zod_schirm_name(name, sizeof(name), "lose", planet,
			                (int)our_team - 1);
		}
		else
		{
			int w = rand() % n;

			strncpy(name, auswahl[w], sizeof(name) - 1);
			name[sizeof(name) - 1] = 0;

			ZLOG("Statistik: %d Niederlagenbilder, gewaehlt %d\n", n, w + 1);
		}
	}

	/* KEIN BILD -- aber die Kampagne muss trotzdem weiterlaufen.
	 *
	 * Wer eine unvollstaendige CD hat, sieht hier nichts. Ein blankes
	 * `return` haette dann auch den Aufbruchsfilm und die Zaehler
	 * uebersprungen: derselbe Siegfilm bis in alle Ewigkeit, weil nur
	 * SpieleWeiterFilm die Stufe hochzaehlt. Ohne Bild gibt es keinen
	 * Knopf, also gilt "weiter" -- dasselbe, was die Zeitschranke von
	 * 20 s tut. */
	if(!zod_schirm_zeigen(name))
	{
		ZLOG("Statistik: kein Bild (%s) -- weiter ohne Bildschirm\n", name);
		SpieleWeiterFilm();

		return;
	}

	dauer = (long)(ztime.ztime - stat_beginn);

	if(dauer < 0) dauer = 0;

	std = dauer / 3600;
	min = (dauer / 60) % 60;
	sek = dauer % 60;

	/* --- Titel --- ALLE Lagen sind am DOSBox-Bild des Nutzers
	 * ausgemessen (23.09.), in Bildkoordinaten 320x200. */
	zod_schirm_schrift("gross", 1);

	snprintf(zeile, sizeof(zeile), "%s",
	         end_video_gewonnen ? "YOU WON" : "YOU LOST");
	zod_schirm_text((320 - zod_schirm_breite(zeile)) / 2, 4, zeile);

	/* --- Fliesstext ---
	 *
	 * Drei Zeilen, alle links bei 38. Der Doppelpunkt steht bei allen
	 * dreien in DERSELBEN Spalte (212) und der Wert rechtsbuendig an
	 * 296 -- deshalb drei Ausgaben je Zeile statt einer. Mit
	 * Leerzeichen aufgefuellt saehe es nur zufaellig richtig aus. */
	zod_schirm_schrift("klein", 1);

	zod_schirm_text(38, 70, "BATTLE LASTED");
	zod_schirm_text(212, 70, ":");
	snprintf(zeile, sizeof(zeile), "%ld:%02ld:%02ld", std, min, sek);
	zod_schirm_text(296 - zod_schirm_breite(zeile), 70, zeile);

	/* Im Original dreistellig mit Fuellnullen -- "004", nicht "4". */
	zod_schirm_text(38, 88, "UNITS KILLED");
	zod_schirm_text(212, 88, ":");
	snprintf(zahl, sizeof(zahl), "%03d",
	         stat_getoetet > 999 ? 999 : stat_getoetet);
	zod_schirm_text(296 - zod_schirm_breite(zahl), 88, zahl);

	zod_schirm_text(38, 105, "UNITS LOST");
	zod_schirm_text(212, 105, ":");
	snprintf(zahl, sizeof(zahl), "%03d",
	         stat_verloren > 999 ? 999 : stat_verloren);
	zod_schirm_text(296 - zod_schirm_breite(zahl), 105, zahl);

	/* --- Knoepfe ---
	 *
	 * Das Original hat unten genau zwei, in der GROSSEN Schrift:
	 * QUIT links bei 4, RETRY rechtsbuendig an 303, Oberkante 160
	 * (gemessen: QUIT 70 Punkte breit, RETRY 95 -- gerechnet 70 und
	 * 96). Der Nutzer wollte zusaetzlich CONTINUE.
	 *
	 * Drei nebeneinander gehen NICHT: zusammen sind sie 310 von 320
	 * Punkten, CONTINUE und RETRY ueberlappten sich. CONTINUE steht
	 * deshalb eine Zeile darueber, zentriert -- dieselbe Schrift,
	 * dieselbe Groesse, und die Originalzeile bleibt, wie sie war. */
	zod_schirm_schrift("gross", 1);

	knopf_b[0] = zod_schirm_breite("QUIT");
	knopf_b[1] = zod_schirm_breite("CONTINUE");
	knopf_b[2] = zod_schirm_breite("RETRY");

	knopf_x[0] = 4;
	knopf_x[1] = (320 - knopf_b[1]) / 2;
	knopf_x[2] = 303 - knopf_b[2];

	knopf_y_o[0] = 160;
	knopf_y_o[1] = 136;
	knopf_y_o[2] = 160;

	knopf_wahl[0] = 1;   /* beenden */
	knopf_wahl[1] = 0;   /* weiter */
	knopf_wahl[2] = 2;   /* noch einmal */

	knopf_unter = -1;
	ZeichneKnoepfe(-1, -1);

	zod_schirm_ausgeben();

	/* Zeiger sichtbar machen -- sonst sind die Knoepfe nicht zu treffen
	 * (das Spiel versteckt den Systemzeiger und zeichnet seinen eigenen,
	 * und auf einem Vollbild zeichnet es gar nichts). */
	zod_schirm_zeiger(1);

	ZLOG("Statistik: %s, getoetet %d, verloren %d, Dauer %ld s\n",
	     end_video_gewonnen ? "gewonnen" : "verloren",
	     stat_getoetet, stat_verloren, dauer);

	{
		int wahl = StatistikWarten();

		/* Zeiger weg und die gemeinsame Palette zurueck, BEVOR der Player
		 * startet -- er oeffnet einen eigenen Schirm. */
		zod_schirm_zeiger(0);
		zod_schirm_ende();

		if(wahl == 1) ExitProgram();
		else if(wahl == 2)
		{
			/* Dieselbe Karte noch einmal. Derselbe Weg wie der Knopf
			 * "Reset" im Spielmenue -- der Server macht daraus eine
			 * Abstimmung, die im Einzelspieler sofort durchgeht.
			 *
			 * KEIN Aufbruchsfilm und kein Weiterzaehlen: es geht nicht
			 * weiter, es geht noch einmal von vorn. */
			client_socket.SendMessage(RESET_MAP, nullptr, 0);
		}
		else
		{
			/* WEITER -- erst jetzt der Aufbruch. Das ist die Reihenfolge
			 * des Originals: Siegfilm, Statistik, Aufbruch. Die
			 * Zeitschranke von 20 s liefert ebenfalls 0, ein Lauf ohne
			 * Zuschauer bleibt also nicht haengen. */
			SpieleWeiterFilm();
		}

		return;
	}

	zod_schirm_ende();
}

void ZPlayer::ProcessEndGame()
{
	/* GAB ES UEBERHAUPT EINE RUNDE?
	 *
	 * ZServer::CheckEndGame prueft jede Sekunde, ob noch mehr als eine
	 * Partei Einheiten hat. Solange keine Karte geladen ist, hat KEINE
	 * welche -- die Bedingung ist also erfuellt, und END_GAME kommt
	 * sofort beim Start. Im Host-Lauf stand daraufhin der
	 * Statistikbildschirm da, bevor ein einziges Bild Spiel zu sehen war
	 * (0 Kills, 0 Sekunden), und der Film waere genauso gekommen.
	 *
	 * stat_beginn wird genau dann gesetzt, wenn eine Karte beim Klienten
	 * ankommt. Null heisst: es gab nichts zu beenden. */
	if(stat_beginn <= 0)
	{
		ZLOG("Rundenende ohne Karte -- weder Film noch Statistik\n");

		return;
	}

	/* ZODENGINE-MODUS: weder Film noch Statistik.
	 *
	 * Die Vorlage hat beides nicht -- nach dem Rundenende laedt der Server
	 * nach zehn Sekunden die naechste Karte, und genau so soll es sich hier
	 * anfuehlen. Der ORIGINALLADEBILDSCHIRM bleibt trotzdem: er haengt am
	 * Terraintyp der Karte, nicht an der Kampagne, und der der Vorlage
	 * sieht schlecht aus.
	 *
	 * Die Regel steht NUR HIER. Ein zweiter Ausstieg in SpieleEndFilme
	 * waere dieselbe Regel an zwei Stellen -- und die laufen irgendwann
	 * auseinander.
	 *
	 * Nur EINMAL ins Protokoll, sonst stuende es nach jeder Runde da. */
	if(!story_modus)
	{
		static bool gemeldet = false;

		if(!gemeldet)
		{
			gemeldet = true;
			ZLOG("Rundenende: ZodEngine-Modus -- kein Film, keine Statistik\n");
		}

		return;
	}

	/* NICHT SOFORT ABSPIELEN, sondern vormerken.
	 *
	 * END_GAME kommt in dem Augenblick, in dem das Hauptgebaeude faellt.
	 * Wer hier gleich den Film startet, schneidet die Explosion mitten
	 * ab -- vom Nutzer gemeldet: "Es geht zu schnell, so dass nicht mal
	 * die Explosionsanimation zu Ende gezeigt wird."
	 *
	 * Die Wartezeit muss WEITERLAUFEND sein, nicht blockierend: nur dann
	 * zeichnet die Hauptschleife die Explosion fertig. Deshalb ein
	 * Faelligkeitszeitpunkt, den ZPlayer::Tick nach dem Zeichnen prueft.
	 *
	 * SECHS Sekunden, nicht vier: mit vier war die Animation auf der
	 * V1200 noch nicht durch (vom Nutzer am Bildschirm beurteilt -- die
	 * Dauer haengt an der Bildrate waehrend der Explosion und ist nicht
	 * auszurechnen).
	 *
	 * Wanduhr, nicht ztime: die Spieluhr steht nach dem Rundenende. */
	end_faellig = COMMON::current_time() + 6.0;

	ZLOG("Rundenende -- Film in 6 s, Explosion laeuft noch\n");
}

/* ERSTER TEIL: der Film zur gerade beendeten Runde.
 *
 * Er zaehlt die Kampagne NICHT weiter -- das tut SpieleWeiterFilm, und nur
 * dann, wenn es wirklich weitergeht. */
void ZPlayer::SpieleEndFilme()
{
	char name[32];
	char b;

	if(!zod_video_an()) return;

	if(video_planet < 0 || video_planet > ZOD_VIDEO_CITY) video_planet = 0;
	if(video_stufe  < 0 || video_stufe  > 3)              video_stufe  = 0;

	b = zod_video_planet[video_planet];

	if(!end_video_gewonnen)
	{
		/* Niederlage: Gebiet 1..4, die Stufe bleibt stehen. */
		snprintf(name, sizeof(name), "e_%clost%d", b, video_stufe + 1);
		ZLOG("Video: verloren, Planet %d Gebiet %d -> %s\n",
		     video_planet + 1, video_stufe + 1, name);
		zod_video_play(name);
		return;
	}

	if(video_stufe < 3)
	{
		/* Gebiet gewonnen. Die Weiterreise (trav) folgt im Original
		 * unmittelbar -- hier aber erst NACH dem Statistikbildschirm,
		 * siehe SpieleWeiterFilm. */
		snprintf(name, sizeof(name), "e_%cwint%d", b, video_stufe + 1);

		ZLOG("Video: Planet %d Gebiet %d gewonnen -> %s\n",
		     video_planet + 1, video_stufe + 1, name);

		zod_video_play(name);
		return;
	}

	/* Viertes Gebiet gewonnen: der Planet ist erobert.
	 *
	 * CITY HAT HIER KEINEN FILM -- es gibt kein cwinpl, die Datei fehlt
	 * auf der CD. Der Abspann ist der Aufbruch, nicht der Siegfilm, und
	 * laeuft deshalb erst nach der Statistik. */
	if(video_planet == ZOD_VIDEO_CITY)
	{
		ZLOG("Video: City, viertes Gebiet gewonnen -- kein cwinpl\n");
		return;
	}

	snprintf(name, sizeof(name), "e_%cwinpl", b);

	ZLOG("Video: Planet %d erobert -> %s\n", video_planet + 1, name);

	zod_video_play(name);
}

/* DRITTER TEIL: der Aufbruch. Laeuft NUR, wenn es weitergeht.
 *
 * Hier -- und nur hier -- zaehlt die Kampagne weiter. Stuende das im
 * Siegfilm, wuerde "Retry" nach einem Sieg das naechste Gebiet aufrufen,
 * obwohl dieselbe Karte noch einmal gespielt wird.
 *
 * Die Zaehler laufen auch weiter, wenn gar keine Filme vorhanden sind
 * (`-V` weggelassen): der Stand der Kampagne ist dann zwar unsichtbar,
 * darf aber nicht stehenbleiben.
 *
 * "?plnt" IST KEIN ANKUNFTSFILM -- das war mein Fehler, zweimal
 * hintereinander, und der Nutzer hat ihn beide Male gemeldet. Die
 * Bilder sagen es eindeutig: e_dplnt zeigt Wueste, Start, Cockpit und
 * am Ende die Briefing-Tafel "VOLCANIC PLANET"; e_vplnt zeigt
 * Vulkanlandschaft, Start, Cockpit und "ARCTIC PLANET".
 *
 * "?plnt" ist also der ABSCHIED vom Planeten ? samt Briefing fuer den
 * naechsten. Der Buchstabe ist der des EROBERTEN Planeten, nicht der
 * des kommenden.
 *
 * Damit geht auch die Stueckzahl auf: vier plnt (d, v, a, j) und kein
 * cplnt -- nach City kommt kein Planet mehr, sondern der Abspann. Und
 * die Ankunft auf der Wueste braucht kein plnt, weil e_intro2 sie
 * zeigt (Anflug, Eintritt in die Atmosphaere, Flug ueber Duenen,
 * Landung). Deshalb gehoert intro2 in den Vorspann. */
void ZPlayer::SpieleWeiterFilm()
{
	char name[32];
	char b;

	if(video_planet < 0 || video_planet > ZOD_VIDEO_CITY) video_planet = 0;
	if(video_stufe  < 0 || video_stufe  > 3)              video_stufe  = 0;

	b = zod_video_planet[video_planet];

	/* NIEDERLAGE: kein Aufbruch, und die Stufe bleibt stehen -- im
	 * Original wiederholt man dasselbe Gebiet. */
	if(!end_video_gewonnen) return;

	if(video_stufe < 3)
	{
		snprintf(name, sizeof(name), "e_%ctrav%d", b, video_stufe + 1);

		ZLOG("Video: Aufbruch, Planet %d Gebiet %d -> %s\n",
		     video_planet + 1, video_stufe + 1, name);

		if(zod_video_an()) zod_video_play(name);

		video_stufe++;
		return;
	}

	if(video_planet == ZOD_VIDEO_CITY)
	{
		static const char *abspann[] = { "e_outro", "e_creds" };

		/* City hat weder winpl noch plnt -- beide Dateien gibt es nicht.
		 * Abspann und Nachspann gehoeren zusammen und gehen deshalb in
		 * EINEN Aufruf: je Aufruf oeffnet und schliesst der Player
		 * seinen Schirm, zwei waeren zwei Modewechsel. */
		ZLOG("Video: City erobert -> e_outro, e_creds\n");

		if(zod_video_an()) zod_video_play_folge(abspann, 2);

		/* Kampagne durch. Faengt von vorn an -- der naechste Sieg soll
		 * nicht wieder den Abspann zeigen. */
		video_planet = 0;
		video_stufe  = 0;
		return;
	}

	snprintf(name, sizeof(name), "e_%cplnt", b);

	ZLOG("Video: Planet %d erobert, Aufbruch -> %s\n",
	     video_planet + 1, name);

	if(zod_video_an()) zod_video_play(name);

	video_planet++;
	video_stufe = 0;
}

void ZPlayer::RefindOurFortRefID()
{
	fort_ref_id = -1;

	for(vector<ZObject*>::iterator i=object_list.begin(); i!=object_list.end(); i++)
	{
		if((*i)->GetOwner() != our_team) continue;

		unsigned char ot, oid;

		(*i)->GetObjectID(ot, oid);

		if(ot == BUILDING_OBJECT && (oid == FORT_FRONT || oid == FORT_BACK))
		{
			fort_ref_id = (*i)->GetRefID();
			break;
		}
	}
}

void ZPlayer::SetNextSoundSetting()
{
	SetSoundSetting(sound_setting+1);
}

void ZPlayer::SetSoundSetting(int sound_setting_)
{
	sound_setting = sound_setting_;

	if(sound_setting < 0) sound_setting = 0;
	if(sound_setting >= MAX_SOUND_SETTINGS) sound_setting = 0;

	switch(sound_setting)
	{
	case SOUND_0: 
		Mix_Volume(-1, 0); 
		Mix_VolumeMusic(0);
		AddNewsEntry("volume off");
		break;
	/* Die Musik lief hier bis 18.09. nur bis 80 von 128 -- dauerhaft -4 dB,
	 * auch wenn der Regler auf 100 % stand. Jetzt bis 128 wie die Klaenge. */
	case SOUND_25: 
		Mix_Volume(-1, 128 / 4); 
		Mix_VolumeMusic(128 / 4);
		AddNewsEntry("volume 25%");
		break;
	case SOUND_50: 
		Mix_Volume(-1, 128 / 2); 
		Mix_VolumeMusic(128 / 2);
		AddNewsEntry("volume 50%");
		break;
	case SOUND_75: 
		Mix_Volume(-1, 128 * 3 / 4); 
		Mix_VolumeMusic(128 * 3 / 4);
		AddNewsEntry("volume 75%");
		break;
	case SOUND_100: 
		Mix_Volume(-1, 128); 
		Mix_VolumeMusic(128);
		AddNewsEntry("volume full");
		break;
	}
}

void ZPlayer::AddSpaceBarEvent(SpaceBarEvent new_event)
{
	//delete duplicates
	for(vector<SpaceBarEvent>::iterator i=space_event_list.begin(); i!=space_event_list.end();)
	{
		if(new_event == *i)
			i = space_event_list.erase(i);
		else
			++i;
	}

	//ZLOG("ZPlayer::AddSpaceBarEvent::ref_id:%d\n", new_event.ref_id);

	space_event_list.insert(space_event_list.begin(), new_event);

	if(space_event_list.size() >= MAX_STORED_SPACE_BAR_EVENTS)
		space_event_list.resize(MAX_STORED_SPACE_BAR_EVENTS);
}

void ZPlayer::DoSpaceBarEvent()
{
	SpaceBarEvent process_event;

	while(space_event_list.size())
	{
		int x, y;
		ZObject *obj;

		//get event
		process_event = space_event_list[0];

		//get object
		obj = GetObjectFromID(process_event.ref_id, object_list);

		//if not good any more, delete this event and try again
		if(!obj || process_event.past_lifetime())
		{
			//if(!obj) ZLOG("ZPlayer::DoSpaceBarEvent::!obj ref_id:%d\n", process_event.ref_id);
			space_event_list.erase(space_event_list.begin());
			continue;
		}

		//focus on its leader instead?
		if(obj->GetGroupLeader()) obj = obj->GetGroupLeader();

		//process event
		{
			obj->GetCenterCords(x, y);
			FocusCameraTo(x, y);

			if(process_event.select_obj && !select_info.ObjectIsSelected(obj)) SelectZObject(obj);

			if(process_event.open_gui) ObjectMakeGuiWindow(obj);
		}

		//move event to end of list
		space_event_list.erase(space_event_list.begin());
		space_event_list.push_back(process_event);

		//exit
		break;
	}
}

bool ZPlayer::ObjectMakeGuiWindow(ZObject *obj)
{
	if(!obj) return false;

	//only one can exist
	DeleteCurrentGuiWindow();

	//checks
	if(obj->GetOwner() == NULL_TEAM) return false;
	if(obj->GetOwner() != our_team) return false;

	//make it
	gui_window = obj->MakeGuiWindow();

	//it get made?
	if(!gui_window) return false;

	//set its build list
	gui_window->SetBuildList(&buildlist);

	//clear rally points
	//gui_window->GetBuildingObj()->GetWayPointDevList().clear();
	obj->GetWayPointDevList().clear();

	return true;
}
