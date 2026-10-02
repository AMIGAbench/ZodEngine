#ifndef ZOD_AMMX_H
#define ZOD_AMMX_H
/*
 * AMMX-Weiche (Apollo 68080).
 *
 * Drei Sperren, damit auf einem 040/060 nie ein AMMX-Opcode ausgeführt wird:
 *
 *  1. Bau: Das Assemblermodul wird nur bei CPU=68080 gebunden, die Aufrufstelle
 *     steht hinter `#ifdef ZOD_AMMX`. Ein 040/060-Bau enthält damit keinen
 *     einzigen AMMX-Opcode -- stärker als jede Laufzeitprüfung.
 *  2. Lauf: `zod_ammx_init()` prüft AttnFlags Bit 10. Nötig, weil der
 *     68080-Bau auch auf einer 060 laufen kann (AMMX liegt im Line-F-Bereich,
 *     dort löst eine 060 den F-Line-Trap aus).
 *  3. Selbsttest: einmal beim Start gegen die C-Fassung, Byte für Byte, über
 *     alle acht Ausrichtungen. Schlägt er fehl, bleibt AMMX aus.
 *
 * `zod_ammx_init()` gehört vor den ersten Blit (SDL_SetVideoMode) und ist
 * mehrfach aufrufbar.
 */

#ifdef __cplusplus
extern "C" {
#endif

void zod_ammx_init(void);

/* 1 = AMMX eingebaut, 68080 erkannt UND Selbsttest bestanden. */
int zod_ammx_available(void);

#ifdef __cplusplus
}
#endif

#endif
