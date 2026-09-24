/* Teamfarben: kommen sie auch durch, wenn die Palettentabelle -- wie im
 * Archiv -- durch RGB565 gelaufen ist?
 *
 * Der Host liest die BMP direkt von der Platte und kann den Fehler deshalb
 * gar nicht zeigen. Die Umsetzung wird hier nachgebildet, sonst prueft der
 * Test einen Fall, den es auf dem Amiga nicht gibt.
 */
#include <stdio.h>
#include <string.h>
#include <SDL/SDL.h>
#include <SDL/SDL_image.h>
#include "lib_qZod_DnMap/zteam.h"

static int fehler = 0;

static SDL_Surface *durch_rgb565(SDL_Surface *s)
{
    SDL_Surface *o = SDL_CreateRGBSurface(SDL_SWSURFACE, s->w, s->h, 32,
                                          0xFF0000, 0xFF00, 0xFF, 0);
    for(int y = 0; y < s->h; y++)
        for(int x = 0; x < s->w; x++)
        {
            Uint8 *p = (Uint8*)s->pixels + y*s->pitch + x*s->format->BytesPerPixel;
            Uint32 v = (s->format->BytesPerPixel==1)?*p:
                       (s->format->BytesPerPixel==2)?*(Uint16*)p:*(Uint32*)p;
            Uint8 r,g,b; SDL_GetRGB(v, s->format, &r,&g,&b);
            /* genau wie tools/assets/pack.py: 5/6/5, dann zurueck */
            Uint16 q = ((r>>3)<<11)|((g>>2)<<5)|(b>>3);
            Uint8 r2 = (Uint8)(((q>>11)&0x1F)*255/31);
            Uint8 g2 = (Uint8)(((q>>5)&0x3F)*255/63);
            Uint8 b2 = (Uint8)((q&0x1F)*255/31);
            *((Uint32*)((Uint8*)o->pixels + y*o->pitch + x*4)) =
                SDL_MapRGB(o->format, r2,g2,b2);
        }
    return o;
}

int main(void)
{
    static const char *teams[] = {"blue","green","yellow","purple","teal","white","black",0};
    SDL_Init(SDL_INIT_VIDEO);

    int genau_gefunden = 0, tolerant_gefunden = 0;

    for(int k = 0; teams[k]; k++)
    {
        char n[128];
        snprintf(n, sizeof(n), "assets/teams/%s_palette.bmp", teams[k]);
        SDL_Surface *roh = IMG_Load(n);
        if(!roh) { printf("FEHLER %s nicht ladbar\n", teams[k]); fehler++; continue; }

        SDL_Surface *q = durch_rgb565(roh);

        ZTeam_Palette pal;
        if(!pal.LoadSurfacePalette(q)) { printf("FEHLER %s Tabelle nicht lesbar\n", teams[k]); fehler++; }

        SDL_Color c;
        Uint8 r = 223, g = 0, b = 0;
        if(pal.GetReplacement(r, g, b, c)) genau_gefunden++;

        if(pal.GetReplacementNear(223, 0, 0, c, 8))
        {
            tolerant_gefunden++;
            /* Nicht nur "gefunden": es muss die RICHTIGE Farbe sein. */
            SDL_Surface *ro = roh;
            Uint8 *pp = (Uint8*)ro->pixels + 7*ro->pitch + 1*ro->format->BytesPerPixel;
            Uint32 v = (ro->format->BytesPerPixel==1)?*pp:
                       (ro->format->BytesPerPixel==2)?*(Uint16*)pp:*(Uint32*)pp;
            Uint8 er,eg,eb; SDL_GetRGB(v, ro->format, &er,&eg,&eb);
            int d = (c.r-er)*(c.r-er)+(c.g-eg)*(c.g-eg)+(c.b-eb)*(c.b-eb);
            if(d > 64)
            {
                printf("FEHLER %s: (%d,%d,%d) statt (%d,%d,%d)\n",
                       teams[k], c.r,c.g,c.b, er,eg,eb);
                fehler++;
            }
            if(c.r==115 && c.g==115 && c.b==115)
            { printf("FEHLER %s bleibt neutralgrau\n", teams[k]); fehler++; }
        }
        else { printf("FEHLER %s: tolerant NICHT gefunden\n", teams[k]); fehler++; }

        SDL_FreeSurface(q); SDL_FreeSurface(roh);
    }

    /* GEGENPROBE: der alte, exakte Vergleich MUSS an denselben Daten
     * scheitern -- sonst bildet der Test den gemeldeten Fehler gar nicht ab
     * und belegt nichts. */
    if(genau_gefunden != 0)
    {
        printf("FEHLER Gegenprobe: der exakte Vergleich fand %d Teams -- "
               "dann bildet dieser Test den gemeldeten Fehler nicht ab\n",
               genau_gefunden);
        fehler++;
    }

    if(fehler) { printf("NICHT BESTANDEN: %d Fehler\n", fehler); return 1; }
    printf("BESTANDEN: %d Teamfarben trotz RGB565 abgeleitet, "
           "exakter Vergleich scheitert erwartungsgemaess an allen %d\n",
           tolerant_gefunden, 7);
    return 0;
}
