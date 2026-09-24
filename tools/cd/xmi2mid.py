#!/usr/bin/env python3
"""XMI (Miles/AIL) nach Standard-MIDI (SMF) umsetzen.

ZWECK: NUR ZUM ANHOEREN UND BEWERTEN. Dieses Werkzeug gehoert nicht ins
Auslieferungspaket -- im Produkt soll ZExtract die Umsetzung auf dem Amiga
aus der CD des Nutzers machen (die Musik gehoert den Bitmap Brothers, fertig
umgesetzte Dateien duerfen wir nicht verbreiten). Hier geht es allein um die
Frage: klingt das Original besser als die heutige OGG-Fassung, und lohnt der
Aufwand?

DAS FORMAT, am Bestand der Z-CD nachgeprueft:

    FORM <len> XDIR
      INFO <len> <anzahl:16 LE>      Zahl der Sequenzen
    CAT  <len> XMID
      FORM <len> XMID                je Sequenz
        TIMB <len> <n:16 LE> (patch,bank)*n
        RBRN <len> ...               Sprungmarken, hier nicht gebraucht
        EVNT <len> <ereignisse>

EVNT unterscheidet sich in ZWEI Punkten von Standard-MIDI, und beide sind
der Grund, warum man nicht einfach kopieren kann:

  1. Die Wartezeit ist KEINE variable Laenge, sondern eine Folge von Bytes
     kleiner 0x80, deren Werte ADDIERT werden. Ein Byte ab 0x80 beendet sie
     und ist der Statuscode.
  2. Es gibt KEIN Note-Off. Auf ein Note-On folgen Note, Anschlag und dann
     eine DAUER als variable Laenge. Das Note-Off muss daraus erzeugt und
     zeitrichtig einsortiert werden.

ZUR ZEITBASIS -- entschieden, nicht geraten.
Ein XMI-Schritt ist eine feste 1/120 Sekunde. Die Tempo-Ereignisse im Strom
beschreiben nur, wie der Takt zu LESEN ist; sie steuern die Zeit NICHT.

Belegt am Bestand: In data/game/assets/sounds liegen neun Stuecke bereits als
.MID (fremde Umsetzung, gleiche Notenzahl wie hier). Das Verhaeltnis der
Laengen ist in jedem Stueck mit nur einem Tempo EXAKT Tempo/500000 -- genau
der Faktor zwischen der festen Lesart und der tempoabhaengigen. Zweiter,
unabhaengiger Beleg: unter der festen Lesart sind die fuenf langen Fassungen
(AA1/AC1/AD1/AJ1/AV1 -- dieselbe Musik je Planet) alle 735,96 bis 736,03
Viertel lang; unter der tempoabhaengigen streuen sie von 681 bis 738.

Eine frueherer Stand dieser Datei waehlte die tempoabhaengige Lesart und
begruendete das mit einer Abnahme an AD1 gegen music_desert.ogg (+0,1 %).
AD1 laeuft mit Tempo 508474 -- das ist der eine Fall, in dem der Fehler
unsichtbar ist. Bei AOPTIONS (Tempo 674157) sind es 35 %.

Umgesetzt wird deshalb wie folgt: Die Wartezeiten bleiben in XMI-Schritten
stehen und werden beim Schreiben ueber die Tempokarte in MIDI-Schritte
gerechnet (Teilung 480, wie in der fremden Umsetzung). Die Tempo-Ereignisse
bleiben erhalten, damit die Notenwerte stimmen; die REALZEIT ergibt sich
dabei von selbst als xmi_schritte/120.

Aufruf:
    tools/cd/xmi2mid.py --ein <verzeichnis mit *.XMI> --aus <zielverzeichnis>
    tools/cd/xmi2mid.py --ein eine.XMI --aus /tmp
"""
import argparse
import pathlib
import struct
import sys


class Kaputt(Exception):
    pass


# ---------------------------------------------------------------- IFF lesen
def iff_chunks(daten, anfang, ende):
    """Liefert (kennung, inhalt_anfang, inhalt_ende) je Brocken.

    IFF-Brocken sind auf gerade Laenge aufgefuellt; das Fuellbyte zaehlt
    NICHT zur Laenge. Wer das vergisst, verrutscht ab dem ersten Brocken
    ungerader Laenge -- und das faellt erst weit spaeter auf.
    """
    p = anfang

    while p + 8 <= ende:
        kennung = daten[p:p + 4]
        laenge = struct.unpack('>I', daten[p + 4:p + 8])[0]
        inhalt = p + 8

        if inhalt + laenge > ende:
            raise Kaputt(f"Brocken {kennung!r} ragt ueber das Ende hinaus")

        yield kennung, inhalt, inhalt + laenge

        p = inhalt + laenge + (laenge & 1)


def sequenzen(daten):
    """Alle EVNT-Bereiche der Datei, in Reihenfolge."""
    gefunden = []

    def absteigen(a, e):
        for kennung, ia, ie in iff_chunks(daten, a, e):
            if kennung in (b'FORM', b'CAT '):
                # Hinter FORM/CAT steht eine weitere Kennung (XDIR/XMID)
                gefunden_typ = daten[ia:ia + 4]
                absteigen(ia + 4, ie)
                del gefunden_typ
            elif kennung == b'EVNT':
                gefunden.append((ia, ie))

    absteigen(0, len(daten))

    return gefunden


# ------------------------------------------------------------ VLQ (MIDI)
def vlq_lesen(daten, p):
    wert = 0

    for _ in range(4):
        b = daten[p]
        p += 1
        wert = (wert << 7) | (b & 0x7F)

        if not (b & 0x80):
            return wert, p

    raise Kaputt("variable Laenge laenger als 4 Byte")


def vlq_schreiben(wert):
    if wert < 0:
        raise Kaputt("negative Wartezeit")

    teile = [wert & 0x7F]
    wert >>= 7

    while wert:
        teile.append((wert & 0x7F) | 0x80)
        wert >>= 7

    return bytes(reversed(teile))


# ------------------------------------------------------------ EVNT zerlegen
def evnt_lesen(daten, a, e):
    """Liefert eine Liste (zeit, reihenfolge, rohbytes) -- absolute Zeiten."""
    ereignisse = []
    tempi = []      # (xmi-schritt, us je viertel)
    offen = []          # (endzeit, reihenfolge, note-off-bytes)
    zeit = 0
    nr = 0
    p = a

    def einreihen(t, roh):
        nonlocal nr
        ereignisse.append((t, nr, roh))
        nr += 1

    while p < e:
        # 1. Wartezeit: Folge von Bytes < 0x80, Werte werden ADDIERT
        while p < e and daten[p] < 0x80:
            zeit += daten[p]
            p += 1

        if p >= e:
            break

        status = daten[p]
        p += 1

        if status == 0xFF:
            typ = daten[p]
            p += 1
            laenge, p = vlq_lesen(daten, p)
            nutz = daten[p:p + laenge]
            p += laenge

            if typ == 0x2F:          # Spurende -- selbst erzeugen
                break

            if typ == 0x51 and laenge == 3:
                tempi.append((zeit, int.from_bytes(nutz, 'big')))

            einreihen(zeit, bytes([0xFF, typ]) + vlq_schreiben(laenge) + nutz)

        elif status in (0xF0, 0xF7):
            laenge, p = vlq_lesen(daten, p)
            nutz = daten[p:p + laenge]
            p += laenge
            einreihen(zeit, bytes([status]) + vlq_schreiben(laenge) + nutz)

        elif (status & 0xF0) == 0x90:
            note = daten[p]
            anschlag = daten[p + 1]
            p += 2
            dauer, p = vlq_lesen(daten, p)

            einreihen(zeit, bytes([status, note, anschlag]))

            # Note-Off als Note-On mit Anschlag 0 -- spart ein Statusbyte
            # und ist von jedem Abspieler richtig zu deuten.
            offen.append((zeit + dauer, nr, bytes([status, note, 0])))
            nr += 1

        elif (status & 0xF0) in (0xC0, 0xD0):
            einreihen(zeit, bytes([status, daten[p]]))
            p += 1

        elif (status & 0xF0) in (0x80, 0xA0, 0xB0, 0xE0):
            einreihen(zeit, bytes([status, daten[p], daten[p + 1]]))
            p += 2

        else:
            raise Kaputt(f"unbekannter Statuscode 0x{status:02X} bei {p - 1}")

    ereignisse.extend(offen)

    # Nach Zeit sortieren; bei gleicher Zeit bleibt die Erzeugungsreihenfolge
    # erhalten (stabil ueber die laufende Nummer).
    ereignisse.sort(key=lambda t: (t[0], t[1]))

    return ereignisse, tempi


# ------------------------------------------------------- Zeitbasis umrechnen
XMI_HZ = 120                    # ein XMI-Schritt ist 1/120 Sekunde, fest


def in_midi_schritte(ereignisse, tempi, teilung):
    """XMI-Schritte -> MIDI-Schritte, abschnittsweise mit dem gueltigen Tempo.

    Ein XMI-Schritt dauert 1/120 s. Ein MIDI-Schritt dauert tempo/teilung us.
    Also: midi = xmi * (1000000/120) / tempo * teilung.

    Gerechnet wird abschnittsweise, weil das Tempo mitten im Stueck wechseln
    kann; die REALZEIT bleibt dabei in jedem Fall xmi/120, unabhaengig davon,
    welche Tempi im Strom stehen.
    """
    marken = sorted(tempi) or [(0, 500000)]

    if marken[0][0] > 0:
        marken.insert(0, (0, 500000))

    aus = []
    k = 0
    tempo = marken[0][1]
    xmi_basis = 0
    midi_basis = 0.0
    faktor = 1e6 / XMI_HZ * teilung

    for zeit, nr, roh in ereignisse:
        while k + 1 < len(marken) and marken[k + 1][0] <= zeit:
            k += 1
            midi_basis += (marken[k][0] - xmi_basis) * faktor / tempo
            xmi_basis = marken[k][0]
            tempo = marken[k][1]

        aus.append((midi_basis + (zeit - xmi_basis) * faktor / tempo, nr, roh))

    return aus


# ------------------------------------------------------------ SMF schreiben
def smf_schreiben(ereignisse, tempi, teilung=480):
    ereignisse = in_midi_schritte(ereignisse, tempi, teilung)

    spur = bytearray()
    letzte = 0

    for zeit, _nr, roh in ereignisse:
        jetzt = int(round(zeit))

        # Rundung darf die Reihenfolge nie umkehren
        if jetzt < letzte:
            jetzt = letzte

        spur += vlq_schreiben(jetzt - letzte)
        spur += roh
        letzte = jetzt

    spur += vlq_schreiben(0) + b'\xFF\x2F\x00'      # Spurende

    kopf = b'MThd' + struct.pack('>IHHH', 6, 0, 1, teilung)

    return kopf + b'MTrk' + struct.pack('>I', len(spur)) + bytes(spur)


# ------------------------------------------------------------------- Ablauf
def umsetzen(pfad_ein, verz_aus):
    daten = pfad_ein.read_bytes()

    if daten[:4] != b'FORM':
        raise Kaputt("keine IFF-Datei")

    teile = sequenzen(daten)

    if not teile:
        raise Kaputt("kein EVNT gefunden")

    erzeugt = []

    for i, (a, e) in enumerate(teile):
        ereignisse, tempi = evnt_lesen(daten, a, e)
        name = pfad_ein.stem + ('' if len(teile) == 1 else f'_{i:02d}') + '.mid'
        ziel = verz_aus / name
        ziel.write_bytes(smf_schreiben(ereignisse, tempi))
        erzeugt.append((ziel, len(ereignisse)))

    return erzeugt


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--ein', required=True)
    ap.add_argument('--aus', required=True)
    args = ap.parse_args()

    quelle = pathlib.Path(args.ein)
    ziel = pathlib.Path(args.aus)
    ziel.mkdir(parents=True, exist_ok=True)

    dateien = sorted(quelle.glob('*.XMI')) if quelle.is_dir() else [quelle]

    if not dateien:
        print("keine *.XMI gefunden", file=sys.stderr)
        return 2

    gut = schlecht = 0
    ereignisse_gesamt = 0

    for f in dateien:
        try:
            for z, n in umsetzen(f, ziel):
                ereignisse_gesamt += n
                gut += 1
        except (Kaputt, IndexError, struct.error) as fehler:
            print(f"  FEHLER {f.name}: {fehler}", file=sys.stderr)
            schlecht += 1

    print(f"{gut} MIDI-Dateien erzeugt, {schlecht} Fehler, "
          f"{ereignisse_gesamt} Ereignisse gesamt")

    return 1 if schlecht else 0


if __name__ == '__main__':
    sys.exit(main())
