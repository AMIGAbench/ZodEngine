#ifndef ZOD_PACK_H
#define ZOD_PACK_H
/*
 * Lader für die Asset-Archive (.zpk, erzeugt von tools/assets/pack.py).
 *
 * Zweck: Auf AmigaOS ist jedes Öffnen einer Datei teuer, und PNG müsste zur
 * Laufzeit dekodiert werden. Die Archive enthalten die Bilder fertig entpackt;
 * geöffnet werden nur noch rund zehn Dateien.
 *
 * Schlüssel ist der Originalpfad ("assets/units/...png"), damit der Ladecode
 * der Engine unverändert bleibt: zod_pack_load() tritt an die Stelle von
 * IMG_Load(), und fehlt ein Eintrag, fällt der Aufrufer auf die Einzeldatei
 * zurück.
 *
 * Alle Zahlen im Archiv sind Big-Endian (Amiga-Reihenfolge).
 */
#include <SDL/SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Archive aus dem Verzeichnis einlesen (Vorgabe "packs").
 * Rückgabe: Anzahl gefundener Einträge, 0 wenn keine Archive da sind. */
int zod_pack_init(const char *dir);

/* Wo die Archive liegen -- und damit auch levelnames.dat, das ZExtract
 * dorthin schreibt. Vorgabe "packs", ueberschreibbar mit ZOD_PACKS. */
const char *zod_pack_dir(void);

/* Alle Archive schliessen und den Index freigeben. */
void zod_pack_shutdown(void);

/* Bild aus den Archiven laden; 0, wenn der Name nicht enthalten ist.
 * Indizierte Bilder liefern eine 8-Bit-Surface mit Palette und gesetztem
 * Farbschlüssel, echte Farbbilder eine 16-Bit-Surface (RGB565). */
SDL_Surface *zod_pack_load(const char *name);

/* Wie zod_pack_load, meldet aber KEINE Fehlanzeige.
 *
 * Nur fuer Aufrufer, die absichtlich ins Blaue fragen. Einziger Fall heute:
 * ZFont probiert alle 255 Zeichencodes durch, und die allermeisten haben gar
 * kein Glyph (Steuerzeichen, Luecken in der Tabelle). Ueber zod_pack_load
 * ergaebe das rund 900 Meldungen je Start und wuerde die Fehlanzeige als
 * Warnzeichen wertlos machen -- sie soll echte Archivluecken anzeigen. */
SDL_Surface *zod_pack_load_quiet(const char *name);

/* 1, wenn Archive geladen wurden */
int zod_pack_available(void);

/* Alle Bildnamen des Index. Nur fuer Pruefwerkzeuge (tests/shim): damit
 * laesst sich jedes einzelne Bild einmal laden und zeichnen, ohne das Spiel
 * zu starten. Die Zeiger gehoeren dem Lader. */
unsigned int zod_pack_image_names(const char ***names);

/* Klang aus dem Archiv holen: rohes vorzeichenbehaftetes 8-Bit-Mono, wie AHI
 * es erwartet. Der Puffer gehört dem Aufrufer und ist mit free() freizugeben.
 * Rückgabe: 1 bei Erfolg, 0 wenn der Name nicht enthalten ist. */
int zod_pack_load_sound(const char *name, unsigned char **data,
                        unsigned int *length, unsigned int *rate);


#ifdef __cplusplus
}
#endif

#endif
