* ---------------------------------------------------------------------------
* AMMX-Blit mit Farbschluessel fuer den Apollo 68080
*
* Ersetzt die innerste Schleife von SDL_UpperBlit (port/amiga/sdl_video.cpp):
*
*     for(x = 0; x < w; x++) { v = s[x]; if(v != key) d[x] = v; }
*
* Die laeuft fuer JEDES Sprite in JEDEM Bild, Byte fuer Byte. Mit AMMX sind
* es acht Bildpunkte je Durchgang und drei Befehle:
*
*     load     (a1),e0       acht Quellbytes
*     pcmpeqb  e1,e0,e2      e2 = $FF je Byte, das dem Schluessel gleicht
*     storeilm e0,e2,(a0)    schreibt nur die Bytes, deren Maske 0 ist
*
* Das Besondere ist STOREILM: Es schreibt bedingt, ohne das ZIEL zu lesen.
* Ein Farbschluessel-Blit braucht damit keinen Lesezugriff aufs Ziel --
* genau die Ersparnis, die eine Boolesche Nachbildung (Ziel lesen, maskieren,
* zusammensetzen, schreiben) nicht hat.
*
* Belegt aus der Referenz (AC68080PRM_all.pdf):
*   STOREILM b,mask,<vea>  speichert je Byte NUR, wenn im Maskenbyte das
*                          oberste Bit (MSB) NICHT gesetzt ist. Massgeblich
*                          ist der C-Ausschnitt aus Apollos eigener AMMX-Doku
*                          (ApolloCrossDev, Docs/AMMX/STOREILM.md):
*                              d[i] = ( m[i] & 128 ) ? d[i] : a[i];
*                          ACHTUNG: Die Hauptseite des PRM sagt "lsb of the 8
*                          bytes" und widerspricht sich damit selbst. Fuer
*                          Masken aus PCMPEQB ($00/$FF) sind beide Lesarten
*                          gleichwertig -- fuer jede andere Maske NICHT.
*   PCMPEQB  <vea>,b,d     byteweiser Vergleich, wahr = $FF, falsch = $00.
*                          $FF hat das MSB gesetzt -> STOREILM ueberspringt
*                          genau die Bytes, die dem Schluessel gleichen. Ohne
*                          Umkehrung der Maske.
*
* Apollo nennt genau diesen Fall als Beispiel (Docs/AMMX/STOREILM.md):
*     pcmpeqw.w #$f81f,E0,E2   ; magenta Pixel in E0?
*     storeilm  E0,E2,(a0)     ; nur speichern, wo es NICHT passt
*
* Moeglich waere auch STOREM3 im Bytemodus (ein Befehl statt zwei: speichert
* jedes Byte, das NICHT $00 ist -- und $00 ist unser Farbschluessel). Nicht
* benutzt, weil die Modusnummer in der Doku uneindeutig ist (#1 im Beispiel,
* #2/#3 in der PRM-Tabelle) und sich hier nicht pruefen laesst.
*   E0..E23 sind EIGENE Register (kein Alias auf D0-D7). gcc benutzt sie
*   nie, also ist keine Sicherung noetig.
*
* NICHT belegt und auf der Vampire zu pruefen:
*   Ob LOAD und STOREILM unausgerichtete Adressen annehmen. Die Referenz sagt
*   dazu nichts (der Hinweis "on the 080 it does not have to be aligned"
*   steht bei MOVE16, nicht bei AMMX). Sprite-Zeilen beginnen auf beliebigen
*   Bytes, die Routine setzt es also voraus. Der Selbsttest beim Start
*   (port/amiga/ammx.cpp) prueft genau das und schaltet AMMX ab, wenn es
*   nicht stimmt.
*
* Aufrufweise: Bebbo-gcc uebergibt auf dem Stapel.
*   void zod_ammx_blit_row(UBYTE *dst, const UBYTE *src, ULONG count,
*                          const UBYTE *key8);
*   4(sp)=dst  8(sp)=src  12(sp)=count  16(sp)=key8
*
* key8 zeigt auf ACHT Byte, alle gleich dem Farbschluessel. Den Aufbau macht
* der Aufrufer einmal je Blit -- nicht je Zeile, und vor allem nicht hier:
* LOAD mit Speicherquelle ist eindeutig 64 Bit, ein Aufbau aus einem
* Datenregister waere es nicht.
*
* Veraendert: d0, d1, a0, a1, e0, e1, e2 -- alle nach m68k-Konvention frei.
* ---------------------------------------------------------------------------

	machine	68080

	section	.text,code

	xdef	_zod_ammx_blit_row

_zod_ammx_blit_row:
	move.l	16(sp),a0		; key8
	load	(a0),e1			; e1 = acht mal der Schluessel
	move.b	(a0),d1			; d1.b = Schluessel, fuer den Rest der Zeile

	move.l	4(sp),a0		; a0 = dst
	move.l	8(sp),a1		; a1 = src
	move.l	12(sp),d0		; d0 = Anzahl Bildpunkte

* --- acht Bildpunkte je Durchgang -----------------------------------------
.acht:
	cmp.l	#8,d0
	blo.b	.rest			; unsigned: weniger als acht uebrig

	load	(a1),e0
	pcmpeqb	e1,e0,e2
	storeilm e0,e2,(a0)

	addq.l	#8,a1
	addq.l	#8,a0
	subq.l	#8,d0
	bra.b	.acht

* --- Rest byteweise (Zeilenbreite ist selten ein Vielfaches von 8) --------
* Kein zusaetzliches Register noetig: der Vergleich geht direkt gegen den
* Speicher, und m68k kann Speicher nach Speicher kopieren.
.rest:
	tst.l	d0
	beq.b	.fertig

.byte:
	cmp.b	(a1),d1
	beq.b	.weiter			; gleich dem Schluessel -> nicht schreiben
	move.b	(a1),(a0)
.weiter:
	addq.l	#1,a1
	addq.l	#1,a0
	subq.l	#1,d0
	bne.b	.byte

.fertig:
	rts


*--------------------------------------------------------------------------
* void zod_ammx_blit_row_k0(UBYTE *dst, const UBYTE *src, ULONG count)
*
* Sonderfall Farbschluessel = 0, und das ist bei uns der Normalfall: Die
* gemeinsame Palette vergibt Platz 0 ausschliesslich an durchsichtige Punkte.
*
* STOREM3 im BYTEMODUS macht daraus zwei Befehle statt drei. Die Referenz:
* "each byte of the source is stored to the destination which has not the
* value of $00. This is perfect for copying sprites where $00 indicated
* transparent." Der Schluessel ist dabei FEST VERDRAHTET -- deshalb gibt es
* diese Fassung nur fuer 0, und der allgemeine Weg oben bleibt fuer alles
* andere.
*
* Zwei Eigenheiten, beide belegt:
*  - Die Modusnummer wird in vasm als REGISTER geschrieben: "storem3 e0,d1,.."
*    bedeutet Modus 1, nicht das Register d1. Die Referenz sagt es
*    ausdruecklich ("must be written as storem3 d0,d3,(a0)"), und der
*    Mario-Kart-Port schreibt es fuer seinen 16-Bit-Fall genauso (dort d2).
*  - LOAD und STOREM3 nehmen Postinkrement, das spart die beiden addq.
*
* Veraendert: d0, d1, a0, a1, e0 -- alle nach m68k-Konvention frei.
*--------------------------------------------------------------------------

	xdef	_zod_ammx_blit_row_k0

_zod_ammx_blit_row_k0:
	move.l	4(sp),a0		; a0 = dst
	move.l	8(sp),a1		; a1 = src
	move.l	12(sp),d0		; d0 = Anzahl Bildpunkte

.k0_acht:
	cmp.l	#8,d0
	blo.b	.k0_rest

	load	(a1)+,e0
	storem3	e0,d1,(a0)+		; d1 = MODUS 1 (Bytes != $00 schreiben)

	subq.l	#8,d0
	bra.b	.k0_acht

.k0_rest:
	tst.l	d0
	beq.b	.k0_fertig

.k0_byte:
	tst.b	(a1)
	beq.b	.k0_weiter		; $00 -> durchsichtig, nicht schreiben
	move.b	(a1),(a0)
.k0_weiter:
	addq.l	#1,a1
	addq.l	#1,a0
	subq.l	#1,d0
	bne.b	.k0_byte

.k0_fertig:
	rts


*--------------------------------------------------------------------------
* void zod_ammx_copy_row(UBYTE *dst, const UBYTE *src, ULONG count)
*
* Lineares Kopieren ueber AMMX: acht Byte je load/store statt vier je move.l.
*
* Die Apollo-Doku sagt fuer genau diese beiden Befehle ausdruecklich zu, dass
* sie KEINE Ausrichtung verlangen:
*   LOAD  "In case of memory sources, there are no restrictions towards
*          alignment. You may load from any valid address in Chip- and FastRAM."
*   STORE "You may store to any valid address ... There are no alignment
*          restrictions."
* Das ist wichtig, weil unsere Flaechen bei AllocVec-Basis + 4 liegen, also
* nie auf 8 oder 16 ausgerichtet sind.
*
* 32 Byte je Durchgang (vier Registerpaare), damit der Schleifenaufwand nicht
* den Gewinn frisst; danach 8er-Schritte, dann Bytes.
*
* Veraendert: d0, a0, a1, e0-e3 -- alle nach m68k-Konvention frei.
*--------------------------------------------------------------------------

	xdef	_zod_ammx_copy_row

_zod_ammx_copy_row:
	move.l	4(sp),a0		; a0 = dst
	move.l	8(sp),a1		; a1 = src
	move.l	12(sp),d0		; d0 = Anzahl Byte

.c_32:
	cmp.l	#32,d0
	blo.b	.c_8

	load	(a1)+,e0
	load	(a1)+,e1
	load	(a1)+,e2
	load	(a1)+,e3
	store	e0,(a0)+
	store	e1,(a0)+
	store	e2,(a0)+
	store	e3,(a0)+

	sub.l	#32,d0
	bra.b	.c_32

.c_8:
	cmp.l	#8,d0
	blo.b	.c_rest

	load	(a1)+,e0
	store	e0,(a0)+

	subq.l	#8,d0
	bra.b	.c_8

.c_rest:
	tst.l	d0
	beq.b	.c_fertig

.c_byte:
	move.b	(a1)+,(a0)+
	subq.l	#1,d0
	bne.b	.c_byte

.c_fertig:
	rts

*--------------------------------------------------------------------------
* RECHTECKFASSUNGEN: die Zeilenschleife liegt IN der Routine
*
* Warum: Die Zeilenschleife stand im C-Teil (sdl_video.cpp), es gab also
* einen Aufruf JE ZEILE. Auf der V1200 gemessen (19.09., ZOD_COPYBENCH) ist
* ein solcher Aufruf rund 350 ns wert -- so viel wie 58 kopierte Byte. Ein
* Truemmersprite von 40 Zeilen kostete damit rund 14 us allein an Aufrufen,
* und beim Sterben des Hauptgebaeudes leben ueber 800 Effekte gleichzeitig.
*
* Belegt, dass es sich lohnt (V1200, 20.09.): Die zaehste Folge des Laufs
* sind 44 Bilder mit `effekte` = 1352856 us, davon nur 380366 us (28 %)
* das RECHNEN der Flaechen. Die uebrigen 72 % sind Aufwand je Effekt --
* und der Blit ist darin der einzige Posten, der mit der Sprite-HOEHE
* waechst.
*
* Nebenbei faellt beim Schluessel-Weg das Neuladen von e1 je Zeile weg.
*
* Registerkonvention (m68k, AmigaOS): d0/d1/a0/a1 sind frei, d2-d7/a2-a6
* muessen erhalten bleiben. Deshalb movem am Anfang und am Ende.
*--------------------------------------------------------------------------

*--------------------------------------------------------------------------
* void zod_ammx_blit_rect_k0(UBYTE *dst, const UBYTE *src,
*                            ULONG w, ULONG h, LONG dpitch, LONG spitch);
* Schluessel 0 (Normalfall), storem3 im Bytemodus.
*--------------------------------------------------------------------------

	xdef	_zod_ammx_blit_rect_k0

_zod_ammx_blit_rect_k0:
	movem.l	d2-d5/a2-a3,-(sp)	; 6 Register = 24 Byte auf dem Stapel
	move.l	24+4(sp),a2		; a2 = Zeilenanfang Ziel
	move.l	24+8(sp),a3		; a3 = Zeilenanfang Quelle
	move.l	24+12(sp),d2		; d2 = Breite
	move.l	24+16(sp),d3		; d3 = Hoehe
	move.l	24+20(sp),d4		; d4 = Zeilenabstand Ziel
	move.l	24+24(sp),d5		; d5 = Zeilenabstand Quelle

	tst.l	d3
	beq.b	.rk_fertig

.rk_zeile:
	movea.l	a2,a0
	movea.l	a3,a1
	move.l	d2,d0

.rk_acht:
	cmp.l	#8,d0
	blo.b	.rk_rest

	load	(a1)+,e0
	storem3	e0,d1,(a0)+		; d1 = MODUS 1, kein Registerinhalt

	subq.l	#8,d0
	bra.b	.rk_acht

.rk_rest:
	tst.l	d0
	beq.b	.rk_naechste

.rk_byte:
	tst.b	(a1)
	beq.b	.rk_weiter
	move.b	(a1),(a0)
.rk_weiter:
	addq.l	#1,a1
	addq.l	#1,a0
	subq.l	#1,d0
	bne.b	.rk_byte

.rk_naechste:
	adda.l	d4,a2
	adda.l	d5,a3
	subq.l	#1,d3
	bne.b	.rk_zeile

.rk_fertig:
	movem.l	(sp)+,d2-d5/a2-a3
	rts


*--------------------------------------------------------------------------
* void zod_ammx_blit_rect(UBYTE *dst, const UBYTE *src,
*                         ULONG w, ULONG h, LONG dpitch, LONG spitch,
*                         const UBYTE *key8);
* Allgemeiner Schluessel, pcmpeqb + storeilm.
*--------------------------------------------------------------------------

	xdef	_zod_ammx_blit_rect

_zod_ammx_blit_rect:
	movem.l	d2-d5/a2-a3,-(sp)

	move.l	24+28(sp),a0		; key8
	load	(a0),e1			; EINMAL je Blit, frueher je Zeile
	move.b	(a0),d1			; Schluesselbyte fuer den Rest der Zeile

	move.l	24+4(sp),a2
	move.l	24+8(sp),a3
	move.l	24+12(sp),d2
	move.l	24+16(sp),d3
	move.l	24+20(sp),d4
	move.l	24+24(sp),d5

	tst.l	d3
	beq.b	.rz_fertig

.rz_zeile:
	movea.l	a2,a0
	movea.l	a3,a1
	move.l	d2,d0

.rz_acht:
	cmp.l	#8,d0
	blo.b	.rz_rest

	load	(a1),e0
	pcmpeqb	e1,e0,e2
	storeilm e0,e2,(a0)

	addq.l	#8,a1
	addq.l	#8,a0
	subq.l	#8,d0
	bra.b	.rz_acht

.rz_rest:
	tst.l	d0
	beq.b	.rz_naechste

.rz_byte:
	cmp.b	(a1),d1
	beq.b	.rz_weiter
	move.b	(a1),(a0)
.rz_weiter:
	addq.l	#1,a1
	addq.l	#1,a0
	subq.l	#1,d0
	bne.b	.rz_byte

.rz_naechste:
	adda.l	d4,a2
	adda.l	d5,a3
	subq.l	#1,d3
	bne.b	.rz_zeile

.rz_fertig:
	movem.l	(sp)+,d2-d5/a2-a3
	rts

	end
