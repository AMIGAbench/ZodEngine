#ifndef FIND_STRUCTURES_H
#define FIND_STRUCTURES_H

#include "../qzod_dnmap_global.h"
#include <string.h>   /* memset fuer InitPointIndex */

/* Zaehlt die Speicherbelegungen der Wegsuche. Definiert in
 * zpath_finding_old.cpp. Ohne diese Stueckzahl waere nicht zu unterscheiden,
 * ob die Wiederverwendung greift oder nur so aussieht. */
extern unsigned long zod_pf_belegungen;


enum pf_tile_types
{
    PF_NORMAL, PF_IMPASSABLE, PF_WATER, PF_ROAD, MAX_PF_TILE_TYPES
};

struct QZOD_DNMAPSHARED_EXPORT map_pathfinding_info_tile
{
    int side_weight;
    int diag_weight;
    bool passable;
};


class QZOD_DNMAPSHARED_EXPORT ffnode
{
public:
    ffnode(int x_, int y_) { x=x_; y=y_; }
    int x, y;
};

class QZOD_DNMAPSHARED_EXPORT pf_point
{
public:
    /* px/py sind der VORGAENGER. Der Startknoten hat keinen -- deshalb
     * muessen sie auf einen Wert, den die Bereichspruefung in Do_Astar
     * sicher verwirft. Uninitialisiert waren sie Muell, und seit die
     * Wegrekonstruktion damit INDIZIERT statt zu suchen, ist das ein
     * Zugriff ins Leere (Host: Absturz, Amiga ohne MMU: stiller
     * Fremdzugriff und ein verdorbener Weg). */
    pf_point() { x = y = 0; px = py = -1; f = g = h = 0; }
    pf_point(int x_, int y_) {x=x_; y=y_; px=py=-1; f=0; g=0; h=0;}

    int x, y;
    int f, g, h;
    int px, py;
};

class QZOD_DNMAPSHARED_EXPORT pf_point_array
{
public:
    pf_point_array()
    {
        list = nullptr; // TODO nullptr;
        point_index = nullptr; // TODO nullptr;
        pi_block = nullptr;
        w = h = 0;
        size = 0;
        alloc_size = 0;
        baum = nullptr;
        baum_blaetter = 0;
        baum_an = 0;
    }

    /* ------------------------------------------------------------------
     * TURNIERBAUM UEBER DIE FELDPLAETZE
     *
     * Ersetzt die lineare Suche in lowest_f_cost, die bei JEDER Expansion
     * ueber die ganze offene Liste lief. Auf der V1200 gemessen:
     * server.wegjobs 4141 us im Mittel (39 % der Serverzeit), Spitze
     * 390 644 us in einem Bild.
     *
     * WARUM KEIN GEWOEHNLICHER BINAERHEAP: Der ordnet das Feld um. Der
     * heutige Code entscheidet den Gleichstand aber ueber den FELDPLATZ --
     * `lowest_f_cost` vergleicht strikt (`>`), bei gleichem f gewinnt also
     * der kleinste Index. Ein Heap waehlte einen anderen Knoten, und bei
     * gleichem f auf offenem Gelaende ist das der Normalfall, nicht die
     * Ausnahme: Einheiten liefen sichtbar andere (gleich teure) Wege.
     *
     * Der Turnierbaum laesst das Feld unberuehrt und fuehrt nur eine
     * Rangordnung darueber. Sein Schluessel ist (f, Platz), lexikographisch
     * -- also genau die Regel von oben. Das Ergebnis ist damit bitgleich.
     *
     * Nur fuer die OFFENE Liste eingeschaltet: Die geschlossene Liste wird
     * nie nach dem Kleinsten gefragt, und sie aendert f an Ort und Stelle
     * (Kostenverbesserung eines geschlossenen Knotens) -- ein Baum darueber
     * wuerde still veralten. */
    inline int BaumF(int platz) const { return list[platz].f; }

    inline int BaumBesser(int a, int b) const
    {
        if(a < 0) return b;
        if(b < 0) return a;

        const int fa = BaumF(a), fb = BaumF(b);

        if(fa != fb) return (fa < fb) ? a : b;

        return (a < b) ? a : b;   /* Gleichstand: kleinster Platz gewinnt */
    }

    /* Kapazitaet sicherstellen -- belegt NUR, wenn der Baum zu klein ist.
     * Ueber Suchen hinweg bleibt er damit stehen, und nach der ersten grossen
     * Suche faellt keine Belegung mehr an. */
    inline void BaumPlatz()
    {
        int n = 1;

        while(n < alloc_size) n <<= 1;

        if(baum && baum_blaetter >= n) return;

        zod_pf_belegungen++;

        free(baum);
        baum_blaetter = n;
        baum = (int*)malloc((size_t)2 * n * sizeof(int));

        if(!baum) { baum_blaetter = 0; baum_an = 0; }
    }

    inline void BaumNeu()
    {
        BaumPlatz();

        if(!baum) return;

        const int n = baum_blaetter;

        for(int i = 0; i < 2 * n; i++) baum[i] = -1;

        for(int i = 0; i < size; i++) baum[n + i] = i;

        for(int i = n - 1; i >= 1; i--)
            baum[i] = BaumBesser(baum[2*i], baum[2*i+1]);
    }

    inline void BaumSetze(int platz, int belegt)
    {
        if(!baum_an || !baum) return;

        int i = baum_blaetter + platz;

        baum[i] = belegt ? platz : -1;

        /* KEIN vorzeitiger Abbruch bei unveraendertem Sieger.
         *
         * Das waere der uebliche Kniff, hier aber falsch: In RemovePoint
         * behaelt der nachgerueckte Punkt seinen PLATZ, bekommt aber ein
         * anderes f. Der Sieger eines Knotens kann derselbe Platz bleiben,
         * waehrend sich sein Wert aendert -- und dann kippt weiter oben
         * sehr wohl noch etwas. Der Baum liefe still falsch, und zwar nur
         * manchmal. Der Weg bis zur Wurzel sind rund zehn Schritte. */
        for(i >>= 1; i >= 1; i >>= 1)
            baum[i] = BaumBesser(baum[2*i], baum[2*i+1]);
    }

    /* Platz des kleinsten (f, Platz), oder -1 wenn leer. */
    inline int BaumKleinster() const
    {
        if(!baum_an || !baum) return -1;

        return baum[1];
    }

    inline void BaumEin()
    {
        baum_an = 1;
        BaumNeu();
    }

    /* ------------------------------------------------------------------
     * WIEDERVERWENDUNG UEBER WEGSUCHEN HINWEG
     *
     * Vorher belegte JEDE Suche sechs Bloecke neu -- zwei Listen, zwei
     * Zeigertabellen, zwei Koordinatenbloecke -- und `list` wuchs in
     * 1000er-Schritten per realloc. Bei einer Suche ueber 16 000 Knoten
     * (diagonal ueber eine 128x128-Karte, der vom Nutzer gemeldete Fall)
     * sind das 16 reallocs mit kumuliert mehreren Megabyte Kopieraufwand,
     * dazu der Neuaufbau des Baums.
     *
     * Auf AmigaOS ist gerade das Belegen teuer, und libnix gibt Freigegebenes
     * ohnehin NICHT ans System zurueck -- daran lag das Schlieren ab der
     * 3./4. Karte. Die Bloecke stehen zu lassen kostet also keinen zusaetzlichen
     * Speicher -- es vermeidet nur das staendige Belegen und Zerstueckeln.
     *
     * Was je Suche bleibt: das Nullsetzen des Koordinatenindex. Das ist ein
     * memset ueber w*h ints und unvermeidlich, solange -1 "nicht enthalten"
     * bedeutet. */
    inline void Wiederverwenden(int w_, int h_)
    {
        if(w != w_ || h != h_ || !point_index || !pi_block)
        {
            FreePointIndex();
            InitPointIndex(w_, h_);
        }
        else
            memset(pi_block, 0xFF, (size_t)w * h * sizeof(int));

        if(!list)
        {
            alloc_size = 1000;
            list = (pf_point*)malloc((size_t)alloc_size * sizeof(pf_point));
            zod_pf_belegungen++;
        }

        size = 0;

        if(baum_an) BaumNeu();   /* leert die Blaetter, belegt nur bei Bedarf */
    }

    /* Koordinatenindex der Wegsuche.
     *
     * Hier stand eine eigene malloc JE SPALTE (128 bzw. 256 Stueck) plus
     * w*h Einzelschreibungen auf -1 -- und das ZWEIMAL je Wegsuche (offene
     * und geschlossene Liste). Auf einer 256x256-Karte also 512 Belegungen
     * und 131 072 Schreibvorgaenge, bevor der erste A*-Schritt laeuft.
     * Auf AmigaOS ist gerade das Belegen teuer, und libnix gibt Freigegebenes
     * nicht ans System zurueck -- daran lag das Schlieren ab der 3./4. Karte.
     *
     * Jetzt: ZWEI Belegungen statt w+1 -- ein Block fuer alle Spalten, die
     * Zeigertabelle zeigt hinein. Das Nullsetzen ist ein memset: -1 ist in
     * allen Bytes 0xFF, das gilt fuer int genauso.
     *
     * NICHT ueber Wegsuchen hinweg wiederverwendet: open_list und
     * closed_list sind LOKALE Variablen in Do_Astar und werden bei jedem
     * Aufruf neu angelegt. Ein Zwischenspeicher muesste dort ansetzen, nicht
     * hier -- sonst bleibt bei jeder Suche Speicher liegen. */
    inline void InitPointIndex(int w_, int h_)
    {
        w = w_;
        h = h_;

        zod_pf_belegungen += 2;

        point_index = (int **)malloc((size_t)w * sizeof(int *));
        pi_block    = (int *)malloc((size_t)w * h * sizeof(int));

        if(!point_index || !pi_block)
        {
            FreePointIndex();
            return;
        }

        for(int i=0;i<w;i++) point_index[i] = pi_block + (size_t)i * h;

        memset(pi_block, 0xFF, (size_t)w * h * sizeof(int));
    }

    inline void FreePointIndex()
    {
        free(point_index);
        free(pi_block);
        point_index = nullptr;
        pi_block = nullptr;
    }

    inline void FreeList()
    {
        free(list);
        free(baum);
        baum = nullptr;
        baum_blaetter = 0;
        baum_an = 0;
    }

    inline void AddPoint(pf_point &np)
    {
        bool gewachsen = false;

        if(size >= alloc_size)
        {
            alloc_size += 1000;
            list = (pf_point*)realloc(list, alloc_size * sizeof(pf_point));
            gewachsen = true;
            zod_pf_belegungen++;
        }

        list[size] = np;
        point_index[np.x][np.y] = size;

        size++;

        if(baum_an)
        {
            /* Nach einem realloc kann der Baum zu klein sein -- dann neu
             * aufbauen (alle 1000 Punkte einmal, O(n) gegen O(n) je
             * Expansion vorher). */
            if(gewachsen && alloc_size > baum_blaetter) BaumNeu();
            else BaumSetze(size - 1, 1);
        }
    }

    inline void RemovePoint(int x, int y)
    {
        int i = point_index[x][y];

        if(i != -1)
        {
            point_index[x][y] = -1;

            size--;
            list[i] = list[size];

            point_index[list[i].x][list[i].y] = i;

            /* Reihenfolge wichtig: erst den frei gewordenen LETZTEN Platz
             * leeren, dann den nachgerueckten Punkt eintragen. Bei i == size
             * ist beides derselbe Platz, und er bleibt leer -- genau wie im
             * Feld, wo er jenseits von `size` liegt. */
            BaumSetze(size, 0);

            if(i < size) BaumSetze(i, 1);
        }
    }

    pf_point* list;
    int **point_index;
    int *pi_block;     /* ein Block fuer alle Spalten, wiederverwendet */
    int w, h;
    int size;
    int alloc_size;

    int *baum;           /* Turnierbaum, 2*baum_blaetter Eintraege */
    int baum_blaetter;   /* auf Zweierpotenz aufgerundet */
    int baum_an;
};






// %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
inline int the_dir(pf_point &lp, pf_point &cp)
{
    int res(-1);
    if(cp.x > lp.x)
    {
        if(cp.y == lp.y)
            res= 0;
        else if(cp.y < lp.y)
            res= 1;
        else
            res= 7;
    }
    else if(cp.x == lp.x)
    {
        if(cp.y > lp.y)
            res= 2;
        else
            res= 6;
    }
    else
    {
        if(cp.y < lp.y)
            res= 3;
        else if(cp.y > lp.y)
            res= 5;
        else
            res= 4;
    }

    if( res < 0 )
        std::cerr<<"\n--<error>\t ZPath_Finding_AStar::dir = -1?!\n";

    return res;
}

#endif
