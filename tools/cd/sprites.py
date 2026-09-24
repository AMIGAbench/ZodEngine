#!/usr/bin/env python3
"""Leser für SPRITES.RSC -- die Grafikbank der Original-CD von Z.

SPRITES.RSC liegt gepackt in z.pac (3 920 557 Byte entpackt) und enthält die
gesamte Spielgrafik. Der Aufbau ist am 18.09. erschlossen worden; die Probe ist
das Auge: Die ersten entschlüsselten Sätze zeigen lesbar "EDIT", "DELETE
ROBOT", "SAVE ROBOTS" und Fahrzeugsprites.

Aufbau der Datei
----------------
    0      u16   Zahl der Sätze (7211)
    2      u32   je Satz ein Versatz in diese Datei, aufsteigend
    2+4n   n     ein Byte je Satz -- Gruppen-/Typkennung
           768   VGA-Palette, 6 Bit je Kanal

Die Reihenfolge ist belegt, nicht geraten: In dieser Anordnung schöpft die
Palette das VGA-Raster voll aus (Maximum **63**), in der umgekehrten kommt sie
über 40 nicht hinaus und die Gruppenbytes zeigen keine Läufe.
    dann   die Sätze

Die Lücke zwischen Versatztabelle und erstem Satz ist exakt 768 + n Byte
gross -- daran ist der Aufbau aufgefallen.

Aufbau eines Satzes
-------------------
    0      u8    Höhe
    1      u8    Breite geteilt durch 8
    2      h     ein Byte je Zeile (Bedeutung unklar, meist Breite/8)
    ...          Kopfrest und Füllung, 0 bis 3 Byte
    Ende−w·h     die Pixel

**Die Pixel sind die LETZTEN w·h Byte des Satzes.** Das ist die ganze Regel,
und sie ist die zweite Fassung: Zuerst stand hier ein fester Kopf von `h+2`
bzw. `h+4` Byte, je nach Schlupf `len − 2 − h − w·h`. Das war für 6183 Sätze
richtig und für **1028 um genau ein Byte falsch** -- nämlich überall dort, wo
der Schlupf ungerade ist. Die Füllung liegt eben **vorn**, zwischen Kopf und
Pixeln, nicht hinten.

Das Tückische daran: Ein um ein Byte verschobener Satz *sieht aus wie ein
Satz*. Er hat die richtige Größe, die Längenprüfung geht auf, und weil in der
Mode-X-Ebenenfolge ein Byte Versatz die Spalten nur umsortiert, blieben grobe
Formen (Schriftzüge, Fahrzeugumrisse) erkennbar. Aufgefallen ist es erst, als
eine fehlende Datei **byteweise im Rohstrom gesucht** wurde: Sie stand dort,
also war nicht die CD unvollständig, sondern der Leser falsch.

**Merke:** Wenn eine Datei fehlt, erst im Rohstrom nach ihren Bytes suchen,
bevor man die Quelle für unvollständig erklärt.

Die Breitenkennung ist belegt: 1 -> 8, 2 -> 16, 4 -> 32, 8 -> 64.

Die Pixel liegen in **Mode-X-Ebenenfolge**: Ebene p enthält alle Spalten mit
x mod 4 == p, je Zeile w/4 Byte, alle Zeilen einer Ebene hintereinander.

    pixel(x, y) = daten[(x mod 4) * (w/4 * h) + y * (w/4) + x div 4]

Das war der Knackpunkt: Linear gelesen zeigt jedes Bild eine Wiederholung alle
vier Spalten -- die Signatur der vier Schreibebenen des VGA-Modus X.

Probe
-----
**7211 von 7211 Sätzen** lesbar. Die Gegenprobe ist das Auge: Die Sätze der
zweiten Art zeigen zwei weiße Handzeiger und zweimal den Schriftzug "LOADING",
die der ersten "EDIT ROBOT", "DELETE ROBOT", "SAVE ROBOTS" und Fahrzeuge.
Das bestätigt Palette und Ebenenentflechtung zugleich.

Aufruf
------
    tools/cd/sprites.py --dump              Kopf und Kennzahlen
    tools/cd/sprites.py --png <verzeichnis> [--max N]   Sätze als PNG
"""
import argparse
import collections
import pathlib
import struct
import sys

sys.path.insert(0, str(pathlib.Path(__file__).parent))

from zpac import ZPac                       # noqa: E402
from nicecomp import entpacke_eintrag       # noqa: E402

PALETTE_BYTES = 768


class Sprites:
    def __init__(self, daten):
        self.d = daten
        self.count = struct.unpack("<H", daten[:2])[0]
        self.offsets = [struct.unpack("<I", daten[2 + i * 4:6 + i * 4])[0]
                        for i in range(self.count)]
        self.offsets.append(len(daten))

        tab_ende = 2 + self.count * 4
        self.gruppe = daten[tab_ende:tab_ende + self.count]
        self.palette = daten[tab_ende + self.count:
                             tab_ende + self.count + PALETTE_BYTES]

    def rgb(self):
        """Palette auf 8 Bit gestreckt, wie im Bestand: (x<<2)|3."""
        out = []

        for i in range(256):
            for k in range(3):
                c = self.palette[i * 3 + k]
                out.append(((c << 2) | 3) if c else 0)

        return out

    def satz(self, r):
        """(Breite, Hoehe, Pixel) oder None, wenn der Satz nicht passt."""
        roh = self.d[self.offsets[r]:self.offsets[r + 1]]

        if len(roh) < 4:
            return None

        h = roh[0]
        w = roh[1] * 8

        if h == 0 or w == 0:
            return None

        # Die Pixel sind die LETZTEN w*h Byte des Satzes. Alles davor ist
        # Kopf und Fuellung -- die Fuellung liegt VORN, nicht hinten.
        if len(roh) < 2 + h + w * h:
            return None

        daten = roh[len(roh) - w * h:]

        # Mode-X entflechten
        px = bytearray(w * h)
        sp = w // 4

        for y in range(h):
            for x in range(w):
                px[y * w + x] = daten[(x % 4) * (sp * h) + y * sp + x // 4]

        return w, h, bytes(px)


def lade(cd):
    pac = ZPac(pathlib.Path(cd) / "z.pac")
    e = [x for x in pac.entries if x.dos_name == "SPRITES.RSC"]

    if not e:
        sys.exit("SPRITES.RSC nicht im Archiv")

    return Sprites(entpacke_eintrag(pac, e[0]))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--cd", default="/media/cem/Z_280796")
    ap.add_argument("--dump", action="store_true")
    ap.add_argument("--png", metavar="VERZ")
    ap.add_argument("--max", type=int, default=64)
    args = ap.parse_args()

    s = lade(args.cd)

    if args.png:
        from PIL import Image

        ziel = pathlib.Path(args.png)
        ziel.mkdir(parents=True, exist_ok=True)
        rgb = s.rgb()
        n = 0

        for r in range(s.count):
            t = s.satz(r)

            if not t:
                continue

            w, h, px = t
            im = Image.frombytes("P", (w, h), px)
            im.putpalette(rgb)
            im.save(ziel / f"{r:05d}_{w}x{h}.png")
            n += 1

            if n >= args.max:
                break

        print(f"{n} Saetze nach {ziel} geschrieben")
        return

    lesbar = sum(1 for r in range(s.count) if s.satz(r))
    masse = collections.Counter()

    for r in range(s.count):
        t = s.satz(r)

        if t:
            masse[(t[0], t[1])] += 1

    print(f"Saetze:          {s.count}")
    print(f"davon lesbar:    {lesbar}")
    print(f"Palette:         Maximum {max(s.palette)} (6 Bit)")
    print(f"Gruppenkennung:  {len(set(s.gruppe))} verschiedene Werte, "
          f"{min(s.gruppe)}..{max(s.gruppe)}")
    print("haeufigste Masse:", masse.most_common(8))


if __name__ == "__main__":
    main()
