#ifndef ZSDL_OPENGL_H
#define ZSDL_OPENGL_H


#include <QtGlobal>
// TODO Qt global
#ifdef Q_OS_WIN              //if windows
#include <windows.h>		 //win for Sleep(1000)
#endif

#ifndef DISABLE_OPENGL
#include <SDL/SDL_opengl.h>
#endif
#include <SDL/SDL.h>
#include <SDL/SDL_image.h>
#include "SDL_rotozoom.h"

#include <string>

#include "qzod_dnseparate_global.h"

#include "constants.h"
#include "common.h"

using namespace std;
using namespace COMMON;



#ifdef __cplusplus
extern "C" {
#endif

class QZOD_DNSEPARATESHARED_EXPORT ZSDL_Surface
{
public:
	ZSDL_Surface();
	~ZSDL_Surface();

	static void SetUseOpenGL(bool use_opengl_);
	static void SetMainSoftwareSurface(SDL_Surface *screen_);
	static void SetScreenDimensions(int w_, int h_);
    static void ZSDL_FillRect(SDL_Rect *dstrect, Uint8 r, Uint8 g, Uint8 b, ZSDL_Surface *dst = nullptr);
	/* Viele kleine Flaechen einer Farbe in einem Rutsch: Farbe einmal
	 * umrechnen, Schirm einmal sperren, Pixel direkt schreiben. Ein
	 * SDL_FillRect je 2x2-Punkt war auf dem Amiga zu teuer (Wegpunktlinien). */
	static void ZSDL_FillDots(const SDL_Rect *dots, int count, Uint8 r, Uint8 g, Uint8 b);
	static void SetMapPlace(int x, int y);
	static void SetHasHud(bool has_hud_);
	/* Das Stueck des Ausschnitts, in dem wirklich Karte liegt -- in
	 * Ausschnittkoordinaten, also derselbe Raum wie die x/y von
	 * GetMapBlitInfo. Ist die Karte kleiner als der Ausschnitt, wird sie
	 * zentriert und links/oben bleibt schwarzer Rand; ohne diese Angabe
	 * zeichneten Voegel, Truemmer und Gegenstaende dort hinein.
	 * w oder h <= 0 heisst "ganzer Ausschnitt" (Vorgabe, Kartenmodul). */
	static void SetMapWindow(int x, int y, int w, int h);
	static void GetScreenDimensions(int &w_, int &h_) { w_ = screen_w; h_ = screen_h; }

	/* ---- NoScaler (-N) -------------------------------------------------
	 *
	 * Das Original von 1996 hatte keinen Skalierer: Einheiten und Fahrzeuge
	 * liegen auf der CD in 8 festen Richtungen, Truemmer als BILDFOLGE
	 * (rock_effects 256 Bilder, bridge_effects 60), durchweg 16x16. Das
	 * Wachsen und das freie Drehen sind Nighsofts Zutat -- die Bildfolgen
	 * benutzt die Engine ohnehin schon (ERockParticle 8/8/18 Bilder,
	 * ERockTurrent/EBridgeTurrent/ETurrentMissile je 12, ERobotTurrent 33).
	 *
	 * Mit NoScaler bleibt die Groesse auf 1.0. Jede REINE Skalierung nimmt
	 * damit den Pfad ohne rotozoom.
	 *
	 * Was bleibt (bewusst, siehe unten): das einmalige Drehen der Geschosse
	 * und der Voegel. Fuer die laeuft rotozoomSurface weiterhin, also wird
	 * auch der Drehflaechenspeicher weiter benutzt -- nur eben einmal je
	 * Geschoss statt in jedem Bild je Truemmerteil.
	 *
	 * NICHT abgeschaltet wird das EINMALIGE Drehen der Geschosse: dort ist
	 * der Winkel die FLUGRICHTUNG (angle = 359 - AngleFromLoc(dx,dy)), und
	 * ELightRocket/EMissileCRockets/EMoMissileRockets haben nur ein
	 * Einzelbild von 7x4 bzw. 10x8 Punkten. Ohne Drehung zeigte jede Rakete
	 * in dieselbe Richtung, egal wohin sie fliegt. Das kostet ein rotozoom
	 * je Geschoss (80 Punkte), waehrend das je Bild neu gedrehte Truemmer-
	 * teil Hunderte kostet. Die vier je Bild drehenden Klassen lassen ihren
	 * SetAngle-Aufruf deshalb selbst weg. */
	static void SetNoScaler(bool on) { no_scaler = on; }
	static bool NoScaler() { return no_scaler; }

	/* ---- NoSpin: das je Bild neue DREHEN der Truemmer, ohne sonst etwas
	 *
	 * `-N` schaltet drei Dinge auf einmal ab: Drehen, Wachsen UND den
	 * Flugbogen (ArcFactor). Der Einbruch kommt aber nachweislich allein vom
	 * Drehen der grossen Flaechen -- gemessen auf der V1200 am 19.09.:
	 *
	 *     Schlimmstes Bild: 187660 us, davon effekte 167393 us
	 *     bei nur 49 Effekten und 22 DREHUNGEN
	 *
	 * Also 7,6 ms je Drehung. Ein 32x32-Teil bei Groesse 5-6 wird zu einem
	 * Huellrechteck von rund 272x272 = 74 000 Bildpunkten, und die innere
	 * Schleife von transformSurfaceY braucht 11 Befehle je Punkt. Das
	 * Histogramm bestaetigt die Seltenheit UND die Wucht: `4-5:95 5-6:16`.
	 *
	 * Ohne Drehung faellt beides weg: das Huellrechteck (Faktor 1,64 mehr
	 * Punkte als noetig, E[(|cos|+|sin|)^2]) und die teure Schleife --
	 * zoomSurfaceY braucht 4 statt 11 Befehle je Punkt und keine Division je
	 * Zeile. Zusammen rund 4,5-fach.
	 *
	 * Sichtbar: Die Truemmer taumeln dann ueber ihre 12 Einzelbilder, statt
	 * frei zu rotieren -- das Verhalten der Original-CD. Flugbogen und
	 * Wachsen BLEIBEN, anders als bei -N.
	 *
	 * Vorgabe auf dem Amiga: an (also nicht drehen). `SetEnv ZOD_SPIN ein`
	 * schaltet das freie Drehen zurueck, fuer den A/B-Vergleich in derselben
	 * Binaerdatei. Geschosse und Voegel sind NICHT betroffen: die drehen
	 * einmal, und dort ist der Winkel die Flugrichtung. */
	static void SetNoSpin(bool on) { no_spin = on; }
	static bool NoSpin() { return no_spin; }

	/* Faktor auf den y-Versatz, den die Effektklassen aus der Groesse
	 * ableiten (die Flughoehe). 0 = flach. Bewusst EINE Konstante: so
	 * laesst sich "ganz aus" gegen "flacher" (z. B. 0.33f) vergleichen,
	 * ohne den Code anzufassen. */
	static float ArcFactor() { return no_scaler ? 0.0f : 1.0f; }

	static int GetMapBlitInfo(SDL_Surface *src, int x, int y, SDL_Rect &from_rect, SDL_Rect &to_rect);

	void Unload();
	//alle gedrehten Fassungen verwerfen (Kartenwechsel)
	static void DropAllRotoZoom();
	void LoadBaseImage(string filename);
	void LoadBaseImage(SDL_Surface *sdl_surface_, bool delete_surface = true);
	//opaque=true: Flaeche ohne Alpha, im Bildschirmformat. Fuer deckende
	//Flaechen wie die Karte -- spart die Alpha-Mischung bei jedem Blit und
	//auf 16-Bit-Schirmen die Haelfte des Speichers.
	void LoadNewSurface(int w, int h, bool opaque = false);
	void UseDisplayFormat();
	void MakeAlphable();
	SDL_Surface *GetBaseSurface();

	void SetSize(float size_);
	void SetAngle(float angle_);
    void SetAlpha(uint8_t  alpha_); // !!TODO !! char alpha_);

	void RenderSurface(int x, int y, bool render_hit = false, bool about_center = false);
	void RenderSurfaceHorzRepeat(int x, int y, int w_total, bool render_hit = false);
	void RenderSurfaceVertRepeat(int x, int y, int h_total, bool render_hit = false);
	void RenderSurfaceAreaRepeat(int x, int y, int w, int h, bool render_hit = false);
    void BlitSurface(SDL_Rect *srcrect, SDL_Rect *dstrect, ZSDL_Surface *dst = nullptr);
	void BlitSurface(int fx, int fy, int fw, int fh, ZSDL_Surface *dst, int x, int y);
	void BlitSurface(ZSDL_Surface *dst, int x, int y);
    void BlitHitSurface(SDL_Rect *srcrect, SDL_Rect *dstrect, ZSDL_Surface *dst = nullptr, bool render_hit = false);

	void BlitOnToMe(SDL_Rect *srcrect, SDL_Rect *dstrect, SDL_Surface *src);
    void FillRectOnToMe(SDL_Rect *dstrect, Uint8 r, Uint8 g, Uint8 b);

	bool WillRenderOnScreen(int x, int y, bool about_center);

    // TODO!!! ZSDL_ModifyBlack in zsdl.h, zsdl.h include zsdl-opengl!
    void SDL_ModifyBlack(SDL_Surface *surface);

	//operator overloads
	ZSDL_Surface& operator=(const ZSDL_Surface &rhs);
	ZSDL_Surface& operator=(SDL_Surface *rhs);
private:
	static bool use_opengl;
	static SDL_Surface *screen;
	static int screen_w;
	static int screen_h;
	static int map_place_x;
	static int map_place_y;
	/* Kartenfenster im Ausschnitt (siehe SetMapWindow). */
	static int karte_x;
	static int karte_y;
	static int karte_w;
	static int karte_h;
	static bool has_hud;
	static bool no_scaler;
	static bool no_spin;

	bool LoadGLtexture();
	bool LoadRotoZoomSurface();

	//weisse Silhouette fuer das Treffer-Aufblitzen, einmal je Bild erzeugt
	SDL_Surface *GetHitSurface(SDL_Surface *src);

	//Drehflaeche verwerfen. IMMER hierueber, nie SDL_FreeSurface von Hand:
	//die Silhouette merkt sich ihre Quelle als Zeiger, und eine neue Flaeche
	//kann dieselbe Adresse bekommen.
	void DropRotoZoom();

	string image_filename;
	SDL_Surface *sdl_surface;

	/* Zwischenspeicher fuer gedrehte/skalierte Fassungen.
	 *
	 * Frueher hielt die Klasse genau EINE. Die Effektklassen teilen sich aber
	 * statische Flaechen (ETurrentMissile::fort_building_piece,
	 * ESideExplosion::normal_img, ERockParticle::debri_*), und jedes
	 * Truemmerteil hat eigenen Winkel und eigene Groesse. Ein sterbendes Fort
	 * wirft 16-22 Teile und 12-18 Feuerbaelle aus -- die eine Flaeche wurde
	 * damit in JEDEM Bild dutzendfach freigegeben und neu berechnet. Genau das
	 * war der Einbruch beim Explodieren eines Gebaeudes. */
	enum { ZOD_ROTO_CACHE = 8 };

	struct roto_entry
	{
		SDL_Surface *surf;
		float angle;
		float size;
		unsigned int stamp;   //Verdraengung: kleinster Stempel fliegt zuerst
	};

	roto_entry roto[ZOD_ROTO_CACHE];
	unsigned int roto_clock;

	/* Die 8 Fassungen je Flaeche begrenzen die SUMME nicht: Beim Rundenende
	 * stirbt alles gleichzeitig, und jede Explosion erzeugt neue (Winkel,
	 * Groesse)-Paare. Gemessen auf der V2 und im Emulator: der Speicher
	 * waechst dort von 0,9 auf 19,3 MB (968 Flaechen) und wird nie wieder
	 * frei -- nach zwei Runden reicht der Speicher fuer die naechste Karte
	 * nicht mehr, der Hintergrund fehlt und alles schmiert.
	 * Deshalb eine Obergrenze ueber ALLE Flaechen mit Verdraengung des am
	 * laengsten ungenutzten Eintrags. */
	static void RotoRegister(ZSDL_Surface *s);
	static void RotoUnregister(ZSDL_Surface *s);
	static void RotoEnforceBudget();
	bool RotoHasEntries() const;

	//aktuell ausgewaehlter Eintrag; der uebrige Code benutzt weiter diesen Zeiger
	SDL_Surface *sdl_rotozoom;
	//sdl_hit gehoert zu hit_src; wechselt die Quelle (z. B. neue Drehung),
	//wird sie neu erzeugt. Nur Zeigervergleich, nie dereferenziert.
	SDL_Surface *sdl_hit;
	SDL_Surface *hit_src;
#ifndef DISABLE_OPENGL
	GLuint gl_texture;
#endif
	bool gl_texture_loaded;
	bool rotozoom_loaded;

	float size, angle;
    // TODO !!  change type, char alpha;
    uint8_t alpha;
};

void InitOpenGL();
void ResetOpenGLViewPort(int width, int height);
inline void ZSDL_FillRect(SDL_Rect *dstrect, Uint8 r, Uint8 g, Uint8 b, ZSDL_Surface *dst = nullptr)
	{ ZSDL_Surface::ZSDL_FillRect(dstrect, r, g, b, dst); }
inline void ZSDL_FillDots(const SDL_Rect *dots, int count, Uint8 r, Uint8 g, Uint8 b)
	{ ZSDL_Surface::ZSDL_FillDots(dots, count, r, g, b); }


#ifdef __cplusplus
}
#endif

#endif
