/*
 * Der Sicht-Zuschnitt der Karte -- deterministisch geprueft.
 *
 * WARUM ES DIESEN TEST GIBT: `ZSDL_Surface::GetMapBlitInfo` ist beim Einbau
 * des Kartenfensters (schmale Karten werden zentriert) von einer
 * Vorzeichen-Bastelei auf einen gewoehnlichen Rechteckschnitt umgestellt
 * worden. Ob das im Regelfall dasselbe tut, laesst sich am BILD nicht
 * beantworten: Zwei Laeufe desselben Binaries unterscheiden sich bei 640x480
 * um rund 27 000 Bildpunkte (bewegte Einheiten), und genau so viel
 * unterscheiden sich auch alt und neu. Der Bildvergleich misst dort die
 * Spielsituation, nicht die Aenderung -- dieselbe Falle wie mehrfach
 * zuvor.
 *
 * Hier laufen beide Fassungen ueber denselben Parameterraum, und verglichen
 * wird der WIRKSAME Blit: das, was nach der Beschneidung durch den Blitter
 * (auf `src->w`/`src->h` und auf den Schirm) tatsaechlich gezeichnet wird.
 * Das ist der richtige Vergleichspunkt, denn die alte Fassung lieferte im
 * Regelfall absichtlich eine zu grosse Breite und verliess sich darauf, dass
 * der Blitter sie beschneidet.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef short Sint16;
typedef unsigned short Uint16;

struct SDL_Rect { Sint16 x, y; Uint16 w, h; };
struct Flaeche  { int w, h; };

/* ------------------------------------------------------------------ alt
 *
 * Wortgleich aus a561367 uebernommen (zsdl_opengl.cpp), nur mit view_w/view_h
 * als Parameter statt aus den statischen Feldern. Absichtlich NICHT
 * aufgeraeumt -- verglichen werden soll das Verhalten, nicht der Stil. */
static int alt_blit(const Flaeche *src, int x, int y,
                    SDL_Rect &from_rect, SDL_Rect &to_rect,
                    int view_w, int view_h)
{
	if(!src) return 0;

	int shift_x = 0;
	int shift_y = 0;

	if(x > shift_x + view_w) return 0;
	if(y > shift_y + view_h) return 0;
	if(x + src->w < shift_x) return 0;
	if(y + src->h < shift_y) return 0;

	to_rect.x = x - shift_x;
	to_rect.y = y - shift_y;
	to_rect.w = 0;
	to_rect.h = 0;

	from_rect.x = shift_x - x;
	from_rect.y = shift_y - y;

	if(to_rect.x + src->w > view_w)
		from_rect.w = view_w - to_rect.x;
	else
		from_rect.w = to_rect.x - view_w;

	if(to_rect.y + src->h > view_h)
		from_rect.h = view_h - to_rect.y;
	else
		from_rect.h = to_rect.y - view_h;

	if(from_rect.x < 0) from_rect.x = 0;
	if(from_rect.y < 0) from_rect.y = 0;

	if(from_rect.w > view_w) from_rect.w = view_w;
	if(from_rect.h > view_h) from_rect.h = view_h;

	to_rect.x += from_rect.x;
	to_rect.y += from_rect.y;

	return 1;
}

/* ------------------------------------------------------------------ neu
 *
 * Wortgleich aus zsdl_opengl.cpp, mit dem Kartenfenster als Parameter. */
static int neu_blit(const Flaeche *src, int x, int y,
                    SDL_Rect &from_rect, SDL_Rect &to_rect,
                    int view_w, int view_h,
                    int karte_x, int karte_y, int karte_w, int karte_h)
{
	if(!src) return 0;

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

	int x0 = x, y0 = y;
	int x1 = x + src->w, y1 = y + src->h;

	if(x0 < cx0) x0 = cx0;
	if(y0 < cy0) y0 = cy0;
	if(x1 > cx1) x1 = cx1;
	if(y1 > cy1) y1 = cy1;

	if(x1 <= x0 || y1 <= y0) return 0;

	from_rect.x = (Sint16)(x0 - x);
	from_rect.y = (Sint16)(y0 - y);
	from_rect.w = (Uint16)(x1 - x0);
	from_rect.h = (Uint16)(y1 - y0);

	to_rect.x = (Sint16)x0;
	to_rect.y = (Sint16)y0;
	to_rect.w = 0;
	to_rect.h = 0;

	return 1;
}

/* ------------------------------------------------- der wirksame Blit
 *
 * Was am Ende gezeichnet wird. SDL beschneidet das Quellrechteck auf die
 * Quellflaeche und das Ergebnis auf das Ziel; unser eigener Blitter
 * (port/amiga/sdl_video.cpp, clip_blit) macht dasselbe. Ohne diesen Schritt
 * waere der Vergleich falsch: Die alte Fassung liefert im Regelfall eine
 * Breite von bis zu view_w fuer ein 16 Punkte breites Sprite. */
struct Wirksam
{
	int leer;              /* nichts gezeichnet */
	int sx, sy, sw, sh;    /* Ausschnitt der Quelle */
	int dx, dy;            /* Lage im Ausschnitt */
};

static Wirksam wirksam(int ok, const Flaeche *src,
                       const SDL_Rect &from_rect, const SDL_Rect &to_rect,
                       int view_w, int view_h)
{
	Wirksam r;
	memset(&r, 0, sizeof(r));
	r.leer = 1;

	if(!ok) return r;

	int sx = from_rect.x, sy = from_rect.y;
	int sw = from_rect.w, sh = from_rect.h;
	int dx = to_rect.x,   dy = to_rect.y;

	/* auf die Quelle */
	if(sx < 0) { sw += sx; dx -= sx; sx = 0; }
	if(sy < 0) { sh += sy; dy -= sy; sy = 0; }
	if(sx + sw > src->w) sw = src->w - sx;
	if(sy + sh > src->h) sh = src->h - sy;

	/* auf das Ziel */
	if(dx < 0) { sw += dx; sx -= dx; dx = 0; }
	if(dy < 0) { sh += dy; sy -= dy; dy = 0; }
	if(dx + sw > view_w) sw = view_w - dx;
	if(dy + sh > view_h) sh = view_h - dy;

	if(sw <= 0 || sh <= 0) return r;

	r.leer = 0;
	r.sx = sx; r.sy = sy; r.sw = sw; r.sh = sh;
	r.dx = dx; r.dy = dy;
	return r;
}

static int gleich(const Wirksam &a, const Wirksam &b)
{
	if(a.leer && b.leer) return 1;
	if(a.leer != b.leer) return 0;
	return a.sx == b.sx && a.sy == b.sy && a.sw == b.sw && a.sh == b.sh
	    && a.dx == b.dx && a.dy == b.dy;
}

int main(void)
{
	/* Wie im Spiel: 640x480 abzueglich HUD (100 breit, 36 hoch). */
	const int view_w = 540, view_h = 444;

	static const int groessen[] = { 1, 2, 7, 16, 32, 45, 96, 270 };
	const int n_gr = (int)(sizeof(groessen) / sizeof(groessen[0]));

	long geprueft = 0, gezeichnet = 0, fehler = 0;

	/* --- Teil 1: ohne Kartenfenster muss neu wie alt zeichnen ---------- */
	for(int gi = 0; gi < n_gr; gi++)
	for(int gj = 0; gj < n_gr; gj++)
	{
		Flaeche src;
		src.w = groessen[gi];
		src.h = groessen[gj];

		/* Bis weit ueber beide Raender hinaus, damit alle Randfaelle
		 * drankommen -- auch der genaue Treffer auf die Kante. */
		for(int x = -src.w - 3; x <= view_w + 3; x++)
		for(int y = -src.h - 3; y <= view_h + 3; y += 7)
		{
			SDL_Rect fa, ta, fn, tn;
			int oa = alt_blit(&src, x, y, fa, ta, view_w, view_h);
			int on = neu_blit(&src, x, y, fn, tn, view_w, view_h, 0, 0, 0, 0);

			Wirksam wa = wirksam(oa, &src, fa, ta, view_w, view_h);
			Wirksam wn = wirksam(on, &src, fn, tn, view_w, view_h);

			geprueft++;
			if(!wa.leer) gezeichnet++;

			if(!gleich(wa, wn))
			{
				if(fehler < 10)
					printf("FEHLER: Flaeche %dx%d bei %d,%d\n"
					       "        alt  leer=%d quelle %d,%d %dx%d -> %d,%d\n"
					       "        neu  leer=%d quelle %d,%d %dx%d -> %d,%d\n",
					       src.w, src.h, x, y,
					       wa.leer, wa.sx, wa.sy, wa.sw, wa.sh, wa.dx, wa.dy,
					       wn.leer, wn.sx, wn.sy, wn.sw, wn.sh, wn.dx, wn.dy);
				fehler++;
			}
		}
	}

	printf("Teil 1: %ld Faelle, davon %ld mit Inhalt, %ld Abweichungen\n",
	       geprueft, gezeichnet, fehler);

	if(!gezeichnet)
	{
		printf("FEHLER: kein einziger Fall zeichnete etwas -- der Test prueft nichts\n");
		return 1;
	}

	/* --- Teil 2: mit Kartenfenster darf NICHTS daneben landen ---------- */
	/* 1280x720 abzueglich HUD ergibt 1180x684; eine 1024x1376-Karte wird
	 * waagerecht zentriert, also 78 Punkte Rand links und rechts. */
	const int vw = 1180, vh = 684;
	const int kx = 78, ky = 0, kw = 1024, kh = 684;

	long im_fenster = 0, daneben = 0, alt_daneben = 0;

	for(int gi = 0; gi < n_gr; gi++)
	{
		Flaeche src;
		src.w = groessen[gi];
		src.h = groessen[gi];

		for(int x = -src.w - 3; x <= vw + 3; x++)
		for(int y = -src.h - 3; y <= vh + 3; y += 11)
		{
			SDL_Rect fn, tn, fa, ta;

			int on = neu_blit(&src, x, y, fn, tn, vw, vh, kx, ky, kw, kh);
			Wirksam wn = wirksam(on, &src, fn, tn, vw, vh);

			if(!wn.leer)
			{
				im_fenster++;

				if(wn.dx < kx || wn.dx + wn.sw > kx + kw ||
				   wn.dy < ky || wn.dy + wn.sh > ky + kh)
				{
					if(daneben < 10)
						printf("FEHLER: %dx%d bei %d,%d zeichnet nach "
						       "%d,%d %dx%d -- ausserhalb der Karte\n",
						       src.w, src.h, x, y,
						       wn.dx, wn.dy, wn.sw, wn.sh);
					daneben++;
				}
			}

			/* Gegenprobe: die alte Fassung kannte das Fenster nicht. Wenn
			 * sie hier NICHT danebenzeichnet, prueft Teil 2 nichts. */
			int oa = alt_blit(&src, x, y, fa, ta, vw, vh);
			Wirksam wa = wirksam(oa, &src, fa, ta, vw, vh);

			if(!wa.leer &&
			   (wa.dx < kx || wa.dx + wa.sw > kx + kw ||
			    wa.dy < ky || wa.dy + wa.sh > ky + kh))
				alt_daneben++;
		}
	}

	printf("Teil 2: %ld Faelle mit Inhalt, %ld ausserhalb der Karte "
	       "(alte Fassung: %ld)\n", im_fenster, daneben, alt_daneben);

	if(!alt_daneben)
	{
		printf("FEHLER: die alte Fassung zeichnet nirgends daneben -- "
		       "Teil 2 kann den Fehler gar nicht finden\n");
		return 1;
	}

	if(fehler || daneben)
	{
		printf("DURCHGEFALLEN\n");
		return 1;
	}

	printf("BESTANDEN: Zuschnitt unveraendert ohne Kartenfenster, "
	       "und mit Fenster nichts im schwarzen Rand\n");
	return 0;
}
