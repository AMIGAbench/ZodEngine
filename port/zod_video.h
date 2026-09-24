#ifndef ZOD_VIDEO_H
#define ZOD_VIDEO_H
/*
 * Zwischensequenzen ueber einen EXTERNEN Player (JVPlayer, jvplay).
 *
 * WARUM EXTERN UND NICHT EINGEBAUT: Die JV-Videos sind 320x200 mit eigener
 * 256er-Palette und 22050 Hz Ton. Die Engine laeuft auf einem 8-Bit-Schirm
 * mit EINER gemeinsamen Palette fuer die ganze Spielgrafik -- ein Film liesse
 * sich darin nicht ohne schweren Farbverlust zeigen. Der Player oeffnet
 * stattdessen seinen eigenen Vollbildschirm, spielt, schliesst ihn wieder.
 * Dieselbe Ueberlegung, aus der das Originalmenue nicht ins Spiel gehoert.
 *
 * DER TON IST DER HEIKLE TEIL. Player und Engine wollen dieselbe Hardware,
 * und es gibt sie nur einmal: AHI vergibt je Treiber genau EINEN Kontext,
 * Paula hat vier Kanaele. Deshalb gibt die Engine den Ton VOLLSTAENDIG frei,
 * solange der Film laeuft, und setzt ihn danach wieder auf -- das ist der
 * "Weg 1", auf den sich der Nutzer festgelegt hat. Ein Player, der sich den
 * Ton mit der Engine teilt, waere kein Entwurf, sondern ein Widerspruch.
 *
 * AUF DEM HOST TUT HIER NICHTS ETWAS. Die Funktionen bleiben trotzdem
 * aufrufbar, damit der Engine-Code ohne #ifdef auskommt.
 */

#ifdef __cplusplus
extern "C" {
#endif

/* Einmal beim Start, aus der Befehlszeile (-V <verzeichnis>).
 *
 *   verzeichnis  Wo die .jv liegen, z.B. "cuts". 0 oder leer = keine Filme.
 *   breit        Waagerechte Aufloesung des Spiels. Ab 640 bekommt der
 *                Player DOUBLE (640x400 statt 320x200) -- ein 320er Bild
 *                mitten auf einem 1280er Schirm waere eine Briefmarke.
 *   ton          0 = stumm (die Engine laeuft mit -s -u), 1 = AHI,
 *                2 = Paula. Folgt der Launcher-Einstellung.
 *   aga          1 = Chipsatzweg erzwingen, 0 = RTG. Heute immer 0; die
 *                Engine kann noch kein AGA. Der Schalter steht schon hier,
 *                damit die Stelle beim Nachruesten EINE ist.
 */
void zod_video_setup(const char *verzeichnis, int breit, int ton, int aga);

/* Laeuft ueberhaupt etwas? 0 = keine Filme (kein -V, oder Verzeichnis leer).
 * Der Aufrufer braucht das nicht zu pruefen -- zod_video_play tut es selbst
 * --, aber es spart das Zusammenbauen von Namen. */
int  zod_video_an(void);

/* Gibt es diesen Film? `name` ist der Rumpf OHNE Pfad und OHNE Endung,
 * also "e_logo", nicht "cuts/e_logo.jv". */
int  zod_video_da(const char *name);

/* Abspielen. Kehrt erst zurueck, wenn der Film durch ist oder der Nutzer
 * ihn mit ESC abgebrochen hat.
 *
 * Rueckgabe: 1 = gespielt, 0 = nichts passiert (kein Verzeichnis, Datei
 * fehlt, Player fehlt oder liess sich nicht starten). Ein fehlender Film
 * ist KEIN Fehler -- das Spiel laeuft dann einfach ohne, genau wie heute.
 */
int  zod_video_play(const char *name);

/* MEHRERE Filme in EINEM Aufruf -- und das ist kein Beiwerk.
 *
 * Der Player oeffnet seinen eigenen Vollbildschirm und schliesst ihn
 * wieder. Je Aufruf. Wer drei Filme in drei Aufrufen spielt, laesst den
 * Amiga dreimal den Bildschirmmodus wechseln, und dazwischen blitzt jedes
 * Mal der Spielschirm auf -- vom Nutzer gemeldet als "Screenwechsel oder
 * Resync zwischen jedem Video".
 *
 * jvplay nimmt eine LISTE (DATEI/M/A) und spielt sie ohne Modewechsel
 * hintereinander. Zusammengehoerende Folgen -- Vorspann, "Gebiet gewonnen"
 * plus Weiterreise, Abspann plus Credits -- gehoeren deshalb in EINEN
 * Aufruf.
 *
 * Fehlende Dateien werden uebersprungen, nicht gemeldet: die Liste bleibt
 * brauchbar, auch wenn der Nutzer nur einen Teil der Filme kopiert hat.
 * Rueckgabe: 1 = mindestens einer lief, 0 = keiner. */
int  zod_video_play_folge(const char * const *namen, int anzahl);

#ifdef __cplusplus
}
#endif

#endif
