#!/usr/bin/env python3
"""Abnahme des Extraktors: Vergleicht seine Archive mit den heutigen.

Der Maßstab ist der vorhandene Bestand. Für jeden Namen, den beide Seiten
kennen, müssen Maße und Daten **bitgleich** sein. Ist das erfüllt, hat die
Extraktion von der CD dasselbe Ergebnis wie der Weg über die PNG-Dateien --
und der Extraktor ist bewiesen, nicht behauptet.

    tools/cd/verify.py <neu> <alt>

Abschluss-Code 0 = keine Abweichung.
"""
import argparse
import pathlib
import struct
import sys


def lies_zpk(pfad):
    """{name: (fmt, w, h, trans, reserve, daten)}"""
    d = pathlib.Path(pfad).read_bytes()

    if d[:4] != b"ZPK1":
        sys.exit(f"{pfad}: keine ZPK1-Datei")

    anzahl, namen_ab, namen_len = struct.unpack(">III", d[4:16])
    namen = d[namen_ab:namen_ab + namen_len]
    aus = {}

    for i in range(anzahl):
        e = d[16 + i * 24:40 + i * 24]
        n_ab, n_len, w, h, fmt, trans, _pal, d_ab, d_len, res = \
            struct.unpack(">IHHHBBHIIH", e)
        name = namen[n_ab:n_ab + n_len].decode("latin1")
        aus[name] = (fmt, w, h, trans, res, d[d_ab:d_ab + d_len])

    return aus


def sammle(verz):
    aus = {}

    for f in sorted(pathlib.Path(verz).glob("*.zpk")):
        aus.update(lies_zpk(f))

    return aus


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("neu")
    ap.add_argument("alt")
    args = ap.parse_args()

    neu = sammle(args.neu)
    alt = sammle(args.alt)
    gemeinsam = sorted(set(neu) & set(alt))

    gleich = 0
    abweichend = []

    for n in gemeinsam:
        if neu[n] == alt[n]:
            gleich += 1
        else:
            a, b = neu[n], alt[n]
            grund = ("Format" if a[0] != b[0] else
                     "Masse" if (a[1], a[2]) != (b[1], b[2]) else
                     "Reserve" if a[4] != b[4] else
                     "Laenge" if len(a[5]) != len(b[5]) else "Pixel")
            abweichend.append((n, grund))

    print(f"Eintraege neu:      {len(neu)}")
    print(f"Eintraege alt:      {len(alt)}")
    print(f"beiden bekannt:     {len(gemeinsam)}")
    print(f"davon bitgleich:    {gleich}")
    print(f"abweichend:         {len(abweichend)}")
    print(f"nur im alten Satz:  {len(set(alt) - set(neu))}")

    if abweichend:
        import collections

        print("\nGruende:", collections.Counter(g for _, g in abweichend)
              .most_common())

        # Wie schwer wiegen die Pixelabweichungen? Ein einzelnes Pixel, das um
        # einen Palettenplatz verrutscht, ist etwas anderes als ein falsches
        # Bild -- also wird es gezaehlt statt behauptet.
        pixel = [n for n, g in abweichend if g == "Pixel"]
        anteile = []

        for n in pixel:
            a, b = neu[n][5], alt[n][5]
            ungleich = sum(1 for x, y in zip(a, b) if x != y)
            anteile.append((100.0 * ungleich / len(a), n))

        if anteile:
            anteile.sort(reverse=True)
            schnitt = sum(q for q, _ in anteile) / len(anteile)
            print(f"Pixelabweichung: im Mittel {schnitt:.1f} % der Pixel, "
                  f"schlimmster Fall {anteile[0][0]:.1f} %")
            print("die schlimmsten fuenf:")

            for q, n in anteile[:5]:
                print(f"  {q:5.1f}%  {n}")

    return 1 if abweichend else 0


if __name__ == "__main__":
    sys.exit(main())
