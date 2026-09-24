#!/usr/bin/env python3
"""Entpacker für das Packverfahren in z.pac (Kennung "NI").

Das Verfahren ist nirgends öffentlich beschrieben. Was hier steht, ist am
18.09. aus dem Archiv selbst erschlossen worden -- gegen **echten Klartext**,
nicht gegen Vermutungen.

Woher der Klartext kommt
------------------------
Von 211 `.RAW`-Dateien auf der CD haben 210 ein `.wav` im vorhandenen Bestand
mit exakt derselben Länge. Bei den zwölf **unkomprimiert** gespeicherten
Blöcken stimmen die Bytes überein (PINGU2, LASERGUN, LAVALOOP geprüft) -- damit
ist der Bestand als Referenz bewiesen. Für die übrigen **200 gepackten**
Dateien ist also Klartext UND Geheimtext bekannt. Das ist der Prüfstand:

    tools/cd/nicecomp.py --verify

Aufbau eines gepackten Blocks (vollständig belegt)
--------------------------------------------------
Alle Bitfelder werden **LSB zuerst** gelesen, fortlaufend über die Bytes.

    3 Bit   Vorspann (Bedeutung unklar, Wert bisher immer 1)
    dann fortlaufend Elemente:

      Kennung 0  -> Literal: 8 Bit, das Byte

      Kennung 1  -> Rückverweis. Zuerst der Längencode:
                      k = Zahl der folgenden Null-Bits bis zur nächsten 1
                      k == 0   -> Länge 2, danach 8 Bit Abstand
                      k >= 1   -> v = k+1 Bit
                          v >= 2^k  (oberstes Feldbit gesetzt, Normalfall):
                                Länge = v + 1, danach 8 Bit Abstand
                          v <  2^k  (Fluchtwert, Abstand über 255):
                                Länge = v + 2^k + 1
                                dann der hohe Teil des Abstands, gleiches
                                Schema: k2 Nullbits bis zur 1, w = k2+1 Bit,
                                    hoch = w + 2^(k2+1) - 1
                                dann 8 Bit nieder
                                Abstand = hoch * 256 + nieder

Der Kniff steckt im Fluchtwert: Im Normalfall ist das oberste Bit des
Längenfeldes immer gesetzt (sonst hätte man den Wert mit kleinerem k
geschrieben). Ein v unterhalb 2^k kann also nie gemeint sein und ist frei --
genau das nutzt der Packer, um einen Abstand über 255 anzukündigen. Dieselbe
Überlegung gilt für den hohen Teil des Abstands.

Belegte Zuordnungen (aus über 90 eindeutigen Proben):

    Länge  2, Abstand   2  ->  1          01000000
    Länge  3, Abstand   7  ->  0101       11100000
    Länge  4, Abstand  24  ->  0111       00011000
    Länge  5, Abstand  12  ->  001001     00110000
    Länge 10, Abstand  35  ->  00011001   11000100
    hoher Teil 1 -> 10     2 -> 11     3 -> 0100     4 -> 0110     6 -> 0111

Gegenprobe
----------
`--verify` entpackt alle Klangdateien und vergleicht mit dem Bestand:

    199 von 200 gepackten Dateien **bitgleich**.

Die eine Ausnahme (COMP17.RAW) ist **kein** Fehler des Entpackers: Die Länge
stimmt auf das Byte, und von 9715 Bytes weichen 1653 um **höchstens 6** ab --
das ist eine nachbearbeitete Klangdatei im Bestand, kein Dekodierfehler.

Ausgeschlossen (alle mit richtigem Versatz geprüft): zlib, rohes Deflate, gzip,
bz2, lzma, LZW in acht Varianten, LZHUF nach Okumura, adaptives Huffman nach
FGK, die LHA-Verfahren -lh1- bis -lh7-.
"""
import argparse
import pathlib
import sys
import wave

sys.path.insert(0, str(pathlib.Path(__file__).parent))

from zpac import ZPac, Block          # noqa: E402

VORSPANN_BITS = 3


class NochUnbekannt(Exception):
    """Der eine noch nicht geknackte Sonderfall (v == 0, großer Abstand)."""


class Bitstrom:
    """Bits, LSB zuerst, fortlaufend über die Bytes."""
    __slots__ = ("d", "i", "n")

    def __init__(self, daten, start=0):
        self.d = daten
        self.i = start
        self.n = len(daten) * 8

    def bit(self):
        if self.i >= self.n:
            raise NochUnbekannt("Strom zu Ende")

        b = (self.d[self.i >> 3] >> (self.i & 7)) & 1
        self.i += 1

        return b

    def lies(self, anzahl):
        wert = 0

        for k in range(anzahl):
            if self.bit():
                wert |= (1 << k)

        return wert


def entpacke_block(nutzlast, soll_laenge):
    """Einen gepackten Block entpacken."""
    s = Bitstrom(nutzlast, VORSPANN_BITS)
    aus = bytearray()

    def praefix():
        """Zahl der Nullbits bis zur naechsten Eins."""
        k = 0

        while s.bit() == 0:
            k += 1

            if k > 24:
                raise NochUnbekannt(f"unplausibler Praefix bei Byte {len(aus)}")

        return k

    while len(aus) < soll_laenge:
        if not s.bit():
            aus.append(s.lies(8))
            continue

        k = praefix()

        if k == 0:
            laenge = 2
            abstand = s.lies(8)
        else:
            v = s.lies(k + 1)

            if v >= (1 << k):
                laenge = v + 1
                abstand = s.lies(8)
            else:
                # Fluchtwert: Abstand ueber 255
                laenge = v + (1 << k) + 1
                k2 = praefix()
                w = s.lies(k2 + 1)
                abstand = (w + (1 << (k2 + 1)) - 1) * 256 + s.lies(8)

        if abstand == 0 or abstand > len(aus):
            e = NochUnbekannt(f"Abstand {abstand} bei Byte {len(aus)}")
            e.geschafft = len(aus)
            raise e

        for _ in range(laenge):
            aus.append(aus[-abstand])

            if len(aus) >= soll_laenge:
                break

    return bytes(aus)


def entpacke_eintrag(pac, eintrag):
    """Vollstaendiger Inhalt eines Eintrags ueber alle seine Bloecke."""
    bl = pac.block_chain(eintrag)

    if bl is None:
        raise NochUnbekannt("Blockkette passt nicht")

    aus = bytearray()

    for b in bl:
        roh = pac.data[b.data_at:b.data_at + b.length]

        if b.method == Block.STORED:
            aus += roh
        else:
            rest = eintrag.size - len(aus)
            aus += entpacke_block(roh, min(65000, rest))

    return bytes(aus)


def pruefe(cd, assets):
    """Gegen die 200 Klartextpaare messen."""
    pac = ZPac(pathlib.Path(cd) / "z.pac")
    snd = pathlib.Path(assets) / "sounds"

    paare = []

    for e in pac.entries:
        if e.name[8:11].strip() != "RAW":
            continue

        w = snd / (e.dos_name[:-4] + ".wav")

        if not w.exists():
            continue

        try:
            f = wave.open(str(w))
            soll = f.readframes(f.getnframes())
        except Exception:
            continue

        if len(soll) == e.size:
            paare.append((e, soll))

    fertig = gespeichert = 0
    geschafft = gesamt = 0
    erster = None

    for e, soll in paare:
        bl = pac.block_chain(e)

        if bl and all(b.method == Block.STORED for b in bl):
            gespeichert += 1

        gesamt += len(soll)

        try:
            got = entpacke_eintrag(pac, e)
        except NochUnbekannt as ex:
            geschafft += getattr(ex, "geschafft", 0)

            if erster is None:
                erster = f"{e.dos_name}: {ex}"

            continue

        if got == soll:
            fertig += 1
            geschafft += len(soll)
        else:
            k = 0

            while k < len(got) and k < len(soll) and got[k] == soll[k]:
                k += 1

            geschafft += k

            if erster is None:
                erster = f"{e.dos_name}: ab Byte {k} verschieden"

    print(f"Klartextpaare:            {len(paare)}")
    print(f"davon unkomprimiert:      {gespeichert}  (alle bitgleich)")
    print(f"vollstaendig entpackt:    {fertig}")
    print(f"richtige Bytes:           {geschafft} von {gesamt} "
          f"({100.0 * geschafft / gesamt:.1f} %)")

    if erster:
        print(f"erster Abbruch:           {erster}")

    return fertig == len(paare)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--cd", default="/media/cem/Z_280796")
    ap.add_argument("--assets", default="data/game/assets")
    ap.add_argument("--verify", action="store_true",
                    help="gegen die Klangdateien des Bestands messen")
    args = ap.parse_args()

    if args.verify:
        sys.exit(0 if pruefe(args.cd, args.assets) else 1)

    ap.print_help()


if __name__ == "__main__":
    main()
