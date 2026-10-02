#!/usr/bin/env python3
"""Erzeugt die eingebetteten Tabellen des Amiga-Extraktors.

`ZExtract` soll eine einzelne Datei sein: CD einlegen, starten, fertig. Also
wandern `names.txt`, `sounds.txt` und `xlat.txt` als C-Quelle ins Programm.

    tools/cd/gen_tables.py        schreibt tools/cd/amiga/tables.c

Die Namen liegen als **ein** durchgehender Block mit Nullbytes dazwischen, nicht
als 4053 einzelne Zeichenkettenkonstanten -- das spart ebenso viele Zeiger und
Umlagerungen in der Hunk-Datei.

Alle Strukturen sind ein Vielfaches von 4 Byte lang. Das ist auf dieser
Werkzeugkette kein Schönheitsfehler, sondern Vorsicht: Eine Struktur mit
ungerader Größe modulo 4 hat hier schon einmal einen Übersetzerfehler ausgelöst
(die schwerste Falle dieser Werkzeugkette).
"""
import argparse
import pathlib
import sys

HIER = pathlib.Path(__file__).parent

# Reihenfolge wie tools/assets/pack.py sie anlegt, plus misc.
GRUPPEN = ["buildings", "cursors", "fonts", "misc", "other", "planets",
           "teams", "units", "sounds"]

# Reihenfolge wie in tools/assets/palette.py und tools/cd/extract.py. Sie ist
# nicht beliebig: Ein Pfad kann zwei Planetenwoerter enthalten, dann gewinnt
# das erste dieser Liste -- in beiden Extraktoren dasselbe.
PLANETEN = ["desert", "volcanic", "arctic", "jungle", "city"]


def gruppe_von(pfad):
    teile = pfad.split("/")

    if len(teile) < 3:
        return "misc"

    return teile[1] if teile[1] in GRUPPEN else "misc"


def lies(pfad, felder):
    aus = []

    for z in pathlib.Path(pfad).read_text().splitlines():
        z = z.strip()

        if z and not z.startswith("#"):
            aus.append(z.split(None, felder - 1))

    return aus


class Namen:
    """Ein Block, Nullbytes dazwischen; gleiche Namen nur einmal."""

    def __init__(self):
        self.blob = bytearray()
        self.wo = {}

    def __call__(self, s):
        if s not in self.wo:
            self.wo[s] = len(self.blob)
            self.blob += s.encode("latin1") + b"\0"

        return self.wo[s]


def c_block(daten, je_zeile=16):
    aus = []

    for i in range(0, len(daten), je_zeile):
        aus.append("\t" + " ".join(f"0x{b:02x}," for b in daten[i:i + je_zeile]))

    return "\n".join(aus)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--namen", default=str(HIER / "names.txt"))
    ap.add_argument("--klaenge", default=str(HIER / "sounds.txt"))
    ap.add_argument("--umsetzung", default=str(HIER / "xlat.txt"))
    ap.add_argument("--ziel", default=str(HIER / "amiga" / "tables.c"))
    args = ap.parse_args()

    # --- Umsetztabellen: Reihenfolge festlegen, "-" ist Nummer 0 ---------
    xlat = lies(args.umsetzung, 2)
    xlat_ord = ["-"] + [p for p in PLANETEN if p in dict(xlat)]
    xlat_daten = {g: bytes.fromhex(h) for g, h in xlat}

    if "-" not in xlat_daten:
        sys.exit("xlat.txt: Grundtabelle '-' fehlt")

    def xlat_nr(pfad):
        rel = pfad[len("assets/"):].lower()

        for i, g in enumerate(xlat_ord):
            if g == "-":
                continue

            if rel.startswith("planets/") and g in rel:
                return i

            if "_" + g in rel or "/" + g + "_" in rel:
                return i

        return 0

    n = Namen()
    bilder = []

    for satz, w, h, pfad in lies(args.namen, 4):
        bilder.append((n(pfad), int(satz), int(w), int(h),
                       GRUPPEN.index(gruppe_von(pfad)), xlat_nr(pfad)))

    klaenge = []

    for dos, rate, pfad in lies(args.klaenge, 3):
        stamm, _, endung = dos.partition(".")
        # so, wie der Name im Verzeichnis von z.pac steht: 8+3, aufgefuellt
        roh = f"{stamm:<8}{endung:<3} "
        klaenge.append((n(pfad), int(rate), roh))

    with open(args.ziel, "w") as f:
        f.write('/* Erzeugt von tools/cd/gen_tables.py -- NICHT von Hand aendern.\n'
                ' *\n'
                ' * Zuordnung der Saetze aus SPRITES.RSC zu den Dateinamen der\n'
                ' * Engine, dazu die Klaenge und die Farbumsetzung. Woher die\n'
                ' * Zahlen stammen und womit sie belegt sind, steht in\n'
                ' * tools/cd/match.py.\n'
                ' */\n'
                '#include "zextract.h"\n\n')

        f.write(f"const char zx_namen[{len(n.blob)}] = {{\n")
        f.write(c_block(n.blob))
        f.write("\n};\n\n")

        f.write(f"const struct ZxBild zx_bilder[{len(bilder)}] = {{\n")

        for name, satz, w, h, grp, xl in bilder:
            f.write(f"\t{{ {name}, {satz}, {w}, {h}, {grp}, {xl} }},\n")

        f.write("};\n\n")

        f.write(f"const struct ZxKlang zx_klaenge[{len(klaenge)}] = {{\n")

        for name, rate, dos in klaenge:
            f.write(f'\t{{ {name}, {rate}, "{dos}" }},\n')

        f.write("};\n\n")

        f.write(f"const UBYTE zx_xlat[{len(xlat_ord)}][256] = {{\n")

        for g in xlat_ord:
            f.write(f"\t{{ /* {g} */\n{c_block(xlat_daten[g])}\n\t}},\n")

        f.write("};\n\n")

        f.write("const char *const zx_gruppen[] = {\n")

        for g in GRUPPEN:
            f.write(f'\t"{g}",\n')

        f.write("};\n\n")

        f.write(f"const LONG zx_n_bilder  = {len(bilder)};\n")
        f.write(f"const LONG zx_n_klaenge = {len(klaenge)};\n")
        f.write(f"const LONG zx_n_gruppen = {len(GRUPPEN)};\n")
        f.write(f"const LONG zx_n_xlat    = {len(xlat_ord)};\n")

    groesse = pathlib.Path(args.ziel).stat().st_size
    print(f"{args.ziel}: {len(bilder)} Bilder, {len(klaenge)} Klaenge, "
          f"{len(xlat_ord)} Umsetztabellen, {len(n.blob)} Byte Namen "
          f"({groesse // 1024} KB Quelle)")


if __name__ == "__main__":
    main()
