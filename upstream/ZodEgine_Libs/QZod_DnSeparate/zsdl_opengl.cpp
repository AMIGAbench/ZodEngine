#include "zsdl_opengl.h"
#include "fineclock.h"   /* Uhr um das Drehen: Rechnen gegen Aufwand je Effekt */
#include <algorithm>       /* std::sort fuer die Verdraengungsliste */

//Asset-Archive (siehe port/zod_pack.h)
#include <zod_pack.h>
#include <zod_palette.h>
#include <amiga_startup.h>
#include <utility>
#include <vector>

#ifdef __amigaos__
extern "C" void zod_dirty_mark(void *dst, int x, int y, int w, int h);
#endif

/* Ein Pixel unabhaengig von der Farbtiefe lesen.
 *
 * Die alten Fassungen lasen hier fest *(Uint32*) -- seit die Flaechen 8 Bit
 * haben, liest das drei Byte zu viel und am letzten Pixel ueber die Belegung
 * hinaus. Genau das war Guru 81000005 ("Speicherliste beschaedigt") beim
 * ersten Start der eigenen Grafikschicht. */
static Uint32 zod_read_pixel(SDL_Surface *s, int x, int y)
{
	const Uint8 *p = (const Uint8*)s->pixels + (size_t)y * s->pitch
	                 + (size_t)x * s->format->BytesPerPixel;

	switch(s->format->BytesPerPixel)
	{
	case 1: return *p;
	case 2: return *(const Uint16*)p;
	case 3:
#if defined(__amigaos__) || defined(__BIG_ENDIAN__)
		return ((Uint32)p[0] << 16) | ((Uint32)p[1] << 8) | p[2];
#else
		return p[0] | ((Uint32)p[1] << 8) | ((Uint32)p[2] << 16);
#endif
	default: return *(const Uint32*)p;
	}
}





void InitOpenGL()
{
#ifndef DISABLE_OPENGL
    glEnable(GL_TEXTURE_2D);			// Enable Texture Mapping
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);	// Clear The Background Color To Black 
    glClearDepth(0.0f);				// Disables(?) Clearing Of The Depth Buffer
	glDepthFunc( GL_ALWAYS );
    glEnable(GL_DEPTH_TEST);			// Enables Depth Testing
    glShadeModel(GL_SMOOTH);			// Enables Smooth Color Shading

	glEnable(GL_ALPHA_TEST);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	//glAlphaFunc(GL_GREATER, 0.5);
	glDisable(GL_LIGHTING);

    glEnable(GL_BLEND);

	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);		// Clear The Screen And The Depth Buffer
	glLoadIdentity();				// Reset The View
#endif
}

void ResetOpenGLViewPort(int width, int height)
{
#ifndef DISABLE_OPENGL
	glViewport(0, 0, width, height);

	glMatrixMode(GL_PROJECTION);
    glLoadIdentity(); // Reset The Projection Matrix
    
    glOrtho(0.0, width, height, 0.0, 1.0, -1.0);
    
    glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
#endif
}

/* Flaechen, deren Pixel LoadNewSurface selbst belegt hat (Kartenflaeche,
 * Minikarte). Bewusst eine Tabelle hier statt eines Mitglieds: ZSDL_Surface
 * behaelt so sein Speicherlayout. Es sind nur eine Handvoll Eintraege. */
typedef std::vector<std::pair<SDL_Surface*, void*> > zsdl_big_list;

//absichtlich nie zerstoert: globale ZMap-Objekte anderer Uebersetzungseinheiten
//geben ihre Flaechen erst beim Programmende frei, in unbestimmter Reihenfolge
static zsdl_big_list &zsdl_big_pixels()
{
	static zsdl_big_list *list = new zsdl_big_list;
	return *list;
}

static void zsdl_big_pixels_add(SDL_Surface *surf, void *pixels)
{
	zsdl_big_pixels().push_back(std::make_pair(surf, pixels));
}

static void *zsdl_big_pixels_take(SDL_Surface *surf)
{
	zsdl_big_list &list = zsdl_big_pixels();

	for(size_t i=0;i<list.size();i++)
		if(list[i].first == surf)
		{
			void *pixels = list[i].second;
			list.erase(list.begin() + i);
			return pixels;
		}

	return nullptr;
}

/* Drehflaechenspeicher: global mitzaehlen, damit sich sein Anteil am
 * Speicher belegen laesst (bis zu 8 Fassungen JE Flaeche, kein Gesamtlimit). */
static long zod_roto_count = 0;
static long zod_roto_bytes = 0;

static long zod_surface_bytes(SDL_Surface *s)
{
	if(!s) return 0;
	return (long)s->h * s->pitch;
}

//fuer die Speichersonde der Diagnose-Fassung (port/amiga/alloc_probe.cpp)
extern "C" void zod_roto_stats(long *count, long *kb)
{
	if(count) *count = zod_roto_count;
	if(kb)    *kb    = zod_roto_bytes / 1024;
}

/* Obergrenze fuer die Summe aller gedrehten Fassungen. Im Normalbetrieb
 * gemessen: 0,9 MB (393 Flaechen) -- die Grenze greift also erst beim
 * Massensterben am Rundenende, genau dort, wo der Speicher wegbrach. */
#ifdef __amigaos__
static const long ZOD_ROTO_BUDGET = 2L * 1024L * 1024L;
#else
static const long ZOD_ROTO_BUDGET = 32L * 1024L * 1024L;
#endif

//Flaechen, die gerade Fassungen halten. Ein gemeinsamer Stempelzaehler macht
//die Eintraege ueber Flaechengrenzen hinweg vergleichbar.
//Das Register wird ABSICHTLICH nie zerstoert ("construct on first use",
//bewusstes Leck von einem Vektor beim Programmende).
//
//WARUM, belegt mit AddressSanitizer: Als datei-statischer Vektor wurde es beim
//`exit` zerstoert -- und DANACH laufen die Destruktoren der statischen
//ZSDL_Surface-Objekte, von denen es im Binary **14 002** gibt. Jeder ruft
//~ZSDL_Surface -> Unload -> DropRotoZoom -> RotoUnregister und greift damit auf
//den bereits freigegebenen Vektor zu:
//
//    #0 RotoUnregister   zsdl_opengl.cpp:164
//    #3 ~ZSDL_Surface    zsdl_opengl.cpp:315
//    #4 __tcf_1          zvehicle.cpp:4      <- statisches Objekt beim Ende
//    #5 __run_exit_handlers
//
//Auf Linux bleibt das folgenlos. Auf AmigaOS nicht: RotoUnregister SCHREIBT
//in den Vektor (`owners[i] = owners.back(); pop_back()`), also in einen
//freigegebenen Block -- und libnix gibt Bloecke sofort zur Wiederverwendung
//frei. Das ist Heap-Korruption beim Beenden, genau die Klasse Fehler, die
//ohne MMU still bleibt.
//
//Die Zerstoerungsreihenfolge statischer Objekte ueber Uebersetzungseinheiten
//hinweg ist in C++ nicht festgelegt; ein Merker als Wache waere deshalb nur
//innerhalb DIESER Datei verlaesslich. Nie zerstoeren ist die einzige Loesung,
//die auch fuer die 14 002 Objekte in anderen Dateien traegt.
static std::vector<ZSDL_Surface*> &zod_roto_owners_ref()
{
	static std::vector<ZSDL_Surface*> *v = new std::vector<ZSDL_Surface*>();

	return *v;
}

#define zod_roto_owners (zod_roto_owners_ref())
static unsigned int zod_roto_clock = 0;

/* Was die Budgetdurchsetzung kostet: Verdraengungen und die dabei
 * durchlaufenen Plaetze (die Suche ist O(Besitzer x 8) JE Verdraengung). */
static unsigned long zod_roto_eject = 0, zod_roto_eject_steps = 0;

/* Was NEBEN dem Rechnen anfaellt, wenn eine Flaeche neu entsteht:
 *
 *  - `frei`  das Freigeben der verdraengten Flaeche. Eine Drehflaeche eines
 *            16x16-Sprites ist bei Groesse 5 nur 80x80 = 6400 Byte, liegt also
 *            UNTER ZOD_BIG_LIMIT (64 KB) und kommt damit aus libnix' calloc --
 *            und libnix' free laeuft die Blockliste linear ab (gemessen 88672
 *            Eintraege). Das stand bisher AUSSERHALB der Uhr und lief damit
 *            unter "Aufwand je Effekt".
 *  - `verw`  RotoRegister + RotoEnforceBudget (lineare Suche ueber die
 *            Besitzer; der Lauf meldet 1,2 Mio. Suchschritte bei 0
 *            Verdraengungen).
 *
 * Zwei Uhrabrufe je gerechneter Flaeche, nicht je Effekt. */
static unsigned long zod_roto_frame_frei = 0;
static unsigned long zod_roto_frame_verw = 0;
static unsigned long zod_roto_frame_weg  = 0;   /* Stueckzahl der Freigaben */

static unsigned long zod_roto_frame_eject = 0, zod_roto_peak_eject = 0;

bool ZSDL_Surface::RotoHasEntries() const
{
	for(int i=0;i<ZOD_ROTO_CACHE;i++)
		if(roto[i].surf) return true;

	return false;
}

void ZSDL_Surface::RotoRegister(ZSDL_Surface *s)
{
	for(size_t i=0;i<zod_roto_owners.size();i++)
		if(zod_roto_owners[i] == s) return;

	zod_roto_owners.push_back(s);
}

void ZSDL_Surface::RotoUnregister(ZSDL_Surface *s)
{
	for(size_t i=0;i<zod_roto_owners.size();i++)
		if(zod_roto_owners[i] == s)
		{
			zod_roto_owners[i] = zod_roto_owners.back();
			zod_roto_owners.pop_back();
			return;
		}
}

//Den am laengsten ungenutzten Eintrag ueber ALLE Flaechen verwerfen, bis die
//Summe wieder unter der Grenze liegt.
/* Auf der V1200 gemessen (20.09.): Diese Funktion kostete im schlimmsten Bild
 * 101777 us von 140397 -- 73 % des ganzen Bildes, bei 18 gerechneten Flaechen
 * also 5654 us je Stueck.
 *
 * Der Grund stand in der Schleife: Sie suchte JE EINZELNER VERDRAENGUNG erneut
 * ueber ALLE Besitzer und ihre acht Plaetze. Muessen nach einer Explosion
 * Dutzende Flaechen weichen, sind das Dutzende vollstaendige Durchgaenge.
 *
 * Jetzt EIN Durchgang: alle verdraengbaren Eintraege einmal einsammeln, dann
 * aus dieser kompakten Liste heraus freigeben.
 *
 * DIE OPFERREIHENFOLGE IST DIESELBE. Die alte Fassung nahm in jedem Durchgang
 * den global aeltesten Eintrag; Freigeben aendert die Stempel der uebrigen
 * nicht, die Folge ist also "aeltester, zweitaeltester, ...". Genau das
 * liefert die Auswahl aus der Liste.
 *
 * NEBENBEI EIN MESSFEHLER BEHOBEN: `zod_roto_eject` wurde deklariert und NIE
 * hochgezaehlt -- die Zeile "0 Verdraengungen gesamt" im Bericht war
 * bedeutungslos, und ich hatte daraus geschlossen, es werde nie verdraengt.
 * Dieselbe Klasse wie beim frueheren zod_roto_frame_bytes. */
struct roto_kandidat
{
	ZSDL_Surface *owner;
	int           slot;
	unsigned int  stamp;
};

static bool roto_aelter(const roto_kandidat &a, const roto_kandidat &b)
{
	return a.stamp < b.stamp;
}

/* Wie weit unter die Grenze aufgeraeumt wird.
 *
 * Ohne diesen Abstand raeumt die Funktion GENAU bis zur Grenze -- und weil sie
 * je gerechneter Flaeche gerufen wird, stoesst die naechste Flaeche sofort
 * wieder daran. Gemessen auf der V1200 (20.09.): in EINEM Bild 22 Drehungen,
 * und `verwalten` minus `freigeben` waren 41466 us. Das ist nicht das
 * Verdraengen, sondern 22-mal vollstaendiges Einsammeln und Sortieren, um je
 * ein bis zwei Eintraege loszuwerden.
 *
 * Mit einem Abstand von einem Viertel deckt ein Aufraeumen viele folgende
 * Flaechen ab, und die uebrigen Aufrufe kehren an der ersten Zeile um.
 *
 * Der Zwischenspeicher ist durchsichtig: was verdraengt wird, entsteht bei
 * Bedarf neu. Ein etwas leererer Speicher kann also nichts kaputtmachen, er
 * kostet nur Treffer -- und die Treffer sind mit 80 % ohnehin reichlich. */
static const long ZOD_ROTO_ZIEL = ZOD_ROTO_BUDGET - (ZOD_ROTO_BUDGET / 4);

void ZSDL_Surface::RotoEnforceBudget()
{
	if(zod_roto_bytes <= ZOD_ROTO_BUDGET || !zod_roto_owners.size()) return;

	/* Statisch, damit je Aufruf nichts belegt wird -- auf AmigaOS ist gerade
	 * das Belegen teuer, und diese Funktion laeuft mitten in der Explosion. */
	static vector<roto_kandidat> liste;

	liste.clear();

	for(size_t o = 0; o < zod_roto_owners.size(); o++)
	{
		ZSDL_Surface *s = zod_roto_owners[o];

		for(int i = 0; i < ZOD_ROTO_CACHE; i++)
		{
			if(!s->roto[i].surf) continue;

			//der gerade ausgewaehlte Eintrag bleibt: er wird jetzt gezeichnet
			if(s->roto[i].surf == s->sdl_rotozoom) continue;

			roto_kandidat k;

			k.owner = s;
			k.slot  = i;
			k.stamp = s->roto[i].stamp;

			liste.push_back(k);
		}
	}

	zod_roto_eject_steps += (unsigned long)liste.size();

	/* EINMAL sortieren statt je Verdraengung das Minimum zu suchen.
	 *
	 * Gemessen auf der V1200 (20.09.): In einem Bild wichen 237 Flaechen, und
	 * die Auswahl allein kostete rund 18800 us -- die Minimumsuche ist
	 * quadratisch in der Zahl der Kandidaten. Ein Sortiervorgang ist
	 * n log n: bei 800 Kandidaten rund 8000 Vergleiche statt einiger
	 * hunderttausend.
	 *
	 * DIE OPFERREIHENFOLGE BLEIBT IDENTISCH: Die Stempel sind eindeutig
	 * (`++zod_roto_clock` je Vergabe), es gibt also keine Gleichstaende, bei
	 * denen eine andere Sortierung eine andere Reihenfolge liefern koennte. */
	std::sort(liste.begin(), liste.end(), roto_aelter);

	size_t naechster = 0;

	while(zod_roto_bytes > ZOD_ROTO_ZIEL && naechster < liste.size())
	{
		ZSDL_Surface *owner = liste[naechster].owner;
		const int     slot  = liste[naechster].slot;
		SDL_Surface  *drop  = owner->roto[slot].surf;

		naechster++;

		if(owner->hit_src == drop)
		{
			if(owner->sdl_hit) SDL_FreeSurface(owner->sdl_hit);
			owner->sdl_hit = nullptr;
			owner->hit_src = nullptr;
		}

		zod_roto_count--;
		zod_roto_bytes -= zod_surface_bytes(drop);
		zod_roto_eject++;          /* wurde bisher NIE hochgezaehlt */
		zod_roto_frame_eject++;

		/* BUCHUNGSFEHLER BEHOBEN: Diese Freigabe lief bisher unter
		 * "verwalten", obwohl sie Freigeben IST. Genau das hat die Deutung
		 * verfaelscht -- auf der V1200 meldete ein Bild `38377 us verwalten`
		 * bei 220 Verdraengungen, und die Auswahl ueber ein kompaktes Feld
		 * kann das nicht sein. */
		{
			const unsigned long t0 = zod_fineclock_ticks();

			SDL_FreeSurface(drop);
			zod_roto_frame_frei += zod_fineclock_ticks() - t0;
		}

		owner->roto[slot].surf = nullptr;

		if(!owner->RotoHasEntries()) RotoUnregister(owner);
	}
}

void ZSDL_Surface::DropAllRotoZoom()
{
	while(zod_roto_owners.size())
	{
		ZSDL_Surface *s = zod_roto_owners.back();

		s->DropRotoZoom();

		if(zod_roto_owners.size() && zod_roto_owners.back() == s)
			zod_roto_owners.pop_back();
	}
}

bool ZSDL_Surface::use_opengl = false;
SDL_Surface *ZSDL_Surface::screen = nullptr; // TODO nullptr;
int ZSDL_Surface::screen_w = 0;
int ZSDL_Surface::screen_h = 0;
int ZSDL_Surface::map_place_x = 0;
int ZSDL_Surface::karte_x = 0;
int ZSDL_Surface::karte_y = 0;
int ZSDL_Surface::karte_w = 0;   /* 0 = ganzer Ausschnitt */
int ZSDL_Surface::karte_h = 0;
int ZSDL_Surface::map_place_y = 0;
bool ZSDL_Surface::has_hud = true;
bool ZSDL_Surface::no_scaler = false;
bool ZSDL_Surface::no_spin   = false;

/* Was der Skalierer wirklich kostet -- bisher unbelegt. Der Anteil am
 * Posten "zeichnen" (auf der V1200 79 % der Bildzeit) war nie gemessen,
 * und ohne diese Zahl ist jede Beschleunigung dort ein Schuss ins Dunkle.
 * Gezaehlt wird nur, was WIRKLICH gerechnet wird: Treffer im
 * Zwischenspeicher kosten nichts. */
static unsigned long zod_roto_calls  = 0;   /* Anfragen insgesamt      */
static unsigned long zod_roto_made   = 0;   /* neu gerechnete Flaechen */
static unsigned long zod_roto_pixels = 0;   /* deren Bildpunkte        */

/* Der MITTELWERT taeuscht hier, und zwar gewaltig. Ruckeln ist ein
 * Spitzenwert: Ein sterbendes Fort wirft 16-22 Truemmerteile und 12-18
 * Feuerbaelle in EINEM Bild aus, jedes mit eigenem Winkel und eigener
 * Groesse -- also Dutzende neu gerechnete UND neu belegte Flaechen in
 * diesem einen Bild. Ueber 4000 Bilder verteilt sieht das nach 7
 * Bildpunkten je Bild aus. Der Nutzer sieht auf echter Hardware die
 * Spitze, nicht den Mittelwert.
 * Deshalb wird je Bild mitgeschrieben und das Maximum behalten. */
static unsigned long zod_roto_frame_made   = 0;
static unsigned long zod_roto_frame_pixels = 0;
static unsigned long zod_roto_peak_made    = 0;
static unsigned long zod_roto_peak_pixels  = 0;

/* DIE ANFRAGEN je Bild fehlten bisher -- und damit die TREFFERQUOTE in der
 * Spitze. "Spitze je Bild: 68 Flaechen" sagt nichts, solange unbekannt ist,
 * ob dahinter 70 Anfragen standen (Speicher nutzlos) oder 700 (Speicher
 * traegt). Die Analyse vom 19.09. sagt 0 bis 1,1 % Trefferquote bei den
 * Truemmern voraus, weil Winkel UND Groesse sich je Bild aendern und die
 * Groesse sogar monoton laeuft -- ein monoton wachsender Schluessel trifft
 * einen Verdraengungsspeicher nie. Diese Zeile prueft die Vorhersage. */
static unsigned long zod_roto_frame_calls = 0;
/* Wie viel der Zeit im Abschnitt `effekte` wirklich das RECHNEN einer Flaeche
 * ist -- der Rest ist Aufwand je Effekt (virtueller Aufruf, Rasterung,
 * Sichttest, Blit). Ohne diese Trennung ist jede Folgeentscheidung geraten.
 * Zwei Uhrabrufe je GERECHNETER Flaeche, nicht je Effekt: bei 24000
 * Rechnungen im Lauf sind das 0,25 % der Laufzeit. */
static unsigned long zod_roto_frame_ticks = 0;
static unsigned long zod_roto_peak_calls  = 0;

/* Belegte Byte je Bild -- trennt Rechnen von Belegen. */
static unsigned long zod_roto_frame_bytes = 0;
static unsigned long zod_roto_peak_bytes  = 0;

extern "C" unsigned long zod_roto_frame_made_get(void)
{
	return zod_roto_frame_made;
}

extern "C" unsigned long zod_roto_frame_ticks_get(void)
{
	return zod_roto_frame_ticks;
}

extern "C" void zod_roto_frame_extra_get(unsigned long *frei, unsigned long *verw,
                                         unsigned long *weg)
{
	*frei = zod_roto_frame_frei;
	*verw = zod_roto_frame_verw;
	/* Beide Stueckzahlen zusammen: die aus dem 8-Platz-Speicher und die aus
	 * der Budgetgrenze. Getrennt zu melden brachte nichts -- entscheidend ist,
	 * wie viele Flaechen in EINEM Bild sterben. */
	*weg  = zod_roto_frame_weg + zod_roto_frame_eject;
}

void zod_roto_frame_end(void)
{
	/* ACHTUNG: Hier wurden `calls` und `made` frueher als ZWEI getrennte
	 * Maxima gefuehrt und im Bericht gegeneinander geteilt. Die koennen aus
	 * VERSCHIEDENEN Bildern stammen -- die gemeldete "Trefferquote in der
	 * Spitze" war damit keine. Jetzt werden die Anfragen DESSELBEN Bildes
	 * festgehalten, in dem die meisten Flaechen gerechnet wurden. */
	if(zod_roto_frame_made   > zod_roto_peak_made)
	{
		zod_roto_peak_made  = zod_roto_frame_made;
		zod_roto_peak_calls = zod_roto_frame_calls;
		zod_roto_peak_bytes = zod_roto_frame_bytes;
	}
	if(zod_roto_frame_pixels > zod_roto_peak_pixels) zod_roto_peak_pixels = zod_roto_frame_pixels;
	if(zod_roto_frame_eject  > zod_roto_peak_eject)  zod_roto_peak_eject  = zod_roto_frame_eject;

	zod_roto_frame_made   = 0;
	zod_roto_frame_pixels = 0;
	zod_roto_frame_calls  = 0;
	zod_roto_frame_ticks  = 0;
	zod_roto_frame_frei   = 0;
	zod_roto_frame_verw   = 0;
	zod_roto_frame_weg    = 0;
	zod_roto_frame_bytes  = 0;
	zod_roto_frame_eject  = 0;
}

extern "C" void zod_roto_peak_report(unsigned long *calls, unsigned long *made,
                                     unsigned long *bytes, unsigned long *eject,
                                     unsigned long *eject_steps,
                                     unsigned long *owners)
{
	*calls = zod_roto_peak_calls; *made = zod_roto_peak_made;
	*bytes = zod_roto_peak_bytes; *eject = zod_roto_peak_eject;
	*eject_steps = zod_roto_eject_steps; *owners = (unsigned long)zod_roto_owners.size();
}

void zod_roto_report(unsigned long &calls, unsigned long &made,
                     unsigned long &pixels, unsigned long &peak_made,
                     unsigned long &peak_pixels)
{
	calls = zod_roto_calls; made = zod_roto_made; pixels = zod_roto_pixels;
	peak_made = zod_roto_peak_made; peak_pixels = zod_roto_peak_pixels;
}

ZSDL_Surface::ZSDL_Surface()
{
    sdl_surface = nullptr; // TODO nullptr;
    sdl_rotozoom = nullptr; // TODO nullptr;
    sdl_hit = nullptr;
    hit_src = nullptr;
	gl_texture_loaded = false;
	rotozoom_loaded = false;

	roto_clock = 0;

	for(int i=0;i<ZOD_ROTO_CACHE;i++)
	{
		roto[i].surf  = nullptr;
		roto[i].angle = 0.0f;
		roto[i].size  = 1.0f;
		roto[i].stamp = 0;
	}

	size = 1.0f;
	angle = 0.0f;
	/* 255 = deckend. Die 127 stammten aus einer Verwechslung, als alpha noch
	 * ein signed char war ("max value 127"); der Typ ist laengst uint8_t.
	 * Die Folge war, dass jede Flaeche, die durch UseDisplayFormat oder
	 * MakeAlphable laeuft, halbdurchsichtig UND im teuersten Blitpfad landete. */
	alpha = 255;
}

ZSDL_Surface::~ZSDL_Surface()
{
	Unload();
}

ZSDL_Surface& ZSDL_Surface::operator=(const ZSDL_Surface &rhs)
{
	if(this == &rhs) return *this;

	LoadBaseImage(rhs.sdl_surface, false);

	return *this;
}

ZSDL_Surface& ZSDL_Surface::operator=(SDL_Surface *rhs)
{
	LoadBaseImage(rhs, false);

	return *this;
}

void ZSDL_Surface::Unload()
{
	//zuerst der Drehspeicher: er kann die Silhouette mitnehmen
	DropRotoZoom();

	if(sdl_surface)
	{
		void *big = zsdl_big_pixels_take(sdl_surface);

		SDL_FreeSurface(sdl_surface);
		zod_big_free(big);
	}
	if(sdl_hit) SDL_FreeSurface(sdl_hit);
#ifndef DISABLE_OPENGL
	if(gl_texture_loaded) glDeleteTextures(1, &gl_texture);
#endif

    sdl_surface = nullptr; // TODO nullptr;
    sdl_rotozoom = nullptr; // TODO nullptr;
    sdl_hit = nullptr;
    hit_src = nullptr;
	gl_texture_loaded = false;
	rotozoom_loaded = false;
}

SDL_Surface *ZSDL_Surface::GetBaseSurface()
{
	return sdl_surface;
}

void ZSDL_Surface::LoadBaseImage(string filename)
{
	//set this for later debugging purposes
	image_filename = filename;

	//erst im Asset-Archiv nachsehen (port/zod_pack.cpp): das spart auf dem
	//Amiga tausende Dateioeffnungen und die PNG-Dekodierung zur Laufzeit.
	//Fehlt der Eintrag, wird wie bisher die Einzeldatei geladen.
	SDL_Surface *surface = zod_pack_load(filename.c_str());

	if(!surface) surface = IMG_Load(filename.c_str());

	LoadBaseImage(surface);
}

void ZSDL_Surface::LoadNewSurface(int w, int h, bool opaque)
{
	SDL_Surface *new_surface;

	/* Deckende Flaeche (die Karte): gleich im Bildschirmformat anlegen.
	 *
	 * Der allgemeine Weg unten belegt die Flaeche erst in 32 Bit, macht per
	 * SDL_DisplayFormatAlpha eine ZWEITE 32-Bit-Kopie und wandelt erst dann
	 * ins Bildschirmformat -- auf einem 16-Bit-Schirm kurzzeitig 4+4+2 statt
	 * 2 Byte je Pixel, bei orig04 zwei zusammenhaengende 9,7-MB-Bloecke fuer
	 * ein 4,8-MB-Ergebnis. Auf dem Amiga (kein virtueller Speicher, keine
	 * Verdichtung) scheitert das nach wenigen Kartenwechseln: der Hintergrund
	 * fehlt, alles hinterlaesst Schlieren. Das Ergebnis ist dasselbe wie
	 * zuvor: Bildschirmformat, ohne SDL_SRCALPHA, schwarz gefuellt. */
	/* Auch NICHT deckende Flaechen entstehen hier im Schirmformat.
	 *
	 * Vorher nahm dieser Weg nur deckende Flaechen, alles andere ging ueber
	 * eine 32-Bit-Flaeche weiter unten. Solange SDL_CreateRGBSurface auf dem
	 * Amiga 32-Bit-Anforderungen still auf 8 Bit herunterdrehte, fiel das
	 * nicht auf. Seit das (zu Recht) nicht mehr passiert, entstehen dort echte
	 * 32-Bit-Flaechen -- und ein Blit aus einer 8-Bit-Quelle dorthin kann der
	 * Amiga-Blitter nicht. Ergebnis am 18.09. auf der V2: Gebirge, Schrift und
	 * Fenster fehlten vollstaendig, weil genau sie so gebaut werden
	 * (ORock::Init schneidet 16x16-Kacheln aus, ZFont setzt Text zusammen).
	 *
	 * Der Farbschluessel ist Platz 0 -- die Festlegung der ganzen Engine
	 * (siehe SDL_ModifyBlack, das echtes Schwarz vorher wegschiebt). */
	if(!use_opengl && screen && screen->format)
	{
		const SDL_PixelFormat *f = screen->format;
		const int bpp = f->BytesPerPixel;

		Unload();

		/* Die Pixel an malloc vorbei belegen (zod_big_alloc, auf dem Amiga
		 * AllocVec): auch die fertige Flaeche kaeme sonst beim Kartenwechsel
		 * nicht ans System zurueck, und bei wachsenden Karten schrumpfte der
		 * groesste freie Block um jede Kartenflaeche. SDL_CreateRGBSurfaceFrom
		 * gibt fremde Pixel nicht frei -- das uebernimmt Unload(). */
		void *pixels = zod_big_alloc((unsigned long)w * h * bpp);
		if(!pixels) return;

		sdl_surface = SDL_CreateRGBSurfaceFrom(pixels, w, h, f->BitsPerPixel, w * bpp,
		                                       f->Rmask, f->Gmask, f->Bmask, 0);
		if(!sdl_surface)
		{
			zod_big_free(pixels);
			return;
		}

		zsdl_big_pixels_add(sdl_surface, pixels);

		SDL_SetAlpha(sdl_surface, 0, 255);
		SDL_FillRect(sdl_surface, nullptr, SDL_MapRGB(sdl_surface->format, 0, 0, 0));

		/* BEWUSST OHNE Farbschluessel -- wie die Urfassung.
		 *
		 * Ein erster Versuch setzte hier fuer nicht deckende Flaechen Platz 0
		 * als Schluessel. Das klang richtig, aendert aber die Zusage der
		 * Funktion: Der allgemeine Weg legt eine 32-Bit-Flaeche mit
		 * SDL_SRCALPHA an, faellt auf Alpha 255 zurueck und fuellt schwarz --
		 * also DECKENDES Schwarz. Genau darauf baut der Aufrufer
		 * (ZGuiMainMenuBase::BuildBaseImage sagt es im Kommentar sogar).
		 *
		 * Mit Schluessel wurde der HUD-Hintergrund durchsichtig, und im
		 * fertigen Bild schien das Ladebild rechts durch die Anzeige. Vom
		 * Nutzer auf der V2 gesehen und im Amiga-Bildschirmfoto bestaetigt.
		 *
		 * Durchsichtigkeit kommt in dieser Engine nicht von hier, sondern von
		 * SDL_SetColorKey(..., 0x000000) auf GELADENEN Bildern (MakeAlphable). */
		return;
	}

    new_surface = SDL_CreateRGBSurface(SDL_HWSURFACE | SDL_SRCALPHA,
                                       w, h, 32,
                                       0xFF000000, 0x0000FF00,
                                       0x00FF0000, 0x000000FF);

	LoadBaseImage(new_surface);

	//Deckende Flaechen: LoadBaseImage hat ueber SDL_DisplayFormatAlpha eine
	//Flaeche MIT Alphakanal erzeugt. Jeder Blit daraus waere damit eine
	//Alpha-Mischung, obwohl nichts durchscheint -- bei der Karte betrifft das
	//den groessten Blit jedes Bildes (540x444). Also ins Bildschirmformat
	//wandeln und SDL_SRCALPHA loeschen: daraus wird ein reiner Kopierblit,
	//und auf einem 16-Bit-Schirm halbiert sich zugleich der Speicherbedarf.
	//
	//Bewusst NICHT ueber UseDisplayFormat: das ruft am Ende SetAlpha(alpha),
	//und alpha ist laut Konstruktor 127 -- die Flaeche waere danach halb
	//durchsichtig.
	if(opaque && sdl_surface && !use_opengl)
	{
		SDL_Surface *conv = SDL_DisplayFormat(sdl_surface);

		if(conv)
		{
			SDL_FreeSurface(sdl_surface);
			sdl_surface = conv;
		}

		SDL_SetAlpha(sdl_surface, 0, 255);
	}

	SDL_Rect the_box;
	the_box.x = 0;
	the_box.y = 0;
    the_box.w = static_cast<uint16_t>(w);
    the_box.h = static_cast<uint16_t>(h);
	ZSDL_FillRect(&the_box, 0, 0, 0, this);
}

void ZSDL_Surface::LoadBaseImage(SDL_Surface *sdl_surface_, bool delete_surface)
{
	Unload();

	sdl_surface = sdl_surface_;

	if(!sdl_surface)
	{
		if(image_filename.size()) ZLOG("could not load:%s\n", image_filename.c_str()); 
		return;
	}

	/* Ins Bildschirmformat bringen, ueber SDL_DisplayFormatAlpha.
	 *
	 * ZURUECKGENOMMEN (16.09.): Ein Versuch, stattdessen SDL_DisplayFormat mit
	 * Farbschluessel zu nehmen, war rechnerisch verlockend -- die Archive
	 * liefern bereits 8 Bit indiziert bzw. RGB565 mit Schluessel, und auf dem
	 * Host waren Sprite-Blits damit 1,8-2,2x schneller. Auf der V2 hat es aber
	 * drei sichtbare Fehler und einen Leistungsverlust erzeugt:
	 *
	 * 1. rotozoomSurface kennt NUR 8 und 32 Bit. Eine 16-Bit-Quelle wandelt es
	 *    bei JEDEM Aufruf erst nach 32 Bit -- zusaetzliche Belegung und ein
	 *    zusaetzlicher Blit, und dabei geht der Farbschluessel verloren: die
	 *    freien Stellen bleiben schwarz. Daher der schwarze Hintergrund der
	 *    Voegel und die ruckeligeren Explosionen.
	 * 2. Beim Wandeln nach RGB565 koennen mehrere Palettenfarben auf denselben
	 *    Wert wie der Schluessel fallen; diese Pixel werden mit durchsichtig
	 *    ("fehlende Farben" bei Gewehr und Granate).
	 * 3. Flaechen, die spaeter MakeAlphable() durchlaufen, bekommen dort einen
	 *    anderen Schluessel gesetzt und verlieren den hier gewaehlten.
	 *
	 * Der Weg bleibt richtig, aber er braucht zuerst einen 16-Bit-Pfad im
	 * Skalierer und eine schluesselfeste Farbwandlung. Bis dahin: Alphaweg. */
	/* 8-Bit-Pfad: Flaechen aus dem Archiv bleiben palettiert.
	 *
	 * SDL_DisplayFormatAlpha machte aus jedem Bild 32 Bit je Pixel -- bei
	 * Quellen mit 8 Bit das Vierfache, und der Blit wurde zur Alphamischung
	 * statt eines Kopiervorgangs mit Schluessel. Der Schluessel ist hier ein
	 * INDEX (Platz 0), kein Farbwert: die Kollision mit Schwarz, an der die
	 * Umstellung am 16./18.09. zweimal scheiterte, kann es nicht mehr geben.
	 * SDL setzt beim Blit auf den 16-Bit-Schirm ueber eine Tabelle um; mit
	 * einem 8-Bit-Schirm entfaellt auch das.
	 *
	 * Der Rotierer kann 8 Bit (SDL_rotozoom, transformSurfaceY) und erhaelt
	 * den Schluessel dabei. */
	SDL_Surface *new_ret = 0;

	if(!use_opengl && zod_palette_ready() &&
	   sdl_surface->format->BytesPerPixel == 1 &&
	   (sdl_surface->flags & SDL_SRCCOLORKEY))
	{
		if(!delete_surface)
		{
			//fremde Flaeche: eine eigene Kopie im selben Format
			new_ret = SDL_ConvertSurface(sdl_surface, sdl_surface->format,
			                             sdl_surface->flags);

			if(new_ret)
				SDL_SetColorKey(new_ret, SDL_SRCCOLORKEY,
				                sdl_surface->format->colorkey);
		}
		else
		{
			//sie gehoert uns bereits -- nichts zu tun
			return;
		}
	}

	if(!new_ret) new_ret = SDL_DisplayFormatAlpha(sdl_surface);


	/* Schlaegt die Wandlung fehl, lieber das Original behalten als gar nichts:
	 * die Urfassung setzte sdl_surface in dem Fall auf 0 und verlor das Bild. */
	if(new_ret)
	{
		if(delete_surface) SDL_FreeSurface( sdl_surface );
		sdl_surface = new_ret;
	}

	//checks
	if ( (sdl_surface->w & (sdl_surface->w - 1)) != 0 )
		;//ZLOG("warning: %s's width is not a power of 2\n", image_filename.c_str());
	
	// Also check if the height is a power of 2
	if ( (sdl_surface->h & (sdl_surface->h - 1)) != 0 )
		;//ZLOG("warning: %s's height is not a power of 2\n", image_filename.c_str());
}

void ZSDL_Surface::UseDisplayFormat()
{
	if(!sdl_surface) return;

	//this is not needed in opengl (?)
	if(use_opengl) return;

	SDL_Surface *new_ret;
	new_ret = SDL_DisplayFormat(sdl_surface);
	SDL_FreeSurface( sdl_surface );
	sdl_surface = new_ret;

	SetAlpha(alpha);
}
// ----------------------------------------------------
// TODO!!! ZSDL_ModifyBlack in zsdl.h, zsdl.h include zsdl-opengl!
void ZSDL_Surface::SDL_ModifyBlack(SDL_Surface *surface)
{
    SDL_Rect White_Pix_Rect;
    int rgb_map;

    rgb_map = SDL_MapRGB(surface->format, 1, 0, 0);

    for(int i=0;i<surface->w;i++)
        for(int j=0;j<surface->h;j++)
        {
            Uint8 r, g, b, a;
            Uint32 pixel;

            pixel = zod_read_pixel(surface, i, j);
            SDL_GetRGBA(pixel, surface->format, &r, &g, &b, &a);

            if(!r && !g && !b && a)
            {
                White_Pix_Rect.x = i;
                White_Pix_Rect.y = j;
                White_Pix_Rect.w = 1;
                White_Pix_Rect.h = 1;
                //SDL_FillRect(&White_Pix_Rect, 1, 0, 0, surface);
                SDL_FillRect(surface, &White_Pix_Rect, rgb_map);
            }
        }
}
//------------------------------------------------------
void ZSDL_Surface::MakeAlphable()
{
	if(!sdl_surface) return;

	//this is not needed in opengl (?)
	if(use_opengl) return;


    // TODO!!! ZSDL_ModifyBlack in zsdl.h, zsdl.h include zsdl-opengl!
    //ZSDL_ModifyBlack(sdl_surface);
    SDL_ModifyBlack( sdl_surface);


	UseDisplayFormat();
	SDL_SetColorKey(sdl_surface, SDL_SRCCOLORKEY, 0x000000); 
}


bool ZSDL_Surface::LoadRotoZoomSurface()
{
	if(!sdl_surface)
	{
		//ZLOG("LoadRotoZoomSurface::sdl_surface for %s not loaded\n", image_filename.c_str());
		return false;
	}

	zod_roto_calls++;
	zod_roto_frame_calls++;

	//liegt diese Fassung schon im Zwischenspeicher?
	for(int i=0;i<ZOD_ROTO_CACHE;i++)
	{
		if(!roto[i].surf) continue;

		if(std::fabs(roto[i].angle - angle) < 1e-3 &&
		   std::fabs(roto[i].size  - size)  < 1e-3)
		{
			roto[i].stamp = ++zod_roto_clock;
			sdl_rotozoom = roto[i].surf;
			rotozoom_loaded = true;
			return true;
		}
	}

	//freien Platz nehmen, sonst den am laengsten ungenutzten verdraengen
	int slot = 0;

	for(int i=0;i<ZOD_ROTO_CACHE;i++)
	{
		if(!roto[i].surf) { slot = i; break; }
		if(roto[i].stamp < roto[slot].stamp) slot = i;
	}

	SDL_Surface *made;

	{
		const unsigned long t0 = zod_fineclock_ticks();

		made = rotozoomSurface(sdl_surface, angle, size, 0);
		zod_roto_frame_ticks += zod_fineclock_ticks() - t0;
	}

	zod_roto_made++;
	zod_roto_frame_made++;

	if(made)
	{
		const unsigned long px = (unsigned long)made->w * made->h;

		zod_roto_pixels       += px;
		zod_roto_frame_pixels += px;
	}

	if(!made)
	{
		ZLOG("LoadRotoZoomSurface::sdl_rotozoom for %s not created (angle:%f size:%f)\n", image_filename.c_str(), angle, size);
		return false;
	}

	if(roto[slot].surf)
	{
		//Die Silhouette merkt sich ihre Quelle nur als Zeiger. Bliebe er auf
		//freigegebenem Speicher stehen, koennte die naechste Flaeche dieselbe
		//Adresse bekommen und der Zwischenspeicher still ein altes Bild liefern.
		if(hit_src == roto[slot].surf)
		{
			if(sdl_hit) SDL_FreeSurface(sdl_hit);
			sdl_hit = nullptr;
			hit_src = nullptr;
		}

		zod_roto_count--;
		zod_roto_bytes -= zod_surface_bytes(roto[slot].surf);

		{
			const unsigned long t0 = zod_fineclock_ticks();

			SDL_FreeSurface(roto[slot].surf);
			zod_roto_frame_frei += zod_fineclock_ticks() - t0;
			zod_roto_frame_weg++;
		}
	}

	zod_roto_count++;
	zod_roto_bytes += zod_surface_bytes(made);
	zod_roto_frame_bytes += (unsigned long)zod_surface_bytes(made);

	roto[slot].surf  = made;
	roto[slot].angle = angle;
	roto[slot].size  = size;
	roto[slot].stamp = ++zod_roto_clock;

	sdl_rotozoom = made;
	rotozoom_loaded = true;

	{
		const unsigned long t0 = zod_fineclock_ticks();

		RotoRegister(this);
		RotoEnforceBudget();
		zod_roto_frame_verw += zod_fineclock_ticks() - t0;
	}

	return true;
}

bool ZSDL_Surface::LoadGLtexture()
{
#ifndef DISABLE_OPENGL
	GLenum texture_format;
	GLint  nOfColors;

	if(!sdl_surface)
	{
		//ZLOG("LoadGLtexture::sdl_surface for %s not loaded\n", image_filename.c_str());
		return false;
	}

	//delete texture if it is already loaded
	if(gl_texture_loaded)
		glDeleteTextures(1, &gl_texture);

	nOfColors = sdl_surface->format->BytesPerPixel;
	switch(nOfColors)
	{
	case 4:
		if (sdl_surface->format->Rmask == 0x000000ff) texture_format = GL_RGBA;   
		else texture_format = GL_BGRA;
		break;
	case 3:
		if (sdl_surface->format->Rmask == 0x000000ff) texture_format = GL_RGB;
		else texture_format = GL_BGR;
		break;
	default:
		ZLOG("LoadGLtexture::%s does not have proper image format\n", image_filename.c_str());
		return false;
        // TODO  break;   will never be executed!
	}

	// Have OpenGL generate a texture object handle for us
	glGenTextures( 1, &gl_texture );
 
	// Bind the texture object
	glBindTexture( GL_TEXTURE_2D, gl_texture );
 
	// Set the texture's stretching properties
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
 
	// Edit the texture object's image data using the information SDL_Surface gives us
    glTexImage2D( GL_TEXTURE_2D, 0, nOfColors, sdl_surface->w,
                  sdl_surface->h, 0, texture_format, GL_UNSIGNED_BYTE,
                  sdl_surface->pixels );

	gl_texture_loaded = true;

	return true;
#else
	return false;
#endif
}

void ZSDL_Surface::SetUseOpenGL(bool use_opengl_)
{
	use_opengl = use_opengl_;
}

void ZSDL_Surface::SetMainSoftwareSurface(SDL_Surface *screen_)
{
	screen = screen_;
}

void ZSDL_Surface::SetScreenDimensions(int w_, int h_)
{
	screen_w = w_;
	screen_h = h_;
}

void ZSDL_Surface::SetMapPlace(int x, int y)
{
	map_place_x = x;
	map_place_y = y;
}

void ZSDL_Surface::SetMapWindow(int x, int y, int w, int h)
{
	karte_x = x;
	karte_y = y;
	karte_w = w;
	karte_h = h;
}

void ZSDL_Surface::SetHasHud(bool has_hud_)
{
	has_hud = has_hud_;
}

//Winkel auf feste Stufen runden. Der Zwischenspeicher haelt genau EINE
//gedrehte Flaeche; ohne Quantisierung aendert sich der Winkel in jedem Bild
//(z. B. ETurrentMissile, ABird), die Flaeche wird jedes Mal neu berechnet UND
//neu belegt. Auf AmigaOS ist gerade das Belegen teuer.
//32 Stufen = 11,25 Grad. Winkel unter 5,6 Grad fallen damit auf 0 und nehmen
//den schnellen Pfad ganz ohne Drehung.
#define ZOD_ANGLE_STEPS 32.0f

/* `floorf` ist in dieser Werkzeugkette ein echter Bibliothekssprung -- belegt
 * mit `nm --undefined-only` auf zsdl_opengl.o: `U _floorf`. Beide Rasterungen
 * laufen JE EFFEKT und JE BILD; in der Explosion sind das ueber 800 Effekte,
 * also Hunderte Spruenge je Bild, nur um auf ein Raster zu runden.
 *
 * Fuer nicht-negative Werte ist Abschneiden dasselbe wie floor, und `(int)`
 * ist auf dem 68080 ein einzelner `fintrz` in Hardware. Negative Werte gehen
 * weiter ueber floorf -- dort sind die beiden NICHT gleich.
 *
 * Achtung: genau diese Wandlung traf die Bebbo-Falle, die `fintrz` loescht
 * und `(int)x` runden laesst. Der Bau nimmt `e` aus
 * BBBFLAGS heraus, deshalb ist sie hier behoben -- aber falls die Flagge je
 * zurueckkommt, rundet das hier statt abzuschneiden. */
static inline float zod_floor_schnell(float v)
{
	if(v < 0.0f) return floorf(v);

	return (float)(int)v;
}

static inline float zod_quantize_angle(float a)
{
	const float step = 360.0f / ZOD_ANGLE_STEPS;

	return zod_floor_schnell(a / step + 0.5f) * step;
}

//Groesse in Zwanzigstel-Schritten. is1() vergleicht mit Toleranz 1e-5, die
//exakte 1,0 bleibt also erhalten und der ungedrehte Pfad greift weiterhin.
/* Histogramm der verlangten Groessen -- acht Klassen.
 *
 * Der Nutzer hat am 19.09. beobachtet, dass der Einbruch kommt, waehrend die
 * Truemmer WEIT OBEN sind. Und die Hoehe IST die Groesse:
 * ETurrentMissile::Process rechnet `y -= (size - 1.0) * 30 * ArcFactor()`.
 * Die Arbeit einer Drehung waechst mit size^2 -- aus einem 32x32-Sprite wird
 * bei Groesse 6,0 ein Huellrechteck von rund 272x272 = 74 KB.
 *
 * Diese Zeile belegt, ob die Spitze wirklich dort liegt. Ohne sie ist die
 * mittlere Groesse "3,5" eine Annahme aus einer Hochrechnung. */
unsigned long zod_size_hist[8];

extern "C" void zod_size_hist_report(unsigned long *h)
{
	for(int i = 0; i < 8; i++) h[i] = zod_size_hist[i];
}

static inline void zod_size_note(float s)
{
	int k = (int)s;              /* 0..1, 1..2, ... 7+ */

	if(k < 0) k = 0;
	if(k > 7) k = 7;

	zod_size_hist[k]++;
}

/* ---- ZOD_ROTOQUANT: wie fein die Groesse gerastert wird -----------------
 *
 * Diese Zahl stand seit je auf 0,05 und ist nie hinterfragt worden. Sie ist
 * aber der Hebel auf BEIDE Haelften des groessten verbliebenen Postens --
 * auf der V1200 am 20.09. mit dem Stufen-Messbetrieb aufgeteilt (der
 * Schalter ZOD_EFFSTUFE ist danach wieder entfernt worden):
 *
 *   Drehflaechen-Zugriff je Effekt   24172 ns
 *     davon Rechnen                  13187 ns  (55 %)
 *     davon Nachschlagen             10985 ns  (45 %)
 *
 * Jeder Rasterschritt erzeugt einen NEUEN Eintrag: `SetSize` macht
 * `rotozoom_loaded` ungueltig, sobald sich die Groesse um mehr als 1e-3
 * aendert. Bei 0,05 ist das bei einem fliegenden Truemmerteil in JEDEM Bild
 * der Fall -- also je Bild ein Nachschlagen ueber acht Plaetze und haeufig
 * eine neue Rechnung.
 *
 * Groeber rastern heisst: dieselbe Groesse wiederholt sich ueber mehrere
 * Bilder, `rotozoom_loaded` bleibt gueltig, und das Nachschlagen entfaellt
 * GANZ. Zusaetzlich teilen sich mehr Effekte dieselbe Flaeche, die
 * Fehltreffer werden also seltener. Und weil `is1()` mit Toleranz 1e-5
 * vergleicht, rasten mehr Groessen auf genau 1,0 ein -- die nehmen dann den
 * Weg ganz ohne Drehflaeche.
 *
 * GEMESSEN AUF DER V1200 (20.09., beide Laeufe mit demselben Messbetrieb;
 * der Raster-25-Lauf war mit 392882 gegen 311030 Groessenwuenschen der
 * SCHWERERE):
 *
 *                               Raster 0,05   Raster 0,25
 *   je Effekt (voll)             46090 ns      19482 ns   (-58 %)
 *   Drehflaechen-Zugriff         24172 ns       2934 ns   (Faktor 8,2)
 *     davon Rechnen              13187 ns        910 ns   (Faktor 14,5)
 *     davon Nachschlagen         10985 ns       2024 ns   (Faktor 5,4)
 *   gerechnete Flaechen im Lauf  11166          1145
 *
 * Vorhergesagt hatte ich Faktor 3.
 *
 * DIE OPTISCHE FRAGE KONNTE KEINE MESSUNG BEANTWORTEN: Bei Groesse 2 ist ein
 * 0,05-Schritt 2,5 %, bei 0,25 sind es 12,5 % -- ob die Truemmer beim Wachsen
 * sichtbar in Stufen springen, sieht nur ein Mensch am Bildschirm. Der Nutzer
 * hat es angesehen: "ja sieht gut aus" (20.09.). Erst damit ist 0,25 die
 * Vorgabe.
 *
 * FEST VERDRAHTET seit der Abnahme -- der frueher vorhandene Schalter
 * ZOD_ROTOQUANT ist entfallen. Als `const` steht die Zahl dem Uebersetzer zur
 * Verfuegung: die Division in zod_quantize_size wird damit zur Multiplikation
 * mit dem Kehrwert, und auf dem 68080 faellt eine FPU-Division je Aufruf weg. */
static const float zod_roto_quant = 0.25f;

static inline float zod_quantize_size(float s)
{
	return zod_floor_schnell(s / zod_roto_quant + 0.5f) * zod_roto_quant;
}

/* Nur fuer die Protokollzeile. Sie ist die Gegenprobe, dass das Raster
 * ueberhaupt greift: Beim Entfernen des alten Messbetriebs ist mir der
 * ZOD_ROTOQUANT-Block versehentlich mitgeloescht worden, und aufgefallen ist
 * es einzig daran, dass diese Zeile im Probelauf fehlte. */
extern "C" int zod_roto_quant_get(void)
{
	return (int)(zod_roto_quant * 100.0f + 0.5f);
}

/* ---- ZOD_ROTOMAX: Deckel auf die GEZEICHNETE Groesse --------------------
 *
 * Es gibt in der Engine KEINEN Deckel. Die Groesse ist eine Parabel ueber die
 * Flugzeit (`erockturrent.cpp:119`, `eturrentmissile.cpp:176`):
 *
 *     size = -(rise / (final_time - init_time)) * t^2 + rise * t + 1
 *
 * Ihr Hoechstwert liegt bei der halben Flugzeit und betraegt
 * `1 + rise * T / 4`. Beide Faktoren sind gewuerfelt:
 *
 *   ETurrentMissile (Fort-Truemmer)  rise 1,00-3,99  T 3,00-4,99 s  -> 5,98
 *   ERockTurrent / EBridgeTurrent    rise 1,10-3,09  T 1,5-2,4 s    -> 2,85
 *   EMapObjectTurrent                rise 0,50-1,49  T 3,0-3,99 s   -> 2,49
 *
 * (Die Flugdauer der Fort-Truemmer steht in `bfort.cpp:452`.) Das deckt sich
 * mit dem Messlauf: `Groessen verlangt ... 5-6:200 6-7:0 7+:0`.
 *
 * Ein Truemmersprite ist 16x16. Bei Groesse 5,98 wird daraus 96x96 = 9216
 * Bildpunkte, das 36-fache der Flaeche.
 *
 * DER DECKEL GREIFT HIER UND NUR HIER, und das ist der Punkt: Die FLUGHOEHE
 * rechnen die Effektklassen aus IHRER EIGENEN `size`-Variablen, BEVOR sie
 * SetSize rufen (`y -= (size - 1) * 30 * ArcFactor()`). Flugbahn, Flugdauer
 * und damit jede Spielmechanik bleiben unberuehrt -- die Truemmer fliegen
 * genauso hoch und weit, werden nur nicht mehr so gross gezeichnet.
 *
 * Nicht zu verwechseln mit `-N` (NoScaler): das klemmt die Groesse auf 1,0
 * UND setzt den Flugbogen auf null.
 *
 * Vorgabe: 0 = kein Deckel, also unveraendert. `SetEnv ZOD_ROTOMAX 300` setzt
 * ihn auf 3,00 (Hundertstel wie bei ZOD_ROTOQUANT).
 *
 * Gedacht ist er fuer die kommenden 68040/68060- und AGA-Bauten und fuer
 * 320x200 -- dort ist ein 96x96-Truemmerteil fast ein Drittel der
 * Bildschirmbreite. */
static float zod_roto_max = 0.0f;
static unsigned long zod_roto_max_greift = 0;

extern "C" void zod_roto_max_set(int hundertstel)
{
	if(hundertstel < 0) hundertstel = 0;

	zod_roto_max = (float)hundertstel / 100.0f;

	/* Den Deckel selbst aufs Raster legen, damit er keine zusaetzliche
	 * Groessenstufe erzeugt -- sonst kostete er einen eigenen Eintrag im
	 * Drehflaechenspeicher. */
	if(zod_roto_max > 0.0f) zod_roto_max = zod_quantize_size(zod_roto_max);
}

extern "C" int zod_roto_max_get(void)
{
	return (int)(zod_roto_max * 100.0f + 0.5f);
}

extern "C" unsigned long zod_roto_max_greift_get(void)
{
	return zod_roto_max_greift;
}

void ZSDL_Surface::SetSize(float size_)
{
	/* NoScaler: die Groesse bleibt exakt 1.0, damit is1() greift und der
	 * skalierungsfreie Pfad in RenderSurface UND in BlitHitSurface gewaehlt
	 * wird. Der uebergebene Wert wird verworfen -- alle 11 Aufrufer bleiben
	 * unangetastet. */
	zod_size_note(size_);   /* VOR dem Klemmen -- die verlangte Groesse zaehlt */

	if(no_scaler) size_ = 1.0f;
	else
	{
		size_ = zod_quantize_size(size_);

		/* Deckel NACH dem Rastern: so bleibt er auf einer Rasterstufe und
		 * erzeugt keinen eigenen Eintrag im Drehflaechenspeicher. */
		if(zod_roto_max > 0.0f && size_ > zod_roto_max)
		{
			size_ = zod_roto_max;
			zod_roto_max_greift++;
		}
	}

	/* Frueher wurde hier die Drehflaeche weggeworfen. Der Zwischenspeicher
	 * haelt jetzt mehrere Fassungen -- beim Wechsel wird nichts mehr
	 * freigegeben, nur die aktuelle Auswahl als ungueltig vermerkt. */
    if(!use_opengl && (std::fabs(size-size_) >= 1e-3))
		rotozoom_loaded = false;

	size = size_;
}

//Drehflaeche verwerfen -- und dabei die Silhouette mitnehmen, falls sie aus
//genau dieser Flaeche gebaut wurde. Der Merker haelt nur einen Zeiger; bliebe
//er auf freigegebenem Speicher stehen, koennte eine neue Flaeche dieselbe
//Adresse bekommen und der Zwischenspeicher still ein altes Bild liefern.
void ZSDL_Surface::DropRotoZoom()
{
	RotoUnregister(this);

	for(int i=0;i<ZOD_ROTO_CACHE;i++)
	{
		if(!roto[i].surf) continue;

		if(hit_src == roto[i].surf)
		{
			if(sdl_hit) SDL_FreeSurface(sdl_hit);
			sdl_hit = nullptr;
			hit_src = nullptr;
		}

		zod_roto_count--;
		zod_roto_bytes -= zod_surface_bytes(roto[i].surf);

		SDL_FreeSurface(roto[i].surf);
		roto[i].surf = nullptr;
	}

	sdl_rotozoom = nullptr;
	rotozoom_loaded = false;
}

void ZSDL_Surface::SetAngle(float angle_)
{
	angle_ = zod_quantize_angle(angle_);

	//wie bei SetSize: nur die Auswahl wechseln, nichts freigeben
    if(!use_opengl && (std::fabs(angle_-angle) >= 1e-3))
		rotozoom_loaded = false;

	angle = angle_;
}


// !!!TODO  SDL_SetAlpha alpha <Uint8> type!!!  char
void ZSDL_Surface::SetAlpha(uint8_t alpha_) //char alpha_)
{
	alpha = alpha_;

	if(!use_opengl)
	{
		/* SDL_SRCALPHA nur setzen, wenn wirklich gemischt werden soll.
		 * Bedingungslos gesetzt zwingt es SDL auch bei alpha=255 in den
		 * Mischpfad, und zusammen mit einem Farbschluessel in den teuersten
		 * Blitter ueberhaupt (Flaechenalpha UND Schluessel) -- auf dem Host
		 * gemessen 12x langsamer als derselbe Blit ohne SDL_SRCALPHA.
		 * SDL_RLEACCEL ist bewusst entfallen: GetHitSurface greift direkt auf
		 * die Pixel dieser Flaechen zu, was mit RLE-Kodierung bricht. */
		Uint32 blend = (alpha == 255) ? 0 : SDL_SRCALPHA;

		if(sdl_surface) SDL_SetAlpha(sdl_surface, blend, alpha);
		if(sdl_rotozoom) SDL_SetAlpha(sdl_rotozoom, blend, alpha);
	}
}

void ZSDL_Surface::FillRectOnToMe(SDL_Rect *dstrect, Uint8 r, Uint8 g, Uint8 b)
{
	if(!sdl_surface) return;

	SDL_FillRect(sdl_surface, dstrect, SDL_MapRGB(sdl_surface->format, r,g,b));

	if(gl_texture_loaded)
	{
#ifndef DISABLE_OPENGL
		glDeleteTextures(1, &gl_texture);
#endif
		gl_texture_loaded = false;
	}

	//unload rotozoom surface?
	if(!use_opengl)
		DropRotoZoom();
}

void ZSDL_Surface::BlitOnToMe(SDL_Rect *srcrect, SDL_Rect *dstrect, SDL_Surface *src)
{
	if(!sdl_surface) return;

	SDL_BlitSurface(src, srcrect, sdl_surface, dstrect);

	if(gl_texture_loaded)
	{
#ifndef DISABLE_OPENGL
		glDeleteTextures(1, &gl_texture);
#endif
		gl_texture_loaded = false;
	}

	//unload rotozoom surface?
	if(!use_opengl)
		DropRotoZoom();
}

//ZSDL_Surface has made itself into an engine it seems...
void ZSDL_Surface::ZSDL_FillRect(SDL_Rect *dstrect, Uint8 r, Uint8 g, Uint8 b, ZSDL_Surface *dst)
{
	if(dst)
	{
		dst->FillRectOnToMe(dstrect, r, g, b);
		return;
	}

	if(use_opengl)
	{
#ifndef DISABLE_OPENGL
		if(dstrect)
		{

			glPushMatrix();

			glColor4ub(r,g,b,255);
			//glColor3f(r,g,b);
			glBindTexture(GL_TEXTURE_2D, 0);

			glTranslatef(dstrect->x,dstrect->y,0.0f);

			glBegin(GL_QUADS);

			glVertex3f(0.0f, 0.0f, 0.0f);		// Top Left
			glVertex3f(dstrect->w, 0.0f, 0.0f);		// Top Right
			glVertex3f(dstrect->w, dstrect->h, 0.0f);		// Bottom Right
			glVertex3f(0.0f, dstrect->h, 0.0f);		// Bottom Left
			glEnd();

			glPopMatrix();

			//glColor3f(1,1,1);
		}
		else
		{
			//we are supposed to fill the whole screen with this color
			//this area is not debuged.
			
			glClearColor(r / 255.0, g / 255.0, b / 255.0, 0.0f); // Clear The Background Color To Black
			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);	// Clear The Screen And The Depth Buffer
			glLoadIdentity(); // Reset The View
		}
#endif
	}
	else
	{
		if(screen) SDL_FillRect(screen, dstrect, SDL_MapRGB(screen->format, r,g,b));
	}
}

/* Viele kleine gleichfarbige Flaechen auf einmal.
 *
 * Der Weg ueber SDL_FillRect kostet je Aufruf ein SDL_MapRGB, das Verschneiden
 * mit dem Ausschnitt, Sperren und Freigeben der Flaeche. Bei den
 * Wegpunktlinien sind das bei jedem Bild Hunderte Aufrufe fuer je vier Pixel --
 * auf dem 68080 deutlich als Ruckeln zu sehen. Hier wird die Farbe einmal
 * umgerechnet, der Schirm einmal gesperrt und direkt geschrieben. */
void ZSDL_Surface::ZSDL_FillDots(const SDL_Rect *dots, int count, Uint8 r, Uint8 g, Uint8 b)
{
	if(!dots || count <= 0) return;

	if(use_opengl)
	{
		for(int i=0;i<count;i++)
		{
			SDL_Rect one = dots[i];
			ZSDL_FillRect(&one, r, g, b);
		}
		return;
	}

	if(!screen) return;

	Uint32 color = SDL_MapRGB(screen->format, r, g, b);
	int bpp = screen->format->BytesPerPixel;
	int cw = screen->w;
	int ch = screen->h;

	if(SDL_MUSTLOCK(screen) && SDL_LockSurface(screen) < 0) return;

	for(int i=0;i<count;i++)
	{
		int x0 = dots[i].x;
		int y0 = dots[i].y;
		int x1 = x0 + dots[i].w;
		int y1 = y0 + dots[i].h;

		if(x0 < 0)  x0 = 0;
		if(y0 < 0)  y0 = 0;
		if(x1 > cw) x1 = cw;
		if(y1 > ch) y1 = ch;

		if(x0 >= x1 || y0 >= y1) continue;

#ifdef __amigaos__
		/* Diese Funktion schreibt DIREKT in die Zeichenflaeche, nicht ueber
		 * SDL_FillRect. Die Ausgabe kopiert seit der Schmutzspur nur noch,
		 * was vermerkt ist -- ohne diese Zeile blieben Wegpunktlinien und
		 * Angriffsradius unsichtbar. */
		zod_dirty_mark(screen, x0, y0, x1 - x0, y1 - y0);
#endif

		for(int y=y0;y<y1;y++)
		{
			Uint8 *p = (Uint8 *)screen->pixels + y * screen->pitch + x0 * bpp;

			switch(bpp)
			{
			case 1:
				for(int x=x0;x<x1;x++) { *p = (Uint8)color; p += 1; }
				break;

			case 2:
				for(int x=x0;x<x1;x++) { *(Uint16 *)p = (Uint16)color; p += 2; }
				break;

			case 3:
				for(int x=x0;x<x1;x++)
				{
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
					p[0] = (Uint8)(color >> 16); p[1] = (Uint8)(color >> 8); p[2] = (Uint8)color;
#else
					p[0] = (Uint8)color; p[1] = (Uint8)(color >> 8); p[2] = (Uint8)(color >> 16);
#endif
					p += 3;
				}
				break;

			default:
				for(int x=x0;x<x1;x++) { *(Uint32 *)p = color; p += 4; }
				break;
			}
		}
	}

	if(SDL_MUSTLOCK(screen)) SDL_UnlockSurface(screen);
}

void ZSDL_Surface::RenderSurface(int x, int y, bool render_hit, bool about_center)
{
	if(use_opengl)
	{
		//if(!WillRenderOnScreen(x, y, about_center)) return;

		if(render_hit)
		{
			SDL_Rect to_rect;

			to_rect.x = x + map_place_x;
			to_rect.y = y + map_place_y;

            BlitHitSurface(nullptr, &to_rect, nullptr, true);
			return;
		}

#ifndef DISABLE_OPENGL
		if(!gl_texture_loaded && !LoadGLtexture()) return;

		glPushMatrix();

		glTranslatef(x, y, 0.0f);

		//scale
		glScalef( size, size, 1.0 );

		//rotate about center
		if(!about_center)
		glTranslatef((sdl_surface->w >> 1), (sdl_surface->h >> 1), 0.0f);
		glRotatef(-angle, 0.0f, 0.0f, 1.0f);
		glTranslatef(-(sdl_surface->w >> 1), -(sdl_surface->h >> 1), 0.0f);
		

		//we are at the center now, so don't translate back
		//if(!about_center) glTranslatef(-(sdl_surface->w >> 1), -(sdl_surface->h >> 1), 0.0f);

		//alpha
		glColor4ub(255,255,255,alpha);

		glBindTexture(GL_TEXTURE_2D, gl_texture);

		glBegin(GL_QUADS);

		glTexCoord2f(0.0f, 1.0f); glVertex3f( 0.0f, sdl_surface->h,  0.0f);	// Bottom Left Of The Texture and Quad
		glTexCoord2f(1.0f, 1.0f); glVertex3f( sdl_surface->w, sdl_surface->h,  0.0f);	// Bottom Right Of The Texture and Quad
		glTexCoord2f(1.0f, 0.0f); glVertex3f( sdl_surface->w, 0.0f,  0.0f);	// Top Right Of The Texture and Quad
		glTexCoord2f(0.0f, 0.0f); glVertex3f( 0.0f, 0.0f,  0.0f);	// Top Left Of The Texture and Quad

		glEnd();

		glPopMatrix();
#endif
	}
	else
	{
		SDL_Surface *render_surface;

		//should we be using the rotozoom surface?
		if(!isz(angle) || !is1(size))
		{
			/* Erst keulen, dann skalieren.
			 *
			 * LoadRotoZoomSurface belegt eine neue Flaeche und rechnet sie
			 * Pixel fuer Pixel aus -- und das geschah bisher AUCH fuer Sprites,
			 * die GetMapBlitInfo unmittelbar danach als unsichtbar verworfen
			 * hat. Beim Tod eines Forts fliegen die Truemmer bis 200 Pixel
			 * ueber den Kartenrand hinaus und wurden dort weiterhin voll
			 * gedreht, skaliert und belegt.
			 *
			 * Die Schranke ist absichtlich grosszuegig: eine Drehung
			 * vergroessert das umschliessende Rechteck um hoechstens den
			 * Faktor Wurzel 2, und about_center verschiebt um hoechstens die
			 * halbe Kantenlaenge. Verworfen wird also nur, was sicher
			 * ausserhalb liegt -- am Bildrand bleibt alles wie bisher. */
			if(sdl_surface)
			{
				int cull_w = screen_w - map_place_x;
				int cull_h = screen_h - map_place_y;

				if(has_hud)
				{
					cull_w -= HUD_WIDTH;
					cull_h -= HUD_HEIGHT;
				}

				int side = (sdl_surface->w > sdl_surface->h) ? sdl_surface->w : sdl_surface->h;
				int ext  = (int)(1.415f * size * side) + 2;

				if(x - ext > cull_w) return;
				if(y - ext > cull_h) return;
				if(x + ext < 0) return;
				if(y + ext < 0) return;
			}

			if(!rotozoom_loaded && !LoadRotoZoomSurface()) return;
			render_surface = sdl_rotozoom;
		}
		else
			render_surface = sdl_surface;

		if(!render_surface) return;

		if(about_center)
		{
			x -= render_surface->w >> 1;
			y -= render_surface->h >> 1;
		}

		SDL_Rect from_rect, to_rect;

		if(GetMapBlitInfo(render_surface, x, y, from_rect, to_rect))
		{
			to_rect.x += map_place_x;
			to_rect.y += map_place_y;

			if(render_hit)	
                BlitHitSurface(&from_rect, &to_rect, nullptr, true);  // TODO nullptr
			else
				SDL_BlitSurface(render_surface, &from_rect, screen, &to_rect);
		}
	}
}

void ZSDL_Surface::RenderSurfaceHorzRepeat(int x, int y, int w_total, bool render_hit)
{
	SDL_Rect from_rect, to_rect;
	int fw, fh;

	if(!sdl_surface) return;

	fw = sdl_surface->w;
	fh = sdl_surface->h;

	while(w_total>0)
	{
		to_rect.x = x;
		to_rect.y = y;

		if(w_total > fw)
		{
            BlitHitSurface(nullptr, &to_rect, nullptr, render_hit);// TODO nullptr

			w_total -= fw;
			x += fw;
		}
		else
		{
			from_rect.x=0;
			from_rect.y=0;
			from_rect.w=w_total;
			from_rect.h=fh;

            BlitHitSurface(&from_rect, &to_rect, nullptr, render_hit);

			w_total = 0;
		}
	}
}

void ZSDL_Surface::RenderSurfaceVertRepeat(int x, int y, int h_total, bool render_hit)
{
	SDL_Rect from_rect, to_rect;
	int fw, fh;

	if(!sdl_surface) return;

	fw = sdl_surface->w;
	fh = sdl_surface->h;

	while(h_total>0)
	{
		to_rect.x = x;
		to_rect.y = y;

		if(h_total > fh)
		{
            BlitHitSurface(nullptr, &to_rect, nullptr, render_hit); // TODO nullptr

			h_total -= fh;
			y += fh;
		}
		else
		{
			from_rect.x=0;
			from_rect.y=0;
			from_rect.w=fw;
			from_rect.h=h_total;

            BlitHitSurface(&from_rect, &to_rect, nullptr, render_hit);// TODO nullptr

			h_total = 0;
		}
	}
}

void ZSDL_Surface::RenderSurfaceAreaRepeat(int x, int y, int w, int h, bool render_hit)
{
	SDL_Rect from_rect, to_rect;
	int fw, fh;
	int w_left, h_left;
	int ox, oy;

	if(!sdl_surface) return;

	fw = sdl_surface->w;
	fh = sdl_surface->h;

	oy=y;
	h_left=h;
	while(h_left>0)
	{
		ox=x;
		w_left=w;
		while(w_left>0)
		{
			from_rect.x=0;
			from_rect.y=0;
			from_rect.w=w_left;
			from_rect.h=h_left;

			to_rect.x = ox;
			to_rect.y = oy;

            BlitHitSurface(&from_rect, &to_rect, nullptr, render_hit); // TODO nullptr

			ox+=fw;
			w_left-=fw;
		}

		oy+=fh;
		h_left-=fh;
	}
}

void ZSDL_Surface::BlitSurface(ZSDL_Surface *dst, int x, int y)
{
	SDL_Rect to_rect;

	to_rect.x = x;
	to_rect.y = y;

    BlitSurface(nullptr, &to_rect, dst); //TODO nullptr
}

void ZSDL_Surface::BlitSurface(int fx, int fy, int fw, int fh, ZSDL_Surface *dst, int x, int y)
{
	SDL_Rect from_rect, to_rect;

	from_rect.x = fx;
	from_rect.y = fy;
	from_rect.w = fw;
	from_rect.h = fh;

	to_rect.x = x;
	to_rect.y = y;
	to_rect.w = fw;
	to_rect.h = fh;

	BlitSurface(&from_rect, &to_rect, dst);
}

void ZSDL_Surface::BlitSurface(SDL_Rect *srcrect, SDL_Rect *dstrect, ZSDL_Surface *dst)
{
	if(srcrect)
	{
		if(srcrect->h <= 0) return;
		if(srcrect->w <= 0) return;
	}

	if(use_opengl)
	{
		//blit on to them
		if(dst)
		{
			dst->BlitOnToMe(srcrect, dstrect, sdl_surface);
			return;
		}

#ifndef DISABLE_OPENGL
		if(!gl_texture_loaded && !LoadGLtexture()) return;

		glPushMatrix();

		if(dstrect) glTranslatef(dstrect->x, dstrect->y, 0.0f);
		//else glTranslatef(0.0, 0.0, 0.0f);

		//scale
		glScalef( size, size, 1.0 );

		//alpha
		glColor4ub(255,255,255,alpha);

		//rotate about center
		glTranslatef((sdl_surface->w >> 1), (sdl_surface->h >> 1), 0.0f);
		glRotatef(angle, 0.0f, 0.0f, 1.0f);
		glTranslatef(-(sdl_surface->w >> 1), -(sdl_surface->h >> 1), 0.0f);

		glBindTexture(GL_TEXTURE_2D, gl_texture);

		glBegin(GL_QUADS);

		if(!srcrect)
		{
			glTexCoord2f(0.0f, 1.0f); glVertex3f( 0.0f, sdl_surface->h,  0.0f);	// Bottom Left Of The Texture and Quad
			glTexCoord2f(1.0f, 1.0f); glVertex3f( sdl_surface->w, sdl_surface->h,  0.0f);	// Bottom Right Of The Texture and Quad
			glTexCoord2f(1.0f, 0.0f); glVertex3f( sdl_surface->w, 0.0f,  0.0f);	// Top Right Of The Texture and Quad
			glTexCoord2f(0.0f, 0.0f); glVertex3f( 0.0f, 0.0f,  0.0f);	// Top Left Of The Texture and Quad
		}
		else
		{
			if(srcrect->x + srcrect->w > sdl_surface->w) srcrect->w = sdl_surface->w - srcrect->x;
			if(srcrect->y + srcrect->h > sdl_surface->h) srcrect->h = sdl_surface->h - srcrect->y;

			GLfloat tex_x = (1.0 * srcrect->x) / sdl_surface->w;
			GLfloat tex_x2 = (1.0 * srcrect->x + srcrect->w) / sdl_surface->w;
			GLfloat tex_y = (1.0 * srcrect->y) / sdl_surface->h;
			GLfloat tex_y2 = (1.0 * srcrect->y + srcrect->h) / sdl_surface->h;

			glTexCoord2f(tex_x,  tex_y2); glVertex3f( 0.0f, srcrect->h,  0.0f);	// Bottom Left Of The Texture and Quad
			glTexCoord2f(tex_x2, tex_y2); glVertex3f( srcrect->w, srcrect->h,  0.0f);	// Bottom Right Of The Texture and Quad
			glTexCoord2f(tex_x2, tex_y); glVertex3f( srcrect->w, 0.0f,  0.0f);	// Top Right Of The Texture and Quad
			glTexCoord2f(tex_x,  tex_y); glVertex3f( 0.0f, 0.0f,  0.0f);	// Top Left Of The Texture and Quad
		}

		glEnd();

		glPopMatrix();
#endif
	}
	else
	{
		if(!sdl_surface) return;

		//software render
		if(!dst)
			SDL_BlitSurface(sdl_surface, srcrect, screen, dstrect);
		else
			dst->BlitOnToMe(srcrect, dstrect, sdl_surface);
	}
}

//Ein Pixel beliebiger Farbtiefe lesen bzw. schreiben. Die alte Fassung las
//immer 32 Bit -- nach MakeAlphable sind Sprites aber durch SDL_DisplayFormat
//gelaufen und auf dem Amiga damit 16 Bit breit. Sie las also je Pixel zwei
//Bytes des Nachbarn mit.
static inline Uint32 zsdl_get_pixel(const Uint8 *p, int bpp)
{
	switch(bpp)
	{
	case 1: return *p;
	case 2: return *(const Uint16*)p;
	case 3:
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
		return (p[0] << 16) | (p[1] << 8) | p[2];
#else
		return p[0] | (p[1] << 8) | (p[2] << 16);
#endif
	default: return *(const Uint32*)p;
	}
}

static inline void zsdl_put_pixel(Uint8 *p, int bpp, Uint32 pixel)
{
	switch(bpp)
	{
	case 1: *p = (Uint8)pixel; break;
	case 2: *(Uint16*)p = (Uint16)pixel; break;
	case 3:
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
		p[0] = (Uint8)(pixel >> 16); p[1] = (Uint8)(pixel >> 8); p[2] = (Uint8)pixel;
#else
		p[0] = (Uint8)pixel; p[1] = (Uint8)(pixel >> 8); p[2] = (Uint8)(pixel >> 16);
#endif
		break;
	default: *(Uint32*)p = pixel; break;
	}
}

//Weisse Silhouette der Quelle: gleiche Groesse, gleiches Format, gleiche
//Maskierung (Farbschluessel bzw. Alpha) -- nur alle sichtbaren Pixel sind weiss.
//Damit wird aus dem Treffer-Aufblitzen EIN Blit statt eines SDL_FillRect je
//Pixel (fuer ein getroffenes 32x32-Fahrzeug bis zu 1024 Aufrufe, jeder mit
//SDL_MapRGB, Clipping und Sperrlogik).
static SDL_Surface *zsdl_make_white_silhouette(SDL_Surface *src)
{
	if(!src || !src->format) return nullptr;

	SDL_Surface *out = SDL_ConvertSurface(src, src->format, src->flags);
	if(!out) return nullptr;

	const int bpp = out->format->BytesPerPixel;
	const bool has_key = (src->flags & SDL_SRCCOLORKEY) != 0;
	const Uint32 key = src->format->colorkey;

	if(SDL_MUSTLOCK(out)) SDL_LockSurface(out);

	for(int j = 0; j < out->h; j++)
	{
		Uint8 *row = (Uint8*)out->pixels + j * out->pitch;

		for(int i = 0; i < out->w; i++)
		{
			Uint8 *p = row + i * bpp;
			Uint32 pixel = zsdl_get_pixel(p, bpp);

			//durchsichtige Stellen bleiben, wie sie sind: der Farbschluessel
			//bzw. das Alpha muss den Umriss weiterhin ausschneiden
			if(has_key && pixel == key) continue;

			Uint8 r, g, b, a;
			SDL_GetRGBA(pixel, out->format, &r, &g, &b, &a);
			if(!a) continue;

			zsdl_put_pixel(p, bpp, SDL_MapRGBA(out->format, 255, 255, 255, a));
		}
	}

	if(SDL_MUSTLOCK(out)) SDL_UnlockSurface(out);

	//Maskierung ausdruecklich uebernehmen, damit der Blit genauso ausschneidet
	//wie beim gewoehnlichen Bild
	if(has_key)
		SDL_SetColorKey(out, SDL_SRCCOLORKEY | (src->flags & SDL_RLEACCEL), key);
	if(src->flags & SDL_SRCALPHA)
		SDL_SetAlpha(out, SDL_SRCALPHA, src->format->alpha);

	return out;
}

//Nachweis, dass der Trefferpfad wirklich laeuft, und Beleg fuer die Ersparnis:
//frueher ein SDL_FillRect je sichtbarem Pixel, jetzt ein Blit je Treffer und
//eine Silhouette je Bild. zod_hit_report() meldet beides beim Beenden.
static long zod_hit_silhouettes = 0;
static long zod_hit_blits = 0;

void zod_hit_report()
{
	if(zod_hit_blits)
		ZLOG("Treffer-Aufblitzen: %ld Blits, %ld Silhouetten erzeugt\n",
		     zod_hit_blits, zod_hit_silhouettes);
}

SDL_Surface *ZSDL_Surface::GetHitSurface(SDL_Surface *src)
{
	if(!src) return nullptr;

	if(sdl_hit && hit_src == src) return sdl_hit;

	zod_hit_silhouettes++;

	if(sdl_hit) SDL_FreeSurface(sdl_hit);

	sdl_hit = zsdl_make_white_silhouette(src);
	hit_src = sdl_hit ? src : nullptr;

	return sdl_hit;
}

void ZSDL_Surface::BlitHitSurface(SDL_Rect *srcrect, SDL_Rect *dstrect, ZSDL_Surface *dst, bool render_hit)
{
	if(!render_hit)
	{
		BlitSurface(srcrect, dstrect, dst);
		return;
	}

	//Dieselbe Flaeche waehlen wie der gewoehnliche Weg. Die alte Fassung nahm
	//immer sdl_surface, obwohl RenderSurface den Ausschnitt auf der gedrehten
	//Flaeche ausrechnet -- Ausschnitt und Bild passten bei jeder Drehung nicht
	//zueinander.
	SDL_Surface *src = sdl_surface;

	if(!isz(angle) || !is1(size))
	{
		if(!rotozoom_loaded && !LoadRotoZoomSurface()) return;
		if(sdl_rotozoom) src = sdl_rotozoom;
	}

	if(!src) return;

	SDL_Surface *hit = GetHitSurface(src);

	//Notnagel: lieber das gewoehnliche Bild als gar keines
	if(!hit)
	{
		BlitSurface(srcrect, dstrect, dst);
		return;
	}

	zod_hit_blits++;

	if(dst)
		dst->BlitOnToMe(srcrect, dstrect, hit);
	else
		SDL_BlitSurface(hit, srcrect, screen, dstrect);
}

/* Wie oft wird ein Sichttest gemacht, und wie oft ist etwas sichtbar?
 *
 * Offene Frage, die das entscheidet: `objekte` kostet auf der V1200 1447 us,
 * und die Schleife laeuft ueber ALLE Objekte, obwohl der Ausschnitt nur rund
 * 7 % der Karte zeigt. Zwei Schaetzungen standen gegeneinander -- 186 us und
 * bis zu 1200 us -- und beide waren ungemessen. Das Verhaeltnis dieser beiden
 * Zaehler sagt, wie viel ueberhaupt zu holen waere, OHNE etwas zu aendern.
 *
 * Wichtig: Ein Objekt macht mehrere Sichttests (ein Stein drei, ein Fahrzeug
 * Koerper plus Aufbau plus Luke) -- deshalb wird hier der TEST gezaehlt, nicht
 * das Objekt. */
unsigned long zod_sicht_tests = 0;
unsigned long zod_sicht_treffer = 0;

/* Wie oft hat das KARTENFENSTER (nicht der Ausschnitt) etwas abgeschnitten?
 *
 * Ohne diese Zahl ist nicht zu unterscheiden, ob der Zuschnitt auf die Karte
 * greift oder ob ihn nur niemand setzt -- das waere still, und die Voegel
 * flogen wieder in den schwarzen Rand. Genau dieselbe Lehre wie bei
 * `zeichnen.Objektlisten`: eine Grenze, die nichts aussortiert, sieht aus wie
 * eine, die es tut. Bei einer Karte, die den Ausschnitt ausfuellt, MUSS die
 * Zahl 0 sein. */
unsigned long zod_fenster_greift = 0;

extern "C" void zod_sicht_report(unsigned long *tests, unsigned long *treffer)
{
	*tests = zod_sicht_tests; *treffer = zod_sicht_treffer;
}

extern "C" unsigned long zod_fenster_report(void) { return zod_fenster_greift; }

int ZSDL_Surface::GetMapBlitInfo(SDL_Surface *src, int x, int y, SDL_Rect &from_rect, SDL_Rect &to_rect)
{
	if(!src) return 0;

	zod_sicht_tests++;

	// 	int full_width = (basic_info.width * 16);
	// 	int full_height = (basic_info.height * 16);

	//int view_w = screen_w - HUD_WIDTH;
	//int view_h = screen_h - HUD_HEIGHT;
	int view_w = screen_w - map_place_x;
	int view_h = screen_h - map_place_y;
	int shift_x = 0;
	int shift_y = 0;

	if(has_hud)
	{
		view_w -= HUD_WIDTH;
		view_h -= HUD_HEIGHT;
	}

	(void)shift_x; (void)shift_y;   /* x/y kommen bereits in Ausschnittlage */

	/* Zugeschnitten wird auf das KARTENFENSTER, nicht auf den Ausschnitt.
	 *
	 * Beides ist nur dann dasselbe, wenn die Karte den Ausschnitt ausfuellt.
	 * Ist sie schmaler oder niedriger, wird sie zentriert (ZMap::ClampShift)
	 * und links/rechts bzw. oben/unten bleibt schwarzer Rand -- Flaeche, zu
	 * der es keinen Kartenpunkt gibt. Der Nutzer hat auf der V1200 gesehen,
	 * was der alte Zuschnitt dort anrichtet: "Die Voegel fliegen in den
	 * schwarzen Rahmen rein und raus. Genauso Explosionen und Gegenstaende."
	 *
	 * Vorgabe (karte_w/h <= 0) ist der ganze Ausschnitt -- so bleibt der
	 * Kartenmodul-Bau unberuehrt, der SetMapWindow nie ruft. */
	int cx0 = 0, cy0 = 0, cx1 = view_w, cy1 = view_h;

	if(karte_w > 0)
	{
		if(karte_x > cx0) cx0 = karte_x;
		if(karte_x + karte_w < cx1) cx1 = karte_x + karte_w;
	}

	if(karte_h > 0)
	{
		if(karte_y > cy0) cy0 = karte_y;
		if(karte_y + karte_h < cy1) cy1 = karte_y + karte_h;
	}

	/* Schnittmenge aus Sprite und Fenster, in Ausschnittkoordinaten.
	 *
	 * Die Urfassung rechnete hier eine Breite aus, die im Regelfall NEGATIV
	 * war (`to_rect.x - view_w`), als Uint16 riesig wurde und sich darauf
	 * verliess, dass der Blitter sie auf `src->w` beschneidet. Das ging gut,
	 * war aber nicht zu erweitern: ein linker Rand laesst sich so nicht
	 * ausdruecken. Hier steht dieselbe Rechnung als gewoehnlicher
	 * Rechteckschnitt. */
	/* Nur zaehlen, wenn das Fenster ueberhaupt enger ist als der Ausschnitt
	 * -- bei einer Karte, die den Ausschnitt fuellt, kostet das nichts. */
	if(cx0 > 0 || cy0 > 0 || cx1 < view_w || cy1 < view_h)
		if(x < cx0 || y < cy0 || x + src->w > cx1 || y + src->h > cy1)
			zod_fenster_greift++;

	int x0 = x, y0 = y;
	int x1 = x + src->w, y1 = y + src->h;

	if(x0 < cx0) x0 = cx0;
	if(y0 < cy0) y0 = cy0;
	if(x1 > cx1) x1 = cx1;
	if(y1 > cy1) y1 = cy1;

	if(x1 <= x0 || y1 <= y0) return 0;

	from_rect.x = static_cast<Sint16>(x0 - x);
	from_rect.y = static_cast<Sint16>(y0 - y);
	from_rect.w = static_cast<Uint16>(x1 - x0);
	from_rect.h = static_cast<Uint16>(y1 - y0);

	to_rect.x = static_cast<Sint16>(x0);
	to_rect.y = static_cast<Sint16>(y0);
	to_rect.w = 0;
	to_rect.h = 0;

	zod_sicht_treffer++;

	return 1;
}

bool ZSDL_Surface::WillRenderOnScreen(int x, int y, bool about_center)
{
	if(!sdl_surface) return false;

	if(about_center)
	{
		x -= ((sdl_surface->w >> 1) * size);
		y -= ((sdl_surface->h >> 1) * size);
	}

	if(x > screen_w) return false;
	if(y > screen_h) return false;
	if(x + (sdl_surface->w * size) < 0) return false;
	if(y + (sdl_surface->h * size) < 0) return false;

	return true;
}
