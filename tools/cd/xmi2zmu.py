#!/usr/bin/env python3
"""XMI (Miles/AIL) in den Abspielstrom ZMU1 umsetzen.

DIESES SKRIPT IST DIE REFERENZ, NICHT DER AUSLIEFERUNGSWEG. Im Produkt macht
ZExtract dasselbe in C auf dem Amiga, aus der CD des Nutzers. Damit sich das
belegen laesst, muss die C-Fassung spaeter BYTEWEISE dieselbe Datei erzeugen
wie dieses Skript -- so wie es beim Bild- und Klangextraktor schon gemacht
wurde.

WARUM EIN EIGENES FORMAT UND NICHT EINFACH MIDI:
Ein Abspieler auf dem Amiga soll nicht parsen muessen. Standard-MIDI braucht
variable Laengen, laufenden Status, Tempokarte und eine Teilungsrechnung mit
Bruechen. Nichts davon wird gebraucht, denn:

    Ein XMI-Schritt ist eine feste 1/120 Sekunde.

Das ist heute an den neun fremden .MID im Asset-Bestand belegt worden (das
Laengenverhaeltnis war in jedem Stueck mit nur einem Tempo exakt
Tempo/500000). Der Abspieler zaehlt also schlicht 120 Schritte je Sekunde und
braucht weder Tempo noch Fliesskomma.

AUFBAU (alles Big-Endian, wie auf dem Amiga natuerlich):

    0   'ZMU1'
    4   u16  Takt in Hz            (120)
    6   u16  Reserve              (0)
    8   u32  Zahl der Ereignisse
    12  u32  Gesamtlaenge in Schritten
    16  Ereignisstrom

Je Ereignis zuerst die Wartezeit, dann die Art:

    u8  Wartezeit   0..254 Schritte; 255 = danach u16 mit der echten Zahl
    u8  Art
        0 Note an   : u8 Kanal, u8 Note, u8 Anschlag
        1 Note aus  : u8 Kanal, u8 Note
        2 Programm  : u8 Kanal, u8 Programm
        3 Lautstaerke (CC7) : u8 Kanal, u8 Wert
        4 Ende      : keine Daten
        5 nur warten: keine Daten (nur fuer Pausen ueber 65535 Schritte)

Kanal 9 ist wie in General MIDI das Schlagzeug; dort ist die Note der
Instrumentenplatz, nicht die Tonhoehe.

Was ABSICHTLICH wegfaellt: Tempo (siehe oben), Pitch Bend, Aftertouch, alle
uebrigen Controller, Sysex. Gemessen am Bestand der Z-CD kostet das nichts --
die Stuecke benutzen nur Programmwechsel und CC7.

Aufruf:
    tools/cd/xmi2zmu.py --ein <verzeichnis oder datei> --aus <zielverzeichnis>
"""
import argparse
import pathlib
import struct
import sys

import xmi2mid


TAKT_HZ = 120

ART_AN, ART_AUS, ART_PROG, ART_VOL, ART_ENDE, ART_WARTEN = 0, 1, 2, 3, 4, 5


class Kaputt(Exception):
    pass


def ereignisse_sammeln(daten, a, e):
    """Aus einem EVNT-Bereich die Ereignisse in XMI-Schritten gewinnen.

    Benutzt denselben Leser wie der MIDI-Umsetzer -- das Format wird an EINER
    Stelle gedeutet, sonst laufen die beiden Wege auseinander.
    """
    roh, _tempi = xmi2mid.evnt_lesen(daten, a, e)
    aus = []

    for zeit, nr, bytes_ in roh:
        s = bytes_[0]
        hoch = s & 0xF0
        kanal = s & 0x0F

        if hoch == 0x90:
            note, anschlag = bytes_[1], bytes_[2]

            # Der XMI-Leser erzeugt das Note-Off bereits als Note-On mit
            # Anschlag 0 -- hier wieder auseinandernehmen.
            if anschlag:
                aus.append((zeit, nr, ART_AN, (kanal, note, anschlag)))
            else:
                aus.append((zeit, nr, ART_AUS, (kanal, note)))

        elif hoch == 0x80:
            aus.append((zeit, nr, ART_AUS, (kanal, bytes_[1])))

        elif hoch == 0xC0:
            aus.append((zeit, nr, ART_PROG, (kanal, bytes_[1])))

        elif hoch == 0xB0 and bytes_[1] == 7:
            aus.append((zeit, nr, ART_VOL, (kanal, bytes_[2])))

    aus.sort(key=lambda t: (t[0], t[1]))

    return aus


def strom_bauen(ereignisse):
    strom = bytearray()
    letzte = 0
    anzahl = 0

    for zeit, _nr, art, d in ereignisse:
        delta = zeit - letzte

        if delta < 0:
            raise Kaputt("Ereignisse nicht in Zeitfolge")

        # Pausen ueber 65535 Schritte (9 Minuten) in Stuecke zerlegen, jedes
        # mit einem reinen Warteereignis abgeschlossen. Kommt im Bestand der
        # Z-CD nicht vor, aber ein Format, das an einer Stelle einfach
        # abbricht, ist kein Format.
        while delta > 0xFFFF:
            strom += bytes([255]) + struct.pack('>H', 0xFFFF)
            strom += bytes([ART_WARTEN])
            delta -= 0xFFFF
            anzahl += 1

        if delta < 255:
            strom += bytes([delta])
        else:
            strom += bytes([255]) + struct.pack('>H', delta)

        strom += bytes([art]) + bytes(d)
        letzte = zeit
        anzahl += 1

    strom += bytes([0, ART_ENDE])
    anzahl += 1

    return bytes(strom), anzahl, letzte


def umsetzen(pfad_ein, verz_aus):
    daten = pfad_ein.read_bytes()

    if daten[:4] != b'FORM':
        raise Kaputt("keine IFF-Datei")

    teile = xmi2mid.sequenzen(daten)

    if not teile:
        raise Kaputt("kein EVNT gefunden")

    erzeugt = []

    for i, (a, e) in enumerate(teile):
        er = ereignisse_sammeln(daten, a, e)
        strom, anzahl, ende = strom_bauen(er)

        kopf = b'ZMU1' + struct.pack('>HHII', TAKT_HZ, 0, anzahl, ende)
        name = pfad_ein.stem + ('' if len(teile) == 1 else f'_{i:02d}') + '.zmu'
        ziel = verz_aus / name
        ziel.write_bytes(kopf + strom)
        erzeugt.append((ziel, anzahl, ende))

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
    byte_gesamt = 0

    for f in dateien:
        try:
            for z, n, ende in umsetzen(f, ziel):
                byte_gesamt += z.stat().st_size
                gut += 1
                print(f"  {z.name:16s} {n:6d} Ereignisse, "
                      f"{ende / TAKT_HZ:7.1f} s, {z.stat().st_size:7d} Byte")
        except (Kaputt, IndexError, struct.error) as fehler:
            print(f"  FEHLER {f.name}: {fehler}", file=sys.stderr)
            schlecht += 1

    print(f"{gut} Stroeme, {schlecht} Fehler, zusammen {byte_gesamt / 1024:.0f} KB")

    return 1 if schlecht else 0


if __name__ == '__main__':
    sys.exit(main())
