; musik_kern.s -- der innere Kern des Musikmischers in 68k-Assembler
;
; Ersetzt die Stimmschleife aus musik_mischer.c. Die C-Fassung BLEIBT und
; ist der Rueckfall; welche laeuft, entscheidet ZOD_MIX_ASM zur Bauzeit und
; ein Selbsttest zur Laufzeit. Dieselbe Aufteilung wie beim AMMX-Blitter,
; und aus demselben Grund: ein Assemblerkern ohne vergleichbare C-Fassung
; ist nicht pruefbar.
;
; REINES 68020 -- kein AMMX, kein 68080-Befehl. Das Modul wird in ALLE drei
; CPU-Baeume gebunden, nicht nur in den 080er.
;
; VERTRAG mit dem C-Teil (Reihenfolge des Parameterblocks):
;
;    0(a0)  LONG *ziel        32-Bit-Mischpuffer, verschraenkt L,R
;    4(a0)  BYTE *proben      Probendaten, vorzeichenbehaftet
;    8(a0)  ULONG pos         16.16 Leseposition
;   12(a0)  ULONG schritt     16.16 Vorschub je Rahmen
;   16(a0)  ULONG ende        16.16 Probenende
;   20(a0)  ULONG schleife    16.16 Schleifenanfang, 0 = einmalig
;   24(a0)  LONG  vl          Lautstaerke links, 0..256
;   28(a0)  LONG  vr          Lautstaerke rechts
;
;   ULONG zod_mix_kern(const LONG *block, ULONG rahmen);
;
;   Rueckgabe: neue Leseposition, oder $FFFFFFFF, wenn die Stimme zu Ende
;   ist. $FFFFFFFF kann keine gueltige Position sein: ende ist hoechstens
;   65535<<16 = $FFFF0000, und pos bleibt darunter.
;
; Die Rechnung ist GENAU die der C-Fassung, damit der Selbsttest byteweise
; vergleichen kann:
;
;   idx  = pos >> 16
;   a    = proben[idx]
;   b    = (idx+1 < probenzahl) ? proben[idx+1]
;                               : (schleife ? proben[schleife>>16] : a)
;   p    = a + (((b - a) * ((pos >> 8) & $FF)) >> 8)
;   *z++ += p * vl
;   *z++ += p * vr
;   pos += schritt
;
; ZWEI ENTSCHEIDUNGEN, die den Kern ohne einen einzigen Speicherzugriff je
; Abtastwert auskommen lassen (ausser Probe lesen und Puffer schreiben):
;
;  - Der Rahmenzaehler ist ein ZEIGER auf das Pufferende (a0). Ein Zaehler
;    in einem Adressregister ginge nicht: subq auf An setzt keine Flags.
;  - Die Bruchrechnung parkt pos in a6 statt auf dem Stapel. Bewegungen
;    zwischen Adress- und Datenregister sind einzelne Befehle.
;
; Ein erster Entwurf schob beides ueber den Stapel -- vier Speicherzugriffe
; je Abtastwert, die nichts mit der Arbeit zu tun haben.

		machine	68020

		xdef	_zod_mix_kern

		section	text,code

_zod_mix_kern:
		movem.l	d2-d7/a2-a6,-(sp)	; 11 Register = 44 Byte

		movea.l	48(sp),a1		; 44 + 4 Ruecksprungadresse
		move.l	52(sp),d0		; rahmen

		movea.l	4(a1),a2		; proben
		move.l	8(a1),d1		; pos
		move.l	12(a1),d2		; schritt
		move.l	16(a1),d3		; ende
		move.l	20(a1),d4		; schleife
		move.l	24(a1),d5		; vl
		move.l	28(a1),d6		; vr
		movea.l	(a1),a1			; ziel -- zuletzt, a1 wird ueberschrieben

		; Pufferende: ziel + rahmen * 8 (zwei Langworte je Rahmen)
		move.l	d0,d7
		lsl.l	#3,d7
		movea.l	a1,a0
		adda.l	d7,a0			; a0 = Ende des Mischpuffers

		; rund = ende - schleife. Ist sie null, ist die "Schleife"
		; keine -- das wird EINMAL hier geklaert, nicht je Abtastwert.
		move.l	d3,d7
		sub.l	d4,d7
		beq.b	.keine_schleife
		movea.l	d7,a3			; rund
		bra.b	.weiter

.keine_schleife:
		moveq	#0,d4			; wie einmalig behandeln
		movea.l	#1,a3

.weiter:	move.l	d3,d7
		lsr.l	#8,d7
		lsr.l	#8,d7
		movea.l	d7,a4			; probenzahl = ende >> 16

		move.l	d4,d7
		lsr.l	#8,d7
		lsr.l	#8,d7
		movea.l	d7,a5			; schleifenindex = schleife >> 16

		cmpa.l	a0,a1
		bhs	.raus

;------------------------------------------------------------------ Schleife
.loop:
		cmp.l	d3,d1			; pos >= ende ?
		blo.b	.drin

		tst.l	d4			; geschleift?
		beq	.fertig

.wrap:		sub.l	a3,d1
		cmp.l	d3,d1
		bhs.b	.wrap

;--- a = proben[idx] --------------------------------------------------------
.drin:		move.l	d1,d7
		lsr.l	#8,d7
		lsr.l	#8,d7			; d7 = idx

		movea.l	d7,a6
		move.b	(a2,a6.l),d7
		ext.w	d7
		ext.l	d7			; d7 = a

;--- b bestimmen ------------------------------------------------------------
		addq.l	#1,a6			; idx + 1
		cmpa.l	a4,a6			; idx+1 < probenzahl ?
		blo.b	.bnormal

		tst.l	d4
		beq.b	.bgleich		; einmalig -> b = a
		movea.l	a5,a6			; geschleift -> b am Schleifenanfang

.bnormal:	move.b	(a2,a6.l),d0
		ext.w	d0
		ext.l	d0			; d0 = b
		bra.b	.interp

.bgleich:	move.l	d7,d0			; b = a

;--- p = a + (((b - a) * frac) >> 8) ---------------------------------------
.interp:	sub.l	d7,d0			; d0 = b - a
		movea.l	d1,a6			; pos parken (kein Stapel)
		lsr.l	#8,d1
		andi.l	#$FF,d1			; d1 = frac
		muls.l	d1,d0
		move.l	a6,d1			; pos zurueck
		asr.l	#8,d0
		add.l	d7,d0			; d0 = p

;--- mischen ----------------------------------------------------------------
		move.l	d0,d7
		muls.l	d5,d0			; p * vl
		add.l	d0,(a1)+
		muls.l	d6,d7			; p * vr
		add.l	d7,(a1)+

		add.l	d2,d1			; pos += schritt

		cmpa.l	a0,a1
		blo	.loop

.raus:		move.l	d1,d0			; neue Position
		movem.l	(sp)+,d2-d7/a2-a6
		rts

.fertig:	moveq	#-1,d0			; $FFFFFFFF = Stimme zu Ende
		movem.l	(sp)+,d2-d7/a2-a6
		rts
