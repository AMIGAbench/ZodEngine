/*  

  SDL_rotozoom.c - rotozoomer for 32bit or 8bit surfaces

  LGPL (c) A. Schiffler

*/

#ifdef WIN32
#include <windows.h>
#endif

//#include <stdlib.h>
//#include <string.h>
#include <cstdlib>
#include <string>

#include "SDL_rotozoom.h"
#include "fastcopy.h"   /* zod_copy_row -- memcpy ist hier ein CopyMem-Stummel */

/* Hinweis an den Amiga-Unterbau: die Flaechen, die HIER entstehen, sollen
 * ueber AllocVec belegt werden statt ueber libnix (siehe ZOD_ROTOMEM in
 * port/amiga/sdl_video.cpp). Der Unterbau entscheidet selbst, ob er dem
 * Hinweis folgt -- ohne den Schalter tut er nichts.
 *
 * Als Waechter mit Zaehler, damit ein zweiter Aufruf innerhalb des ersten
 * ihn nicht vorzeitig abschaltet. */
extern "C" void zod_surface_big_hint(int on);

#ifndef __amigaos__
/* Attrappe fuer den Host -- dort gibt es den Amiga-Unterbau nicht.
 *
 * SCHWACH, und das ist noetig: `make shimtest` uebersetzt
 * port/amiga/sdl_video.cpp AUF DEM HOST, dort gibt es die echte Fassung also
 * sehr wohl. Ein `#ifndef __amigaos__` allein waere eine Doppeldefinition --
 * und genau das hat das Pruefmittel schon einmal lautlos zerlegt. */
extern "C" __attribute__((weak)) void zod_surface_big_hint(int) { }
#endif

namespace {
struct ZodBigHint
{
	ZodBigHint()  { zod_surface_big_hint(1); }
	~ZodBigHint() { zod_surface_big_hint(0); }
};
}

/* Schutzzeilen am Ende der Zielflaeche.
 *
 * Das Original von SDL_gfx belegt hier "dstheight + GUARD_ROWS" Zeilen und
 * setzt danach dst->h wieder auf dstheight. Der Grund steht dort nicht dabei,
 * ist aber handfest: transformSurfaceRGBA/Y laufen mit Festkommaschritten
 * (16.16) ueber das Ziel und schreiben rundungsbedingt bis zu zwei Zeilen
 * ueber die letzte hinaus.
 *
 * In dieser Fassung waren die Schutzzeilen verlorengegangen. Unter SDL und
 * glibc fiel das nie auf -- deren Speicherverwaltung hat hinter jedem Block
 * Luft. Auf AmigaOS liegt dort die naechste Belegung, und exec meldete
 * ausgerechnet beim Laden der Grafiken "Guru 81000005" (AN_MemCorrupt).
 * Gefunden mit tests/shim unter AddressSanitizer:
 *   WRITE of size 4 ... 0 bytes after 40-byte region
 *   transformSurfaceRGBA ... SDL_rotozoom.cpp:591
 */
#define ZOD_GUARD_ROWS (2)

#define MAX(a,b)    (((a) > (b)) ? (a) : (b))


/* 
 
 32bit integer-factor averaging Shrinker

 Shrinks 32bit RGBA/ABGR 'src' surface to 'dst' surface.
 
*/

int shrinkSurfaceRGBA(SDL_Surface * src, SDL_Surface * dst, int factorx, int factory)
{
    int x, y, dx, dy, dgap, ra, ga, ba, aa; // TODO sgap,
    int n_average;
    tColorRGBA *sp, *osp, *oosp;
    tColorRGBA *dp;

    /*
     * Averaging integer shrink
     */

    /* Precalculate division factor */
    n_average = factorx*factory;
   
    /*
     * Scan destination
     */
    sp = static_cast<tColorRGBA*>(src->pixels);
//    sgap = src->pitch - src->w * 4;

    dp = static_cast<tColorRGBA*>(dst->pixels);
    dgap = dst->pitch - dst->w * 4;

    for (y = 0; y < dst->h; y++) {

      osp=sp;
      for (x = 0; x < dst->w; x++) {

        /* Trace out source box and accumulate */
        oosp=sp;
        ra=ga=ba=aa=0;
        for (dy=0; dy < factory; dy++) {
         for (dx=0; dx < factorx; dx++) {
          ra += sp->r;
          ga += sp->g;
          ba += sp->b;
          aa += sp->a;
          
          sp++;
         } 
         /* src dx loop */
         sp = (tColorRGBA *)((Uint8*)sp + (src->pitch - 4*factorx)); // next y
        }
        /* src dy loop */

        /* next box-x */
        sp = (tColorRGBA *)((Uint8*)oosp + 4*factorx);
                
        /* Store result in destination */
        dp->r = ra/n_average;
        dp->g = ga/n_average;
        dp->b = ba/n_average;
        dp->a = aa/n_average;
                 
        /*
         * Advance destination pointer 
         */
         dp++;
        } 
        /* dst x loop */

        /* next box-y */
        sp = (tColorRGBA *)((Uint8*)osp + src->pitch*factory);

        /*
         * Advance destination pointers 
         */
        dp = (tColorRGBA *) ((Uint8 *) dp + dgap);
      } 
      /* dst y loop */

    return (0);
}

/* 
 
 8bit integer-factor averaging Shrinker

 Shrinks 8bit Y 'src' surface to 'dst' surface.
 
*/

int shrinkSurfaceY(SDL_Surface * src, SDL_Surface * dst, int factorx, int factory)
{
    int x, y, dx, dy,  dgap, a;  //sgap, TODO
    int n_average;
    Uint8 *sp, *osp, *oosp;
    Uint8 *dp;

    /*
     * Averaging integer shrink
     */

    /* Precalculate division factor */
    n_average = factorx*factory;
   
    /*
     * Scan destination
     */
    sp = (Uint8 *) src->pixels;
//    sgap = src->pitch - src->w;

    dp = (Uint8 *) dst->pixels;
    dgap = dst->pitch - dst->w;
    
    for (y = 0; y < dst->h; y++) {    

      osp=sp;
      for (x = 0; x < dst->w; x++) {

        /* Trace out source box and accumulate */
        oosp=sp;
        a=0;
        for (dy=0; dy < factory; dy++) {
         for (dx=0; dx < factorx; dx++) {
          a += (*sp);
          /* next x */           
          sp++;
         } 
         /* end src dx loop */         
         /* next y */
         sp = (Uint8 *)((Uint8*)sp + (src->pitch - factorx)); 
        } 
        /* end src dy loop */
        
        /* next box-x */
        sp = (Uint8 *)((Uint8*)oosp + factorx);
                
        /* Store result in destination */
        *dp = a/n_average;

        /*
         * Advance destination pointer 
         */
         dp++;
        } 
        /* end dst x loop */

        /* next box-y */
        sp = (Uint8 *)((Uint8*)osp + src->pitch*factory);

        /*
         * Advance destination pointers 
         */
        dp = (Uint8 *)((Uint8 *)dp + dgap);
      } 
      /* end dst y loop */

    return (0);
}

/* 
 
 32bit Zoomer with optional anti-aliasing by bilinear interpolation.

 Zoomes 32bit RGBA/ABGR 'src' surface to 'dst' surface.
 
*/

int zoomSurfaceRGBA(SDL_Surface * src, SDL_Surface * dst, int flipx, int flipy, int smooth)
{
    int x, y, sx, sy, *sax, *say, *csax, *csay, csx, csy, ex, ey, t1, t2, sstep, lx, ly;
    tColorRGBA *c00, *c01, *c10, *c11, *cswap;
    tColorRGBA *sp, *csp, *dp;
    int dgap;

    /*
     * Variable setup 
     */
    if (smooth) {
	/*
	 * For interpolation: assume source dimension is one pixel 
	 */
	/*
	 * smaller to avoid overflow on right and bottom edge.     
	 */
    sx = static_cast<int>(65536.0f * static_cast<float>(src->w - 1) / dst->w);
    sy = static_cast<int>(65536.0f * static_cast<float>(src->h - 1) / dst->h);
    } else {
    sx = static_cast<int>(65536.0f * static_cast<float>(src->w) / dst->w);
    sy = static_cast<int>(65536.0f * static_cast<float>(src->h) / dst->h);
    }

    /*
     * Allocate memory for row increments 
     */
    if ((sax = static_cast<int*>(malloc((dst->w + 1) * sizeof(Uint32))) ) == nullptr) {
	return (-1);
    }
    if ((say = static_cast<int*>(malloc((dst->h + 1) * sizeof(Uint32)))) == nullptr) {
	free(sax);
	return (-1);
    }

    /*
     * Precalculate row increments 
     */
    sp = csp = (tColorRGBA *) src->pixels;
    dp = (tColorRGBA *) dst->pixels;

    if (flipx) csp += (src->w-1);
    if (flipy) csp += (src->pitch*(src->h-1));

    csx = 0;
    csax = sax;
    for (x = 0; x <= dst->w; x++) {
	*csax = csx;
	csax++;
	csx &= 0xffff;
	csx += sx;
    }
    csy = 0;
    csay = say;
    for (y = 0; y <= dst->h; y++) {
	*csay = csy;
	csay++;
	csy &= 0xffff;
	csy += sy;
    }

    dgap = dst->pitch - dst->w * 4;

    /*
     * Switch between interpolating and non-interpolating code 
     */
    if (smooth) {

	/*
	 * Interpolating Zoom 
	 */

	/*
	 * Scan destination 
	 */
	ly = 0;
	csay = say;
	for (y = 0; y < dst->h; y++) {
            /*
             * Setup color source pointers 
             */
            c00 = csp;	    
            c01 = csp;
            c01++;	    
            c10 = (tColorRGBA *) ((Uint8 *) csp + src->pitch);
            c11 = c10;
            c11++;
            csax = sax;
            if (flipx) {
	     cswap = c00; c00=c01; c01=cswap;
	     cswap = c10; c10=c11; c11=cswap;
            }
            if (flipy) {
	     cswap = c00; c00=c10; c10=cswap;
	     cswap = c01; c01=c11; c11=cswap;
            }
            lx = 0;
	    for (x = 0; x < dst->w; x++) {
		/*
		 * Interpolate colors 
		 */
		ex = (*csax & 0xffff);
		ey = (*csay & 0xffff);
		t1 = ((((c01->r - c00->r) * ex) >> 16) + c00->r) & 0xff;
		t2 = ((((c11->r - c10->r) * ex) >> 16) + c10->r) & 0xff;
		dp->r = (((t2 - t1) * ey) >> 16) + t1;
		t1 = ((((c01->g - c00->g) * ex) >> 16) + c00->g) & 0xff;
		t2 = ((((c11->g - c10->g) * ex) >> 16) + c10->g) & 0xff;
		dp->g = (((t2 - t1) * ey) >> 16) + t1;
		t1 = ((((c01->b - c00->b) * ex) >> 16) + c00->b) & 0xff;
		t2 = ((((c11->b - c10->b) * ex) >> 16) + c10->b) & 0xff;
		dp->b = (((t2 - t1) * ey) >> 16) + t1;
		t1 = ((((c01->a - c00->a) * ex) >> 16) + c00->a) & 0xff;
		t2 = ((((c11->a - c10->a) * ex) >> 16) + c10->a) & 0xff;
		dp->a = (((t2 - t1) * ey) >> 16) + t1;

		/*
		 * Advance source pointers 
		 */
		csax++;
		sstep = (*csax >> 16);
		lx += sstep;
		if (lx >= src->w) sstep = 0;
		if (flipx) sstep = -sstep;
		c00 += sstep;
		c01 += sstep;
		c10 += sstep;
		c11 += sstep;
		/*
		 * Advance destination pointer 
		 */
		dp++;
	    }
	    /*
	     * Advance source pointer 
	     */
	    csay++;
	    sstep = (*csay >> 16);
            ly += sstep;
            if (ly >= src->h) sstep = 0;
            sstep *= src->pitch;
	    if (flipy) sstep = -sstep;
	    csp = (tColorRGBA *) ((Uint8 *) csp + sstep);

	    /*
	     * Advance destination pointers 
	     */
	    dp = (tColorRGBA *) ((Uint8 *) dp + dgap);
	}
    } else {

	/*
	 * Non-Interpolating Zoom 
	 */

	csay = say;
	for (y = 0; y < dst->h; y++) {
	    sp = csp;
	    csax = sax;
	    for (x = 0; x < dst->w; x++) {
		/*
		 * Draw 
		 */
		*dp = *sp;
		/*
		 * Advance source pointers 
		 */
		csax++;
		sstep = (*csax >> 16);
		if (flipx) sstep = -sstep;
		sp += sstep;
		/*
		 * Advance destination pointer 
		 */
		dp++;
	    }
	    /*
	     * Advance source pointer 
	     */
	    csay++;
	    sstep = (*csay >> 16) * src->pitch;
	    if (flipy) sstep = -sstep;
	    csp = (tColorRGBA *) ((Uint8 *) csp + sstep);

	    /*
	     * Advance destination pointers 
	     */
	    dp = (tColorRGBA *) ((Uint8 *) dp + dgap);
	}
    }

    /*
     * Remove temp arrays 
     */
    free(sax);
    free(say);

    return (0);
}

/* 
 
 8bit Zoomer without smoothing.

 Zoomes 8bit palette/Y 'src' surface to 'dst' surface.
 
*/

int zoomSurfaceY(SDL_Surface * src, SDL_Surface * dst, int flipx, int flipy)
{
    Uint32 x, y, *sax, *say, *csax, *csay; //sx, sy,
    int csx, csy;
    Uint8 *sp, *dp, *csp;
    int dgap;

    /* Schrittweitentabellen: fuer Sprites (bis 256 Punkte je Kante) auf dem
     * Stapel. Zwei malloc/free JE AUFRUF sind auf AmigaOS teuer -- libnix gibt
     * Freigegebenes nicht ans System zurueck und fragmentiert dabei -- genau
     * daran lag das Schlieren ab der 3./4. Karte. Bei groesseren Flaechen
     * bleibt es beim malloc. */
    enum { ZOD_ZOOM_STACK = 257 };
    Uint32 sax_stack[ZOD_ZOOM_STACK];
    Uint32 say_stack[ZOD_ZOOM_STACK];
    int sax_heap = 0, say_heap = 0;

    if (dst->w + 1 <= ZOD_ZOOM_STACK) {
	sax = sax_stack;
    } else if ((sax = (Uint32 *) malloc((dst->w + 1) * sizeof(Uint32))) == nullptr) {
	return (-1);
    } else {
	sax_heap = 1;
    }

    if (dst->h + 1 <= ZOD_ZOOM_STACK) {
	say = say_stack;
    } else if ((say = (Uint32 *) malloc((dst->h + 1) * sizeof(Uint32))) == nullptr) {
	if (sax_heap) free(sax);
	return (-1);
    } else {
	say_heap = 1;
    }

    /*
     * Pointer setup 
     */
    sp = csp = (Uint8 *) src->pixels;
    dp = (Uint8 *) dst->pixels;
    dgap = dst->pitch - dst->w;

    if (flipx) csp += (src->w-1);
    if (flipy) csp  = ( (Uint8*)csp + src->pitch*(src->h-1) );

    /*
     * Precalculate row increments 
     */
    csx = 0;
    csax = sax;
    for (x = 0; x < dst->w; x++) {
        csx += src->w;
        *csax = 0;
        while (csx >= dst->w) {
            csx -= dst->w;
            (*csax)++;
        }
        csax++;
    }
    csy = 0;
    csay = say;
    for (y = 0; y < dst->h; y++) {
	csy += src->h;
	*csay = 0;
	while (csy >= dst->h) {
	  csy -= dst->h;
	  (*csay)++;
	}
	csay++;
    }


    /*
     * Draw 
     */
    /* Bei VERGROESSERUNG sind die meisten Zielzeilen Duplikate.
     *
     * `say[y]` ist der Schritt der QUELLzeile. Ist er 0, steht die Quellzeile
     * still -- die naechste Zielzeile entsteht dann aus genau denselben
     * Quellbytes mit genau derselben Schrittweitentabelle `sax` und ist damit
     * byteidentisch zur eben geschriebenen. Sie laesst sich am Stueck
     * kopieren, statt sie Punkt fuer Punkt neu zu rechnen.
     *
     * Bei Groesse 6 werden aus 45 Quellzeilen 270 Zielzeilen: 5 von 6 sind
     * Duplikate. Die teure Schleife (rund 4 Befehle je Punkt, mit
     * ungleichmaessig springender Quelladresse) laeuft dann nur noch fuer
     * jede sechste Zeile; der Rest ist eine langwortweise Zeilenkopie.
     *
     * NICHT `memcpy` benutzen: das ist in dieser Werkzeugkette ein Stummel um
     * CopyMem und faellt bei ungleicher Paritaet auf byteweises Kopieren
     * zurueck (siehe port/amiga/fastcopy.h).
     *
     * Bei VERKLEINERUNG ist `say[y]` nie 0, dann kostet das einen Vergleich
     * je Zeile und sonst nichts. */
    Uint8 *letzte_zeile = nullptr;   /* Anfang der zuletzt geschriebenen Zeile,
                                      * wenn die naechste ihr gleicht */

    csay = say;
    for (y = 0; y < dst->h; y++) {
	Uint8 *zeile = dp;

	if (letzte_zeile) {
	    zod_copy_row(dp, letzte_zeile, dst->w);
	    dp += dst->w;
	} else {
	    csax = sax;
	    sp = csp;
	    /* Die flipx-Abfrage stand JE BILDPUNKT in der Schleife, samt
	     * Multiplikation mit +-1 -- beides ist ueber die ganze Flaeche
	     * unveraenderlich. Zwei Schleifen statt einer. */
	    if (!flipx) {
		for (x = 0; x < dst->w; x++) {
		    *dp++ = *sp;
		    sp += *csax++;
		}
	    } else {
		for (x = 0; x < dst->w; x++) {
		    *dp++ = *sp;
		    sp -= *csax++;
		}
	    }
	}

	/* Steht die Quellzeile still, ist die naechste Zielzeile dieselbe. */
	letzte_zeile = (*csay == 0) ? zeile : nullptr;

	/*
	 * Advance source pointer (for row) 
	 */
	csp += ((*csay) * src->pitch) * (flipy ? -1 : 1);
	csay++;

	/*
	 * Advance destination pointers 
	 */
	dp += dgap;
    }

    /*
     * Remove temp arrays 
     */
    if (sax_heap) free(sax);
    if (say_heap) free(say);

    return (0);
}

/* 
 
 32bit Rotozoomer with optional anti-aliasing by bilinear interpolation.

 Rotates and zoomes 32bit RGBA/ABGR 'src' surface to 'dst' surface.
 
*/

void transformSurfaceRGBA(SDL_Surface * src, SDL_Surface * dst, int cx, int cy, int isin, int icos, int flipx, int flipy, int smooth)
{
    int x, y, t1, t2, dx, dy, xd, yd, sdx, sdy, ax, ay, ex, ey, sw, sh;
    tColorRGBA c00, c01, c10, c11, cswap;
    tColorRGBA *pc, *sp;
    int gap;

    /*
     * Variable setup 
     */
    xd = ((src->w - dst->w) << 15);
    yd = ((src->h - dst->h) << 15);
    ax = (cx << 16) - (icos * cx);
    ay = (cy << 16) - (isin * cx);
    sw = src->w - 1;
    sh = src->h - 1;
    pc = (tColorRGBA*)dst->pixels;
    gap = dst->pitch - dst->w * 4;

    /*
     * Switch between interpolating and non-interpolating code 
     */
    if (smooth) {
	for (y = 0; y < dst->h; y++) {
	    dy = cy - y;
	    sdx = (ax + (isin * dy)) + xd;
	    sdy = (ay - (icos * dy)) + yd;
	    for (x = 0; x < dst->w; x++) {
		dx = (sdx >> 16);
		dy = (sdy >> 16);
		if ((dx > -1) && (dy > -1) && (dx < src->w) && (dy < src->h)) {
  		    if (flipx) dx = sw - dx;
  		    if (flipy) dy = sh - dy;
                    sp = (tColorRGBA *) ((Uint8 *) src->pixels + src->pitch * dy);
                    sp += dx;
                    c00 = *sp;
                    sp += 1;
                    c01 = *sp;
                    sp = (tColorRGBA *) ((Uint8 *) sp + src->pitch);
                    c11 = *sp;
                    sp -= 1;
                    c10 = *sp;
                    if (flipx) {
                      cswap = c00; c00=c01; c01=cswap;
                      cswap = c10; c10=c11; c11=cswap;
                    }
                    if (flipy) {
                      cswap = c00; c00=c10; c10=cswap;
                      cswap = c01; c01=c11; c11=cswap;
                    }
		    /*
		     * Interpolate colors 
		     */
		    ex = (sdx & 0xffff);
		    ey = (sdy & 0xffff);
		    t1 = ((((c01.r - c00.r) * ex) >> 16) + c00.r) & 0xff;
		    t2 = ((((c11.r - c10.r) * ex) >> 16) + c10.r) & 0xff;
		    pc->r = (((t2 - t1) * ey) >> 16) + t1;
		    t1 = ((((c01.g - c00.g) * ex) >> 16) + c00.g) & 0xff;
		    t2 = ((((c11.g - c10.g) * ex) >> 16) + c10.g) & 0xff;
		    pc->g = (((t2 - t1) * ey) >> 16) + t1;
		    t1 = ((((c01.b - c00.b) * ex) >> 16) + c00.b) & 0xff;
		    t2 = ((((c11.b - c10.b) * ex) >> 16) + c10.b) & 0xff;
		    pc->b = (((t2 - t1) * ey) >> 16) + t1;
		    t1 = ((((c01.a - c00.a) * ex) >> 16) + c00.a) & 0xff;
		    t2 = ((((c11.a - c10.a) * ex) >> 16) + c10.a) & 0xff;
		    pc->a = (((t2 - t1) * ey) >> 16) + t1;
		}
		sdx += icos;
		sdy += isin;
		pc++;
	    }
	    pc = (tColorRGBA *) ((Uint8 *) pc + gap);
	}
    } else {
	for (y = 0; y < dst->h; y++) {
	    dy = cy - y;
	    sdx = (ax + (isin * dy)) + xd;
	    sdy = (ay - (icos * dy)) + yd;
	    for (x = 0; x < dst->w; x++) {
		dx = (short) (sdx >> 16);
		dy = (short) (sdy >> 16);
		if (flipx) dx = (src->w-1)-dx;
		if (flipy) dy = (src->h-1)-dy;
		if ((dx >= 0) && (dy >= 0) && (dx < src->w) && (dy < src->h)) {
		    sp = (tColorRGBA *) ((Uint8 *) src->pixels + src->pitch * dy);
		    sp += dx;
		    *pc = *sp;
		}
		sdx += icos;
		sdy += isin;
		pc++;
	    }
	    pc = (tColorRGBA *) ((Uint8 *) pc + gap);
	}
    }
}

/* 
 
 8bit Rotozoomer without smoothing

 Rotates and zoomes 8bit palette/Y 'src' surface to 'dst' surface.
 
*/

/* Aufgeraeumte Fassung der 8-Bit-Drehung.
 *
 * Anlass: Der Nutzer meldet Bildratenzusammenbruch bei grossen Explosionen.
 * Gemessen (tests/host/roto_bench.cpp) gehen 66-82 % eines Skalier-Aufrufs in
 * diese Schleife -- nicht in Belegung, Loeschen und Palettenkopie. Und AMMX
 * hilft hier NICHT: Der Lesezugriff ist ein Gather (jeder Zielpunkt kommt von
 * einer anderen, nicht fortlaufenden Quelladresse). Der Befehl, der das koennte
 * (TEX), braucht Texturen von mindestens 256x256 und ist laut Referenz in
 * heutigen Cores teilweise defekt; VPERM nimmt sein Muster nur als Sofortwert.
 *
 * Also wird die Schleife selbst billiger gemacht -- portabel, also auch auf
 * 040/060 wirksam. Die Urfassung machte je Bildpunkt rund 15 Operationen:
 *
 *   - zwei schleifeninvariante Abfragen (flipx, flipy) IN der Schleife,
 *   - vier Grenzvergleiche,
 *   - ein Neuladen von src->pixels (der Uebersetzer DARF es nicht heraushalten:
 *     der Schreibzugriff *pc koennte damit ueberlappen),
 *   - eine Multiplikation src->pitch * dy,
 *   - zwei short-Wandlungen.
 *
 * Geaendert:
 *   1. Alles Invariante in lokale Variablen (damit auch src->pixels).
 *   2. Zeilenanfaenge einmal je Aufruf in eine Tabelle -- die Multiplikation
 *      je Bildpunkt entfaellt.
 *   3. Je Zielzeile wird vorab der x-Bereich berechnet, in dem die Quellstelle
 *      ueberhaupt INNERHALB der Quellflaeche liegt. Beides sind lineare
 *      Funktionen von x, der gueltige Bereich ist also ein Intervall. Damit
 *      entfallen die vier Grenzvergleiche im Inneren, und die Punkte ausserhalb
 *      werden gar nicht erst angefasst (das memset hat dort schon den
 *      Farbschluessel hingelegt). Bei einer gedrehten Flaeche liegt rund die
 *      Haelfte der Zielpunkte ausserhalb.
 *   4. flipx/flipy stehen nur noch als Zweig VOR der Schleife.
 *
 * Verhalten unveraendert: tests/host/roto_bench.cpp vergleicht die Ausgabe
 * Byte fuer Byte mit der Urfassung ueber alle im Spiel vorkommenden Winkel und
 * Groessen.
 */

/* Abrunden bzw. Aufrunden bei ganzzahliger Division mit Vorzeichen. Die
 * C-Division schneidet gegen Null ab, das ist hier falsch.
 *
 * ACHTUNG, hier stand `long long`. Das war ueberaengstlich und hat richtig
 * weh getan: Auf m68k gibt es keine 64-Bit-Division in Hardware, gcc ruft
 * dafuer ___divdi3/___moddi3 auf (101 bzw. 170 Befehle). zod_spanne braucht
 * VIER solche Aufrufe, und transformSurfaceY ruft zod_spanne ZWEIMAL je
 * Zielzeile -- also acht Softwaredivisionen je Zeile, gegen rund zwoelf
 * Befehle je Bildpunkt. Im ganzen Bau war SDL_rotozoom.o danach das einzige
 * Objekt, das ___divdi3 ueberhaupt brauchte.
 *
 * 32 Bit reichen nachweislich: `grenze` ist hoechstens sw<<16 (bei 512 Punkten
 * Kantenlaenge rund 34 Mio.), `a` bleibt im Bereich einiger hundert Millionen
 * -- die Differenz passt mit grossem Abstand in einen int, und der Teiler ist
 * icos/isin, betragsmaessig hoechstens 65536. Damit wird daraus divs.l, auf
 * 68060/68080 ein Hardwarebefehl. */
static inline int zod_floordiv(int a, int b)
{
    int q = a / b;

    if((a % b) != 0 && ((a < 0) != (b < 0))) q--;

    return q;
}

static inline int zod_ceildiv(int a, int b)
{
    return -zod_floordiv(-a, b);
}

/* Der Bereich von x in [0,n), fuer den  a + x*s  in [0,grenze) liegt.
 * Rueckgabe 0 = leer. */
static int zod_spanne(int a, int s, int grenze, int n, int *von, int *bis)
{
    if(s == 0)
    {
        if(a < 0 || a >= grenze) return 0;

        *von = 0;
        *bis = n - 1;

        return 1;
    }

    int lo, hi;

    if(s > 0)
    {
        lo = zod_ceildiv(-a, s);
        hi = zod_ceildiv(grenze - a, s) - 1;
    }
    else
    {
        /* s < 0: aus  a + x*s < grenze  folgt  x > (grenze-a)/s  -- also
         * floor+1, NICHT ceil. Bei genau ganzzahligem Quotienten liefert ceil
         * einen Punkt zu viel, und der greift hinter die Zeilentabelle.
         * (Genau daran ist die erste Fassung abgestuerzt.) */
        lo = zod_floordiv(grenze - a, s) + 1;
        hi = zod_floordiv(-a, s);
    }

    if(lo < 0) lo = 0;
    if(hi > n - 1) hi = n - 1;

    if(lo > hi) return 0;

    *von = lo;
    *bis = hi;

    return 1;
}

/* `dst_ist_null` sagt zu, dass die Zielflaeche vollstaendig genullt ankommt.
 * Dann entfaellt das Fuellen mit dem Farbschluessel, sofern der 0 ist. */
void transformSurfaceY(SDL_Surface * src, SDL_Surface * dst, int cx, int cy, int isin, int icos, int flipx, int flipy, int dst_ist_null)
{
    const int sw = src->w;
    const int sh = src->h;
    const int spitch = src->pitch;
    const int dw = dst->w;
    const int dh = dst->h;
    const int dpitch = dst->pitch;
    const tColorY *spix = (const tColorY *)src->pixels;
    tColorY *dpix = (tColorY *)dst->pixels;

    const int xd = ((sw - dw) << 15);
    const int yd = ((sh - dh) << 15);
    const int ax = (cx << 16) - (icos * cx);
    const int ay = (cy << 16) - (isin * cx);

    /* Zielflaeche auf den Farbschluessel setzen -- alles ausserhalb der
     * Quellflaeche bleibt danach unangetastet.
     *
     * Entfaellt, wenn die Flaeche ohnehin genullt ankommt UND der Schluessel 0
     * ist. Beides trifft im Spiel zu: SDL_CreateRGBSurface nullt in BEIDEN
     * Zweigen (calloc bzw. memset nach zod_big_alloc, sdl_video.cpp), und die
     * gemeinsame Palette vergibt Platz 0 nur an durchsichtige Punkte.
     *
     * Es geht um viel: Bei einem gedrehten 64x64-Sprite ist das Ziel rund
     * 118x118 = 14 KB, also ~122 us je Drehung bei 114 MB/s. Beim Tod eines
     * Forts fallen Dutzende Drehungen in EINEM Bild an. */
    if(!(dst_ist_null && (src->format->colorkey & 0xff) == 0))
        memset(dpix, (unsigned char)(src->format->colorkey & 0xff), dpitch * dh);

    if(sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0) return;

    /* Zeilenanfaenge der Quelle: ersetzt die Multiplikation je Bildpunkt.
     *
     * Aus dem Stapel, solange die Quelle nicht zu hoch ist -- genau wie es
     * zoomSurfaceY daneben schon macht, und aus demselben Grund: Zwei
     * malloc/free JE AUFRUF sind auf AmigaOS teuer, und libnix gibt
     * Freigegebenes nicht ans System zurueck.
     *
     * Der Drehweg hatte den Rueckfall als einziger nicht -- und er ist nach
     * `-N` der einzige, der ueberhaupt noch laeuft. Beim Tod eines Forts
     * fallen Dutzende Drehungen in EINEM Bild an; der Nutzer sieht dort 6 fps.
     * Sprites sind 16 bis 64 Punkte hoch, der Stapelweg traegt also fast
     * immer. */
    enum { ZOD_ROTO_STACK = 256 };
    const tColorY *zeile_stapel[ZOD_ROTO_STACK];
    const tColorY **zeile;
    int zeile_halde = 0;

    if(sh <= ZOD_ROTO_STACK)
        zeile = zeile_stapel;
    else if((zeile = (const tColorY **)malloc((size_t)sh * sizeof(tColorY*))) == nullptr)
        return;
    else
        zeile_halde = 1;

    for(int i = 0; i < sh; i++) zeile[i] = spix + (size_t)spitch * i;

    for(int y = 0; y < dh; y++)
    {
        const int dyz = cy - y;
        const int sdx0 = (ax + (isin * dyz)) + xd;
        const int sdy0 = (ay - (icos * dyz)) + yd;

        int vx0, vx1, vy0, vy1;

        /* Nur dort arbeiten, wo BEIDE Koordinaten innerhalb liegen. */
        if(!zod_spanne(sdx0, icos, sw << 16, dw, &vx0, &vx1)) continue;
        if(!zod_spanne(sdy0, isin, sh << 16, dw, &vy0, &vy1)) continue;

        const int von = vx0 > vy0 ? vx0 : vy0;
        const int bis = vx1 < vy1 ? vx1 : vy1;

        if(von > bis) continue;

        tColorY *pc = dpix + (size_t)dpitch * y + von;
        int sdx = sdx0 + von * icos;
        int sdy = sdy0 + von * isin;

        if(!flipx && !flipy)
        {
            for(int x = von; x <= bis; x++)
            {
                *pc++ = zeile[sdy >> 16][sdx >> 16];
                sdx += icos;
                sdy += isin;
            }
        }
        else
        {
            for(int x = von; x <= bis; x++)
            {
                int dx = sdx >> 16;
                int dy = sdy >> 16;

                if(flipx) dx = (sw - 1) - dx;
                if(flipy) dy = (sh - 1) - dy;

                *pc++ = zeile[dy][dx];
                sdx += icos;
                sdy += isin;
            }
        }
    }

    if(zeile_halde) free((void*)zeile);
}

/* 
 
 32bit specialized 90degree rotator

 Rotates and zooms 'src' surface to 'dst' surface in 90degree increments.

 (contributed by Jeff Schiller)
 
*/
SDL_Surface* rotateSurface90Degrees(SDL_Surface* pSurf, int numClockwiseTurns) 
{
 int row, col, newWidth, newHeight;
 SDL_Surface* pSurfOut;
 
 /* Has to be a valid surface pointer and only 32-bit surfaces (for now) */
 if (!pSurf || pSurf->format->BitsPerPixel != 32) { return nullptr; }

 /* normalize numClockwiseTurns */
 while(numClockwiseTurns < 0) { numClockwiseTurns += 4; }
 numClockwiseTurns = (numClockwiseTurns % 4);

 /* if it's even, our new width will be the same as the source surface */
 newWidth = (numClockwiseTurns % 2) ? (pSurf->h) : (pSurf->w);
 newHeight = (numClockwiseTurns % 2) ? (pSurf->w) : (pSurf->h);
 pSurfOut = SDL_CreateRGBSurface( pSurf->flags, newWidth, newHeight, pSurf->format->BitsPerPixel,
                           pSurf->format->Rmask,
                           pSurf->format->Gmask, 
                           pSurf->format->Bmask, 
                           pSurf->format->Amask);
 if(!pSurfOut) {
   return nullptr;
 }

 if(numClockwiseTurns != 0) {
   SDL_LockSurface(pSurf);
   SDL_LockSurface(pSurfOut);
   switch(numClockwiseTurns) {
     
     /* rotate clockwise */
     case 1: /* rotated 90 degrees clockwise */
     {
       Uint32* srcBuf = nullptr;
       Uint32* dstBuf = nullptr;

       for (row = 0; row < pSurf->h; ++row) {
         srcBuf = (Uint32*)(pSurf->pixels) + (row*pSurf->pitch/4);
         dstBuf = (Uint32*)(pSurfOut->pixels) + (pSurfOut->w - row - 1);
         for (col = 0; col < pSurf->w; ++col) {
           *dstBuf = *srcBuf;
           ++srcBuf;
           dstBuf += pSurfOut->pitch/4;
         } 
         /* end for(col) */
       } 
       /* end for(row) */
     }
     break;

     case 2: /* rotated 180 degrees clockwise */
     {
       Uint32* srcBuf = nullptr;
       Uint32* dstBuf = nullptr;

       for(row = 0; row < pSurf->h; ++row) {
         srcBuf = (Uint32*)(pSurf->pixels) + (row*pSurf->pitch/4);
         dstBuf = (Uint32*)(pSurfOut->pixels) + ((pSurfOut->h - row - 1)*pSurfOut->pitch/4) + (pSurfOut->w - 1);
         for(col = 0; col < pSurf->w; ++col) {
           *dstBuf = *srcBuf;
           ++srcBuf;
           --dstBuf;
         } 
       } 
     }
     break;

     case 3:
     {
       Uint32* srcBuf = nullptr;
       Uint32* dstBuf = nullptr;

       for(row = 0; row < pSurf->h; ++row) {
         srcBuf = (Uint32*)(pSurf->pixels) + (row*pSurf->pitch/4);
         dstBuf = (Uint32*)(pSurfOut->pixels) + row + ((pSurfOut->h - 1)*pSurfOut->pitch/4);
         for(col = 0; col < pSurf->w; ++col) {
           *dstBuf = *srcBuf;
           ++srcBuf;
           dstBuf -= pSurfOut->pitch/4;
         } 
       } 
     }
     break;
   } 
   /* end switch */

   SDL_UnlockSurface(pSurf);
   SDL_UnlockSurface(pSurfOut);
 } 
 /* end if numClockwiseTurns > 0 */
 else {
   /* simply copy surface to output */
   if(SDL_BlitSurface(pSurf, nullptr, pSurfOut, nullptr)) {
     return nullptr;
   }
 }
 return pSurfOut;
}

/* 
 
 rotozoomSurface()

 Rotates and zoomes a 32bit or 8bit 'src' surface to newly created 'dst' surface.
 'angle' is the rotation in degrees. 'zoom' a scaling factor. If 'smooth' is 1
 then the destination 32bit surface is anti-aliased. If the surface is not 8bit
 or 32bit RGBA/ABGR it will be converted into a 32bit RGBA format on the fly.

*/

#define VALUE_LIMIT	0.001

/* Local rotozoom-size function with trig result return */

void rotozoomSurfaceSizeTrig(int width, int height, double angle, double zoomx, double zoomy, int *dstwidth, int *dstheight, 
			     double *canglezoom, double *sanglezoom)
{
    double x, y, cx, cy, sx, sy;
    double radangle;
    int dstwidthhalf, dstheighthalf;

    /*
     * Determine destination width and height by rotating a centered source box 
     */
    radangle = angle * (M_PI / 180.0);
    *sanglezoom = sin(radangle);
    *canglezoom = cos(radangle);
    *sanglezoom *= zoomx;
    *canglezoom *= zoomx;
    x = width >> 1;
    y = height >> 1;
    cx = *canglezoom * x;
    cy = *canglezoom * y;
    sx = *sanglezoom * x;
    sy = *sanglezoom * y;
    
    dstwidthhalf = MAX((int)
		       ceil(MAX(MAX(MAX(fabs(cx + sy), fabs(cx - sy)), fabs(-cx + sy)), fabs(-cx - sy))), 1);
    dstheighthalf = MAX((int)
			ceil(MAX(MAX(MAX(fabs(sx + cy), fabs(sx - cy)), fabs(-sx + cy)), fabs(-sx - cy))), 1);
    *dstwidth = 2 * dstwidthhalf;
    *dstheight = 2 * dstheighthalf;
}


/* Publically available rotozoom-size function */

void rotozoomSurfaceSizeXY(int width, int height, double angle, double zoomx, double zoomy, int *dstwidth, int *dstheight)
{
    double dummy_sanglezoom, dummy_canglezoom;

    rotozoomSurfaceSizeTrig(width, height, angle, zoomx, zoomy, dstwidth, dstheight, &dummy_sanglezoom, &dummy_canglezoom);
}

/* Publically available rotozoom-size function */

void rotozoomSurfaceSize(int width, int height, double angle, double zoom, int *dstwidth, int *dstheight)
{
    double dummy_sanglezoom, dummy_canglezoom;

    rotozoomSurfaceSizeTrig(width, height, angle, zoom, zoom, dstwidth, dstheight, &dummy_sanglezoom, &dummy_canglezoom);
}

/* Publically available rotozoom function */

SDL_Surface *rotozoomSurface(SDL_Surface * src, double angle, double zoom, int smooth)
{
  return rotozoomSurfaceXY(src, angle, zoom, zoom, smooth);
}

/* Publically available rotozoom function */

SDL_Surface *rotozoomSurfaceXY(SDL_Surface * src, double angle, double zoomx, double zoomy, int smooth)
{
    ZodBigHint zod_hinweis;

    SDL_Surface *rz_src;
    SDL_Surface *rz_dst;
    double zoominv;
    double sanglezoom, canglezoom, sanglezoominv, canglezoominv;
    int dstwidthhalf, dstwidth, dstheighthalf, dstheight;
    int is32bit;
    int i, src_converted;
    int flipx,flipy;
    Uint8 r,g,b;
    Uint32 colorkey;
    int colorKeyAvailable = 0;

    /*
     * Sanity check 
     */
    if (src == nullptr)
    return (nullptr);

    if( src->flags & SDL_SRCCOLORKEY )
    {
        colorkey = src->format->colorkey;
        SDL_GetRGB(colorkey, src->format, &r, &g, &b);
        colorKeyAvailable = 1;
    }
    /*
     * Determine if source surface is 32bit or 8bit 
     */
    is32bit = (src->format->BitsPerPixel == 32);
    if ((is32bit) || (src->format->BitsPerPixel == 8)) {
	/*
	 * Use source surface 'as is' 
	 */
	rz_src = src;
	src_converted = 0;
    } else {
	/*
	 * New source surface is 32bit with a defined RGBA ordering 
	 */
	rz_src =
	    SDL_CreateRGBSurface(SDL_SWSURFACE, src->w, src->h, 32, 
#if SDL_BYTEORDER == SDL_LIL_ENDIAN
                                0x000000ff, 0x0000ff00, 0x00ff0000, 0xff000000
#else
                                0xff000000,  0x00ff0000, 0x0000ff00, 0x000000ff
#endif
	    );
		if(colorKeyAvailable)
			SDL_SetColorKey(src, 0, 0);

    SDL_BlitSurface(src, nullptr, rz_src, nullptr);

		if(colorKeyAvailable)
			SDL_SetColorKey(src, SDL_SRCCOLORKEY, colorkey);
	src_converted = 1;
	is32bit = 1;
    }

    /*
     * Sanity check zoom factor 
     */
    flipx = (zoomx<0.0);
    if (flipx) zoomx=-zoomx;
    flipy = (zoomy<0.0);
    if (flipy) zoomy=-zoomy;
    if (zoomx < VALUE_LIMIT) zoomx = VALUE_LIMIT;
    if (zoomy < VALUE_LIMIT) zoomy = VALUE_LIMIT;
    zoominv = 65536.0 / (zoomx * zoomx);

    /*
     * Check if we have a rotozoom or just a zoom 
     */
    if (fabs(angle) > VALUE_LIMIT) {

	/*
	 * Angle!=0: full rotozoom 
	 */
	/*
	 * ----------------------- 
	 */

	/* Determine target size */
	rotozoomSurfaceSizeTrig(rz_src->w, rz_src->h, angle, zoomx, zoomy, &dstwidth, &dstheight, &canglezoom, &sanglezoom);

	/*
	 * Calculate target factors from sin/cos and zoom 
	 */
	sanglezoominv = sanglezoom;
	canglezoominv = canglezoom;
	sanglezoominv *= zoominv;
	canglezoominv *= zoominv;

	/* Calculate half size */
	dstwidthhalf = dstwidth >> 1;
	dstheighthalf = dstheight >> 1;

	/*
	 * Alloc space to completely contain the rotated surface 
	 */
    rz_dst = nullptr;
	if (is32bit) {
	    /*
	     * Target surface is 32bit with source RGBA/ABGR ordering 
	     */
	    rz_dst =
		SDL_CreateRGBSurface(SDL_SWSURFACE, dstwidth, dstheight + ZOD_GUARD_ROWS, 32,
				     rz_src->format->Rmask, rz_src->format->Gmask,
				     rz_src->format->Bmask, rz_src->format->Amask);
	} else {
	    /*
	     * Target surface is 8bit 
	     */
	    rz_dst = SDL_CreateRGBSurface(SDL_SWSURFACE, dstwidth, dstheight + ZOD_GUARD_ROWS, 8, 0, 0, 0, 0);
	}

	/* Schutzzeilen wieder abziehen: belegt sind dstheight + ZOD_GUARD_ROWS
	 * Zeilen, gezeichnet wird nur in dstheight. */
	if (rz_dst != nullptr) {
	    rz_dst->h = dstheight;
	    rz_dst->clip_rect.h = (Uint16) dstheight;
	}

	/* Der 8-Bit-Weg loescht in transformSurfaceY selbst (memset auf den
	 * Farbschluessel). Ein SDL_FillRect hier waere der ZWEITE Durchgang ueber
	 * dieselbe Flaeche -- bei einem gedrehten 64x64 sind das 8 KB umsonst. */
	if (colorKeyAvailable == 1 && is32bit){
		colorkey = SDL_MapRGB(rz_dst->format, r, g, b);

        SDL_FillRect(rz_dst, nullptr, colorkey );
	}
	
	/*
	 * Lock source surface 
	 */
	SDL_LockSurface(rz_src);
	/*
	 * Check which kind of surface we have 
	 */
	if (is32bit) {
	    /*
	     * Call the 32bit transformation routine to do the rotation (using alpha) 
	     */
	    transformSurfaceRGBA(rz_src, rz_dst, dstwidthhalf, dstheighthalf,
				 (int) (sanglezoominv), (int) (canglezoominv), 
				 flipx, flipy,
				 smooth);
	    /*
	     * Turn on source-alpha support 
	     */
	    SDL_SetAlpha(rz_dst, SDL_SRCALPHA, 255);
	} else {
	    /*
	     * Copy palette and colorkey info 
	     */
	    /* Ein memcpy statt 256 Einzelzuweisungen. Die Farben sind ein
	     * zusammenhaengendes Feld; der Uebersetzer kann die Schleife nicht
	     * zusammenfassen, weil er die Ueberlappung nicht ausschliessen kann. */
	    /* NUR wenn es wirklich zwei verschiedene Paletten sind.
	     *
	     * Im Amiga-Unterbau bekommt jede 8-Bit-Flaeche denselben Zeiger auf
	     * die gemeinsame Palette (sdl_video.cpp) -- dann kopiert das hier
	     * 1 KB auf sich selbst, und zwar ueber den CopyMem-Stummel von
	     * libnix. Je Drehung. Beim Tod eines Forts sind das Dutzende je Bild. */
	    if(rz_dst->format->palette->colors != rz_src->format->palette->colors)
	        memcpy(rz_dst->format->palette->colors,
	               rz_src->format->palette->colors,
	               (size_t)rz_src->format->palette->ncolors * sizeof(SDL_Color));
	    rz_dst->format->palette->ncolors = rz_src->format->palette->ncolors;
	    /*
	     * Call the 8bit transformation routine to do the rotation 
	     */
	    /* rz_dst kommt unmittelbar davor aus SDL_CreateRGBSurface und ist
	     * damit vollstaendig genullt. */
	    transformSurfaceY(rz_src, rz_dst, dstwidthhalf, dstheighthalf,
			      (int) (sanglezoominv), (int) (canglezoominv),
			      flipx, flipy, 1);
	    SDL_SetColorKey(rz_dst, SDL_SRCCOLORKEY | SDL_RLEACCEL, rz_src->format->colorkey);
	}
	/*
	 * Unlock source surface 
	 */
	SDL_UnlockSurface(rz_src);

    } else {

	/*
	 * Angle=0: Just a zoom 
	 */
	/*
	 * -------------------- 
	 */

	/*
	 * Calculate target size
	 */
	zoomSurfaceSize(rz_src->w, rz_src->h, zoomx, zoomy, &dstwidth, &dstheight);

	/*
	 * Alloc space to completely contain the zoomed surface 
	 */
    rz_dst = nullptr;
	if (is32bit) {
	    /*
	     * Target surface is 32bit with source RGBA/ABGR ordering 
	     */
	    rz_dst =
		SDL_CreateRGBSurface(SDL_SWSURFACE, dstwidth, dstheight + ZOD_GUARD_ROWS, 32,
				     rz_src->format->Rmask, rz_src->format->Gmask,
				     rz_src->format->Bmask, rz_src->format->Amask);
	} else {
	    /*
	     * Target surface is 8bit 
	     */
	    rz_dst = SDL_CreateRGBSurface(SDL_SWSURFACE, dstwidth, dstheight + ZOD_GUARD_ROWS, 8, 0, 0, 0, 0);
	}

	/* Schutzzeilen wieder abziehen: belegt sind dstheight + ZOD_GUARD_ROWS
	 * Zeilen, gezeichnet wird nur in dstheight. */
	if (rz_dst != nullptr) {
	    rz_dst->h = dstheight;
	    rz_dst->clip_rect.h = (Uint16) dstheight;
	}

	/* Der 8-Bit-Zoomweg braucht die Fuellung NICHT: zoomSurfaceY schreibt
	 * jeden Punkt von w*h (dgap ueberspringt nur die Zeilenauffuellung), die
	 * Fuellung wird also restlos ueberschrieben. Anders als beim Drehen gibt
	 * es hier keine leeren Ecken. Bei Groesse 6 sind das 272x272 = 74 KB
	 * umsonst, JE Truemmerteil -- und seit ZOD_SPIN laufen genau die
	 * Truemmer ueber diesen Zweig.
	 *
	 * Fuer 32 Bit bleibt sie: dass zoomSurfaceRGBA (mit `smooth`) ebenfalls
	 * jeden Punkt schreibt, ist NICHT geprueft. Dieselbe Klammer wie im
	 * Drehzweig darueber.
	 *
	 * Belegt mit einer Gift-Sonde (zweimal verschieden fuellen, rechnen,
	 * nach Bytes suchen, die beide Male ihr Gift behalten): 0 von 99804
	 * Punkten nie beschrieben. Gegenprobe mit absichtlich ausgelassener
	 * letzter Zeile: 6579 von 79133 -- die Sonde faellt im Fehlerfall
	 * also durch. */
	if (colorKeyAvailable == 1 && is32bit){
		colorkey = SDL_MapRGB(rz_dst->format, r, g, b);
        
        SDL_FillRect(rz_dst, nullptr, colorkey );
	}

	/*
	 * Lock source surface 
	 */
	SDL_LockSurface(rz_src);
	/*
	 * Check which kind of surface we have 
	 */
	if (is32bit) {
	    /*
	     * Call the 32bit transformation routine to do the zooming (using alpha) 
	     */
	    zoomSurfaceRGBA(rz_src, rz_dst, flipx, flipy, smooth);
	    /*
	     * Turn on source-alpha support 
	     */
	    SDL_SetAlpha(rz_dst, SDL_SRCALPHA, 255);
	} else {
	    /*
	     * Copy palette and colorkey info 
	     */
	    /* Ein memcpy statt 256 Einzelzuweisungen. Die Farben sind ein
	     * zusammenhaengendes Feld; der Uebersetzer kann die Schleife nicht
	     * zusammenfassen, weil er die Ueberlappung nicht ausschliessen kann. */
	    /* NUR wenn es wirklich zwei verschiedene Paletten sind.
	     *
	     * Im Amiga-Unterbau bekommt jede 8-Bit-Flaeche denselben Zeiger auf
	     * die gemeinsame Palette (sdl_video.cpp) -- dann kopiert das hier
	     * 1 KB auf sich selbst, und zwar ueber den CopyMem-Stummel von
	     * libnix. Je Drehung. Beim Tod eines Forts sind das Dutzende je Bild. */
	    if(rz_dst->format->palette->colors != rz_src->format->palette->colors)
	        memcpy(rz_dst->format->palette->colors,
	               rz_src->format->palette->colors,
	               (size_t)rz_src->format->palette->ncolors * sizeof(SDL_Color));
	    rz_dst->format->palette->ncolors = rz_src->format->palette->ncolors;
	    /*
	     * Call the 8bit transformation routine to do the zooming 
	     */
	    zoomSurfaceY(rz_src, rz_dst, flipx, flipy);
	    SDL_SetColorKey(rz_dst, SDL_SRCCOLORKEY | SDL_RLEACCEL, rz_src->format->colorkey);
	}
	/*
	 * Unlock source surface 
	 */
	SDL_UnlockSurface(rz_src);
    }

    /*
     * Cleanup temp surface 
     */
    if (src_converted) {
	SDL_FreeSurface(rz_src);
    }

    /*
     * Return destination surface 
     */
    return (rz_dst);
}

/* 
 
 zoomSurface()

 Zoomes a 32bit or 8bit 'src' surface to newly created 'dst' surface.
 'zoomx' and 'zoomy' are scaling factors for width and height. If 'smooth' is 1
 then the destination 32bit surface is anti-aliased. If the surface is not 8bit
 or 32bit RGBA/ABGR it will be converted into a 32bit RGBA format on the fly.

*/

#define VALUE_LIMIT	0.001

void zoomSurfaceSize(int width, int height, double zoomx, double zoomy, int *dstwidth, int *dstheight)
{
    /*
     * Sanity check zoom factors 
     */
    if (zoomx < VALUE_LIMIT) {
	zoomx = VALUE_LIMIT;
    }
    if (zoomy < VALUE_LIMIT) {
	zoomy = VALUE_LIMIT;
    }

    /*
     * Calculate target size 
     */
    *dstwidth = (int) ((double) width * zoomx);
    *dstheight = (int) ((double) height * zoomy);
    if (*dstwidth < 1) {
	*dstwidth = 1;
    }
    if (*dstheight < 1) {
	*dstheight = 1;
    }
}

SDL_Surface *zoomSurface(SDL_Surface * src, double zoomx, double zoomy, int smooth)
{
    ZodBigHint zod_hinweis;

    SDL_Surface *rz_src;
    SDL_Surface *rz_dst;
    int dstwidth, dstheight;
    int is32bit;
    int i, src_converted;
    int flipx, flipy;

    /*
     * Sanity check 
     */
    if (src == nullptr)
    return (nullptr);

    /*
     * Determine if source surface is 32bit or 8bit 
     */
    is32bit = (src->format->BitsPerPixel == 32);
    if ((is32bit) || (src->format->BitsPerPixel == 8)) {
	/*
	 * Use source surface 'as is' 
	 */
	rz_src = src;
	src_converted = 0;
    } else {
	/*
	 * New source surface is 32bit with a defined RGBA ordering 
	 */
	rz_src =
	    SDL_CreateRGBSurface(SDL_SWSURFACE, src->w, src->h, 32, 
#if SDL_BYTEORDER == SDL_LIL_ENDIAN
                                0x000000ff, 0x0000ff00, 0x00ff0000, 0xff000000
#else
                                0xff000000,  0x00ff0000, 0x0000ff00, 0x000000ff
#endif
	    );
    SDL_BlitSurface(src, nullptr, rz_src, nullptr);
	src_converted = 1;
	is32bit = 1;
    }

    flipx = (zoomx<0.0);
    if (flipx) zoomx = -zoomx;
    flipy = (zoomy<0.0);
    if (flipy) zoomy = -zoomy;

    /* Get size if target */
    zoomSurfaceSize(rz_src->w, rz_src->h, zoomx, zoomy, &dstwidth, &dstheight);

    /*
     * Alloc space to completely contain the zoomed surface 
     */
    rz_dst = nullptr;
    if (is32bit) {
	/*
	 * Target surface is 32bit with source RGBA/ABGR ordering 
	 */
	rz_dst =
	    SDL_CreateRGBSurface(SDL_SWSURFACE, dstwidth, dstheight + ZOD_GUARD_ROWS, 32,
				 rz_src->format->Rmask, rz_src->format->Gmask,
				 rz_src->format->Bmask, rz_src->format->Amask);
    } else {
	/*
	 * Target surface is 8bit 
	 */
	rz_dst = SDL_CreateRGBSurface(SDL_SWSURFACE, dstwidth, dstheight + ZOD_GUARD_ROWS, 8, 0, 0, 0, 0);
    }

    /* Schutzzeilen wieder abziehen: belegt sind dstheight + ZOD_GUARD_ROWS
     * Zeilen, gezeichnet wird nur in dstheight. */
    if (rz_dst != nullptr) {
        rz_dst->h = dstheight;
        rz_dst->clip_rect.h = (Uint16) dstheight;
    }

    /*
     * Lock source surface 
     */
    SDL_LockSurface(rz_src);
    /*
     * Check which kind of surface we have 
     */
    if (is32bit) {
	/*
	 * Call the 32bit transformation routine to do the zooming (using alpha) 
	 */
	zoomSurfaceRGBA(rz_src, rz_dst, flipx, flipy, smooth);
	/*
	 * Turn on source-alpha support 
	 */
	SDL_SetAlpha(rz_dst, SDL_SRCALPHA, 255);
    } else {
	/*
	 * Copy palette and colorkey info 
	 */
	for (i = 0; i < rz_src->format->palette->ncolors; i++) {
	    rz_dst->format->palette->colors[i] = rz_src->format->palette->colors[i];
	}
	rz_dst->format->palette->ncolors = rz_src->format->palette->ncolors;
	/*
	 * Call the 8bit transformation routine to do the zooming 
	 */
	zoomSurfaceY(rz_src, rz_dst, flipx, flipy);
	SDL_SetColorKey(rz_dst, SDL_SRCCOLORKEY | SDL_RLEACCEL, rz_src->format->colorkey);
    }
    /*
     * Unlock source surface 
     */
    SDL_UnlockSurface(rz_src);

    /*
     * Cleanup temp surface 
     */
    if (src_converted) {
	SDL_FreeSurface(rz_src);
    }

    /*
     * Return destination surface 
     */
    return (rz_dst);
}

SDL_Surface *shrinkSurface(SDL_Surface * src, int factorx, int factory)
{
    ZodBigHint zod_hinweis;

    SDL_Surface *rz_src;
    SDL_Surface *rz_dst;
    int dstwidth, dstheight;
    int is32bit;
    int i, src_converted;

    /*
     * Sanity check 
     */
    if (src == nullptr)
    return (nullptr);

    /*
     * Determine if source surface is 32bit or 8bit 
     */
    is32bit = (src->format->BitsPerPixel == 32);
    if ((is32bit) || (src->format->BitsPerPixel == 8)) {
	/*
	 * Use source surface 'as is' 
	 */
	rz_src = src;
	src_converted = 0;
    } else {
	/*
	 * New source surface is 32bit with a defined RGBA ordering 
	 */
	rz_src =
	    SDL_CreateRGBSurface(SDL_SWSURFACE, src->w, src->h, 32, 
#if SDL_BYTEORDER == SDL_LIL_ENDIAN
                                0x000000ff, 0x0000ff00, 0x00ff0000, 0xff000000
#else
                                0xff000000,  0x00ff0000, 0x0000ff00, 0x000000ff
#endif
	    );
    SDL_BlitSurface(src, nullptr, rz_src, nullptr);
	src_converted = 1;
	is32bit = 1;
    }

    /* Get size for target */
    dstwidth=rz_src->w/factorx;
    while (dstwidth*factorx>rz_src->w) { dstwidth--; }
    dstheight=rz_src->h/factory;
    while (dstheight*factory>rz_src->h) { dstheight--; }

    /*
     * Alloc space to completely contain the shrunken surface 
     */
    rz_dst = nullptr;
    if (is32bit) {
	/*
	 * Target surface is 32bit with source RGBA/ABGR ordering 
	 */
	rz_dst =
	    SDL_CreateRGBSurface(SDL_SWSURFACE, dstwidth, dstheight + ZOD_GUARD_ROWS, 32,
				 rz_src->format->Rmask, rz_src->format->Gmask,
				 rz_src->format->Bmask, rz_src->format->Amask);
    } else {
	/*
	 * Target surface is 8bit 
	 */
	rz_dst = SDL_CreateRGBSurface(SDL_SWSURFACE, dstwidth, dstheight + ZOD_GUARD_ROWS, 8, 0, 0, 0, 0);
    }

    /* Schutzzeilen wieder abziehen: belegt sind dstheight + ZOD_GUARD_ROWS
     * Zeilen, gezeichnet wird nur in dstheight. */
    if (rz_dst != nullptr) {
        rz_dst->h = dstheight;
        rz_dst->clip_rect.h = (Uint16) dstheight;
    }

    /*
     * Lock source surface 
     */
    SDL_LockSurface(rz_src);
    /*
     * Check which kind of surface we have 
     */
    if (is32bit) {
	/*
	 * Call the 32bit transformation routine to do the shrinking (using alpha) 
	 */
	shrinkSurfaceRGBA(rz_src, rz_dst, factorx, factory);
	/*
	 * Turn on source-alpha support 
	 */
	SDL_SetAlpha(rz_dst, SDL_SRCALPHA, 255);
    } else {
	/*
	 * Copy palette and colorkey info 
	 */
	for (i = 0; i < rz_src->format->palette->ncolors; i++) {
	    rz_dst->format->palette->colors[i] = rz_src->format->palette->colors[i];
	}
	rz_dst->format->palette->ncolors = rz_src->format->palette->ncolors;
	/*
	 * Call the 8bit transformation routine to do the shrinking 
	 */
	shrinkSurfaceY(rz_src, rz_dst, factorx, factory);
	SDL_SetColorKey(rz_dst, SDL_SRCCOLORKEY | SDL_RLEACCEL, rz_src->format->colorkey);
    }
    /*
     * Unlock source surface 
     */
    SDL_UnlockSurface(rz_src);

    /*
     * Cleanup temp surface 
     */
    if (src_converted) {
	SDL_FreeSurface(rz_src);
    }

    /*
     * Return destination surface 
     */
    return (rz_dst);
}
