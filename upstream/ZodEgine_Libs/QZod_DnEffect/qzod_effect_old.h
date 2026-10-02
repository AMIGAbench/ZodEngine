#ifndef QZOD_EFFECT_OLD_H
#define QZOD_EFFECT_OLD_H

#include "qzod_dneffect_global.h"



class QZOD_DNEFFECTSHARED_EXPORT effect_flags
{
public:
    effect_flags(){ Clear(); }

    bool unit_particles;
    int unit_particles_radius;
    int unit_particles_amount;
    int x, y;

    void Clear()
    {
        unit_particles = false;
        unit_particles_radius = 0;
        unit_particles_amount = 0;
    }
};

class QZOD_DNEFFECTSHARED_EXPORT ZEffect
{
    public:
        ZEffect(ZTime *ztime_);
        virtual ~ZEffect() {}

        static void SetEffectList(vector<ZEffect*> *effect_list_);
        static void SetSettings(ZSettings *zsettings_);
        static void SetMap(ZMap *zmap_) { zmap = zmap_; }

        virtual void Process();
        /* Hier stand in BEIDEN Fassungen eine Ausgabe des Kartennamens ueber
         * std::cerr -- und zwar unter der Bedingung, dass die Karte NICHT leer
         * ist, also im laufenden Spiel IMMER. Das sind die Basisfassungen:
         * jeder Effekt, der sie nicht ueberschreibt, schrieb damit zwei
         * formatierte Zeilen JE BILD auf die Konsole. Beim Tod eines Forts
         * leben hunderte Effekte gleichzeitig.
         *
         * Zusaetzlich kopierte GetMapBasics() dafuer die gesamte
         * map_basics-Struktur, nur um einen Namen zu drucken.
         *
         * std::cerr laeuft am Logmodul vorbei und landet unumgeleitet auf der
         * Konsole; auf AmigaOS ist jede Zeile ein synchroner Schreibvorgang.
         * Das ist mit hoher Wahrscheinlichkeit die wahre Ursache des
         * Explosions-Ruckelns, das der Nutzer von Anfang an gemeldet hat --
         * nicht der Sprite-Skalierer.
         *
         * Die Pruefung auf eine kaputte Zielflaeche ist ebenfalls entfallen:
         * sie lief je Effekt und Bild, und ein Nullzeiger wuerde eine Zeile
         * spaeter ohnehin auffallen. */
        virtual void DoPreRender(ZMap &the_map, SDL_Surface *dest)
        {
            (void)the_map; (void)dest;
        }
        virtual void DoRender(ZMap &the_map, SDL_Surface *dest)
        {
            (void)the_map; (void)dest;
        }
        bool KillMe();
        effect_flags &GetEFlags();
    protected:
        bool killme;

        static ZSettings *zsettings;
        static ZSettings default_settings;
        static vector<ZEffect*> *effect_list;
        static ZMap *zmap;
        ZTime *ztime;

        effect_flags eflags;
};


#endif // QZOD_EFFECT_OLD_H
