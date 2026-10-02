#!/usr/bin/env python3
"""Ordnet die Sätze aus SPRITES.RSC den Dateinamen der Engine zu.

Die Zuordnung wird **nicht geraten**. Der vorhandene Bestand
(`data/game/assets`) ist erwiesenermaßen Originalmaterial, also gilt:

    Ein Satz gehört genau dann zu einer Datei, wenn seine Pixel
    **bitgleich** mit deren Palettenindizes sind.

Das liefert die Tabelle und ihren Beweis in einem Schritt. Es sagt zugleich
genau, wie weit die Zuordnung reicht -- jede Zeile in `names.txt` ist belegt,
und was fehlt, fehlt sichtbar.

Nicht gesucht werden die Teamfarben blau/grün/gelb unter `units/` und
`buildings/`: Die erzeugt `ZTeam::Make` zur Laufzeit aus Rot, genau wie beim
heutigen Packer (`tools/assets/pack.py`).

Für die Klänge gilt dasselbe, und dort trägt es weiter: **257 von 261**
Klangdateien des Bestands sind bitgleich eine `.RAW` der CD. Der Name allein
hätte nur 210 gefunden -- Nighsoft hat viele umbenannt (`acknowledge_00.wav`).
Der Inhalt findet auch die.

Die dritte Tabelle: die Farbumsetzung
-------------------------------------
Die Indizes der CD sind fast, aber nicht ganz die der gemeinsamen Palette der
Engine -- Nighsofts PNG-Paletten weichen stellenweise um eine VGA-Stufe ab.
Diese Umsetzung wird **nicht über Farbabstände geschätzt**, sondern aus den
4053 belegten Paaren **abgeleitet**: Für jedes Paar wird Pixel für Pixel
gezählt, welcher Index der CD auf welchen Index des Bestands trifft; die
Mehrheit gewinnt. Je Planetenbank eine Tabelle, weil die Bänke Plätze
ersetzen.

Das Ergebnis ist eindeutig: In allen fünf Planetengruppen gibt es **keinen
einzigen** mehrdeutigen Index. Ein Versuch mit Farbabständen kam auf 1877
bitgleiche Bilder, die abgeleiteten Tabellen auf **3847 von 4053**.

Aufruf
------
    tools/cd/match.py                 Tabellen schreiben (names.txt, sounds.txt,
                                      xlat.txt)
    tools/cd/match.py --bericht       nur zählen, nach Bereichen aufgeschlüsselt
"""
import argparse
import collections
import hashlib
import pathlib
import sys
import wave

sys.path.insert(0, str(pathlib.Path(__file__).parent))

from sprites import lade                       # noqa: E402
from zpac import ZPac                          # noqa: E402
from nicecomp import entpacke_eintrag, NochUnbekannt   # noqa: E402

HIER = pathlib.Path(__file__).parent
TEAM_VERZ = ("units", "buildings")
TEAM_MARKEN = ("_blue", "_green", "_yellow")


def ist_teamfassung(rel):
    """Wie tools/assets/pack.py: nur in units/ und buildings/ filtern."""
    teile = rel.parts

    return (teile[0] in TEAM_VERZ
            and any(m in rel.name.lower() for m in TEAM_MARKEN))


def sammle(sprites, assets):
    """(zuordnung, offen, mehrdeutig) gegen den Bestand."""
    nach_inhalt = collections.defaultdict(list)

    for r in range(sprites.count):
        nach_inhalt[sprites.satz(r)].append(r)

    from PIL import Image

    wurzel = pathlib.Path(assets)
    zuordnung = []
    offen = collections.Counter()
    mehrdeutig = 0

    for p in sorted(wurzel.rglob("*.png")):
        rel = p.relative_to(wurzel)

        if ist_teamfassung(rel):
            continue

        bereich = "/".join(rel.parts[:2]) if len(rel.parts) > 1 else rel.parts[0]
        im = Image.open(p)

        if im.mode != "P":
            offen[bereich] += 1
            continue

        treffer = nach_inhalt.get((im.width, im.height, im.tobytes()))

        if not treffer:
            offen[bereich] += 1
            continue

        if len(treffer) > 1:
            mehrdeutig += 1

        zuordnung.append((treffer[0], im.width, im.height,
                          "assets/" + rel.as_posix()))

    return zuordnung, offen, mehrdeutig


def sammle_klaenge(cd, assets):
    """(zuordnung, offen) fuer die Klaenge, ueber den Inhalt."""
    pac = ZPac(pathlib.Path(cd) / "z.pac")
    nach_inhalt = {}

    for e in pac.entries:
        if e.name[8:11].strip() != "RAW":
            continue

        try:
            d = entpacke_eintrag(pac, e)
        except NochUnbekannt:
            continue

        nach_inhalt.setdefault(hashlib.sha1(d).digest(), []).append(e.dos_name)

    zuordnung = []
    offen = []

    for f in sorted((pathlib.Path(assets) / "sounds").glob("*.wav")):
        try:
            wf = wave.open(str(f))
            roh = wf.readframes(wf.getnframes())
            rate = wf.getframerate()
        except Exception:
            offen.append(f.name)
            continue

        treffer = nach_inhalt.get(hashlib.sha1(roh).digest())

        if not treffer:
            offen.append(f.name)
            continue

        zuordnung.append((treffer[0], rate, "assets/sounds/" + f.name))

    return zuordnung, offen


def schreibe_klaenge(zuordnung, ziel):
    with open(ziel, "w") as f:
        f.write("# Zuordnung .RAW aus z.pac -> Klangdatei der Engine\n")
        f.write("# erzeugt von tools/cd/match.py; Grundlage ist bitgleicher\n")
        f.write("# Inhalt. Nicht von Hand aendern.\n")
        f.write("# dosname rate pfad\n")

        for dos, rate, pfad in sorted(zuordnung, key=lambda t: t[2]):
            f.write(f"{dos:<13} {rate:6d} {pfad}\n")


def leite_umsetzung_ab(sprites, zuordnung, packs):
    """Umsetztabellen aus den belegten Paaren ableiten (Mehrheit je Index).

    Braucht die heutigen Archive als Maßstab -- deshalb entsteht `xlat.txt`
    hier und nicht im Extraktor; der liest sie nur noch.
    """
    from verify import sammle
    from extract import planet_von

    alt = sammle(packs)
    stimmen = collections.defaultdict(
        lambda: collections.defaultdict(collections.Counter))

    for r, w, h, pfad in zuordnung:
        a = alt.get(pfad)

        if not a:
            continue

        cd = sprites.satz(r)[2]

        if len(cd) != len(a[5]):
            continue

        t = stimmen[planet_von(pfad)]

        for x, y in zip(cd, a[5]):
            t[x][y] += 1

    tabellen = {}
    mehrdeutig = 0

    for p, t in stimmen.items():
        b = bytearray(range(256))

        for i, c in t.items():
            b[i] = c.most_common(1)[0][0]

            if len(c) > 1:
                mehrdeutig += 1

        tabellen[p] = bytes(b)

    return tabellen, mehrdeutig


def schreibe_umsetzung(tabellen, ziel):
    with open(ziel, "w") as f:
        f.write("# Farbumsetzung CD-Index -> Index der gemeinsamen Palette\n")
        f.write("# erzeugt von tools/cd/match.py aus den belegten Paaren,\n")
        f.write("# nicht ueber Farbabstaende geschaetzt. Je Zeile eine\n")
        f.write("# Tabelle: <gruppe> <512 Hexziffern>.  Nicht von Hand aendern.\n")

        for p in sorted(tabellen, key=lambda x: (x is not None, x)):
            f.write(f"{p or '-'} {tabellen[p].hex()}\n")


def schreibe(zuordnung, ziel):
    with open(ziel, "w") as f:
        f.write("# Zuordnung CD-Satz -> Dateiname der Engine\n")
        f.write("# erzeugt von tools/cd/match.py; Grundlage ist bitgleicher\n")
        f.write("# Inhalt, nicht Vermutung. Nicht von Hand aendern.\n")
        f.write("# satz breite hoehe pfad\n")

        for r, w, h, pfad in sorted(zuordnung, key=lambda t: t[3]):
            f.write(f"{r:05d} {w:4d} {h:4d} {pfad}\n")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--cd", default="/media/cem/Z_280796")
    ap.add_argument("--assets", default="data/game/assets")
    ap.add_argument("--ziel", default=str(HIER / "names.txt"))
    ap.add_argument("--klaenge", default=str(HIER / "sounds.txt"))
    ap.add_argument("--umsetzung", default=str(HIER / "xlat.txt"))
    ap.add_argument("--packs", default="data/game/packs",
                    help="heutige Archive als Massstab fuer die Farbumsetzung")
    ap.add_argument("--bericht", action="store_true")
    args = ap.parse_args()

    s = lade(args.cd)
    zuordnung, offen, mehrdeutig = sammle(s, args.assets)
    benutzt = {r for r, _, _, _ in zuordnung}

    print(f"Saetze auf der CD:        {s.count}")
    print(f"davon zugeordnet:         {len(benutzt)}")
    print(f"Bilder zugeordnet:        {len(zuordnung)}")
    print(f"davon mehrdeutige Saetze: {mehrdeutig}")
    print(f"Bilder ohne Gegenstueck:  {sum(offen.values())}")

    kl, kl_offen = sammle_klaenge(args.cd, args.assets)
    print(f"Klaenge zugeordnet:       {len(kl)}")
    print(f"Klaenge ohne Gegenstueck: {len(kl_offen)}  {kl_offen}")

    tabellen, mehrdeutig_idx = leite_umsetzung_ab(s, zuordnung, args.packs)
    print(f"Umsetztabellen:           {len(tabellen)}, "
          f"mehrdeutige Indizes {mehrdeutig_idx}")

    if args.bericht:
        print("\noffen nach Bereich:")

        for b, n in offen.most_common(20):
            print(f"  {b:<34} {n:5d}")

        return

    schreibe(zuordnung, args.ziel)
    schreibe_klaenge(kl, args.klaenge)
    schreibe_umsetzung(tabellen, args.umsetzung)
    print(f"\nnames.txt, sounds.txt und xlat.txt geschrieben")


if __name__ == "__main__":
    main()
