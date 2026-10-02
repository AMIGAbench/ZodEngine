#ifndef ZOD_ZEIGER_H
#define ZOD_ZEIGER_H
/*
 * Der Mauszeiger, spaet gezeichnet.
 *
 * WARUM NICHT DER HARDWARE-ZEIGER VON AmigaOS -- gemessen am 02.10., nicht
 * vermutet. Vom Nutzer gewuenscht war genau der; drei Kosten sprechen dagegen,
 * und alle drei sind belegt:
 *
 *   1. FARBEN. Ein `pointerclass`-Zeiger nimmt seine Farben aus den
 *      Palettenplaetzen 17..19 des Schirms (intuition.doc, SA_FullPalette:
 *      "playfield colors 0-3, and colors 17-19 for the sprite"). Das sind
 *      DREI. Die 196 Zeiger dieses Spiels haben bis zu 13; gemessen decken
 *      die drei haeufigsten Farben im Median nur 78 % der sichtbaren Punkte,
 *      bei 95 Zeigern unter 75 %, im schlimmsten Fall 53 %. Aus schattierten
 *      Haenden wuerden flache Silhouetten.
 *
 *   2. DIE PLAETZE 17..19 SIND DIE ROTE TEAMRAMPE. In palette.zpl stehen dort
 *      (67,0,0), (95,0,0), (120,0,0) -- die Mitteltoene, aus denen
 *      `ZTeam::Make` alle Teamfarben erzeugt. Ein Zeiger, der sie
 *      ueberschreibt, verfaerbt rote Einheiten und Gebaeude (1,1 % aller
 *      Bildpunkte, aber ausgerechnet die Teamfarbe).
 *
 *   3. Ob die RTG-Treiber der Zielhardware ueberhaupt ein echtes
 *      Hardware-Sprite liefern oder den Zeiger in Software nachbilden, steht
 *      in keiner hier vorliegenden Referenz. Der Nutzen waere damit nicht
 *      einmal sicher.
 *
 * NACHGESEHEN, OB DAS ORIGINAL ES BESSER MACHTE: nein. Die Zeiger sind
 * Originalmaterial -- tools/cd/names.txt ordnet 53 Saetze aus SPRITES.RSC
 * bitgleich zu, und dort stehen dieselben bis zu 13 Farben. Z lief auf VGA im
 * Mode-X und zeichnete seinen Zeiger ebenfalls in Software; eine Farbgrenze
 * gab es dort nie.
 *
 * WAS DER HARDWARE-ZEIGER GEBRACHT HAETTE, ist die VERZOEGERUNG, und nur die:
 * Der Zeiger wurde bisher waehrend des Bildaufbaus in die Zeichenflaeche
 * geblittet und erschien erst, wenn das fertige Bild in den Schirm kam -- ein
 * ganzes Bild spaeter. Auf der V1200 sind das 12 bis 19 ms, in einer
 * Explosion bis 40. Der Nutzer zielt mit dem gezeichneten Zeiger, die Maus
 * steht da aber schon weiter; der Klick landet systematisch VOR dem Ziel, in
 * Bewegungsrichtung. Genau das sieht aus wie ein verschluckter Klick.
 *
 * DIESER WEG holt den Nutzen ohne jede der drei Kosten: der Zeiger wird in
 * `SDL_Flip` gezeichnet, NACH der Bildkopie, an der Lage von genau diesem
 * Augenblick. Er liegt damit um die Dauer der Kopie zurueck statt um ein
 * ganzes Bild. Alle Farben bleiben, keine Palette wird angefasst.
 *
 * Er ist ein echtes Software-Sprite: was er ueberdeckt, wird gesichert und
 * beim naechsten Bild zuerst zurueckgeschrieben. BEWUSST NICHT ueber die
 * Schmutzspur -- die ist auf drei Messreihen eingestellt (siehe CLAUDE.md,
 * "Weniger kopieren"), und ein Zeiger, der dort jedes Bild Bloecke
 * schmutzig macht, zieht Folgekosten nach sich, die mit ihm nichts zu tun
 * haben. 16x16 sichern und zurueckschreiben sind 512 Byte je Bild.
 */

#ifdef __cplusplus
extern "C" {
#endif

struct SDL_Surface;

/* 1 = der Port zeichnet den Zeiger selbst (Amiga). Dann darf die Engine ihn
 * NICHT in die Zeichenflaeche blitten -- sonst stuende er zweimal im Bild. */
int zod_zeiger_spaet(void);

/* Welches Bild im naechsten SDL_Flip gilt. hot_x/hot_y ist der Aufhaengepunkt
 * IM BILD (0,0 = oben links, 8,8 = Mitte bei 16x16). */
void zod_zeiger_setzen(struct SDL_Surface *bild, int hot_x, int hot_y);

/* Kein Zeiger im naechsten Bild (Menues, Filme). */
void zod_zeiger_keiner(void);

#ifdef __cplusplus
}
#endif

#endif
