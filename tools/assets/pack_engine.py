#!/usr/bin/env python3
"""Packt den Teil der Spieldaten, der **nicht** von der CD kommt.

Das Auslieferungsmodell trennt zwei Dinge:

* Was auf der Original-CD steht, holt der Nutzer selbst (`tools/cd/extract.py`
  bzw. `ZExtract` auf dem Amiga). Das ist Material der Bitmap Brothers.
* Alles übrige stammt von Nighsoft (Zod Engine, GPL) oder ist Beiwerk der
  Engine. Das wird **mitgeliefert** -- und genau dafür ist dieses Werkzeug da.

Es erzeugt ein einziges Archiv `zod_engine.zpk`. Der Lader kennt den Namen
(`port/zod_pack.cpp`); beim Nutzer liegt es neben den extrahierten Archiven.

**Es landet bewusst NICHT in `data/game/packs`.** Dort steht der volle Satz aus
`pack.py`, und der enthält dieselben Dateien noch einmal -- zwei Archive mit
demselben Namen für dasselbe Bild sind eine Falle, die man erst bemerkt, wenn
eines von beiden veraltet. Die Entwicklungsumgebung bleibt beim vollen Satz,
das Auslieferungspaket bekommt Extraktor plus dieses eine Archiv.

Was hineinkommt, wird **nicht von Hand gepflegt**, sondern ausgerechnet: alles
aus `data/game/assets`, was weder in `tools/cd/names.txt` noch in
`tools/cd/sounds.txt` steht -- also alles, was der Extraktor nicht liefern
kann. Wächst die Zuordnung, schrumpft dieses Archiv von selbst.

Ausgenommen bleiben wie beim gewöhnlichen Packer die vorgefärbten
Teamvarianten unter `units/` und `buildings/`: die erzeugt `ZTeam::Make` zur
Laufzeit aus Rot.

    tools/assets/pack_engine.py [--src data/game/assets] [--out dist/packs-engine]
"""
import argparse
import pathlib
import sys

HIER = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HIER))

import pack as packer                              # noqa: E402

TABELLEN = HIER.parent / "cd"

TEAM_VERZ = ("units", "buildings")
TEAM_MARKEN = ("_blue", "_green", "_yellow")


def von_der_cd():
    """Alle Pfade, die der Extraktor liefert -- aus den Tabellen gelesen."""
    aus = set()

    for datei, feld in ((TABELLEN / "names.txt", 3),
                        (TABELLEN / "sounds.txt", 2)):
        if not datei.exists():
            sys.exit(f"{datei} fehlt -- erst tools/cd/match.py laufen lassen")

        for zeile in datei.read_text().splitlines():
            zeile = zeile.strip()

            if zeile and not zeile.startswith("#"):
                aus.add(zeile.split(None, feld)[feld])

    return aus


def ist_teamfassung(rel):
    teile = rel.parts

    return (teile[0] in TEAM_VERZ
            and any(m in rel.name.lower() for m in TEAM_MARKEN))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--src", default="data/game/assets")
    ap.add_argument("--out", default="dist/packs-engine")
    ap.add_argument("--palette", default="data/game/packs/palette.zpl")
    args = ap.parse_args()

    quelle = pathlib.Path(args.src)
    ziel = pathlib.Path(args.out)
    ziel.mkdir(parents=True, exist_ok=True)

    cd = von_der_cd()
    pal = packer.load_palette(args.palette)

    eintraege = []
    team = 0
    uebersprungen = 0

    for f in sorted(quelle.rglob("*")):
        if f.suffix.lower() not in (".png", ".bmp", ".wav"):
            continue

        rel = f.relative_to(quelle)
        name = "assets/" + rel.as_posix()

        if name in cd:
            continue

        if ist_teamfassung(rel):
            team += 1
            continue

        try:
            if f.suffix.lower() == ".wav":
                res = packer.convert_wav(f)

                if not res or not res[1]:
                    uebersprungen += 1
                    continue

                rate, pcm = res
                eintraege.append((name, packer.FMT_PCM8, 0, 0, 255, b"", pcm,
                                  rate))
                continue

            # Die Teampaletten bleiben ausgenommen: ZTeam liest aus ihnen
            # Farbpaare, Umindizieren wuerde sie verfaelschen.
            if rel.as_posix().lower().startswith("teams/"):
                fmt, w, h, trans, palette, daten = packer.convert(f)
            else:
                fmt, w, h, trans, palette, daten, _ = \
                    packer.convert_shared(f, rel.as_posix(), pal)
        except Exception as exc:
            print(f"  uebersprungen: {rel} ({exc})")
            uebersprungen += 1
            continue

        eintraege.append((name, fmt, w, h, trans, palette, daten))

    groesse = packer.build_pack(eintraege, ziel / "zod_engine.zpk")

    bilder = sum(1 for e in eintraege if e[1] != packer.FMT_PCM8)
    print(f"zod_engine.zpk: {bilder} Bilder und "
          f"{len(eintraege) - bilder} Klaenge, "
          f"{groesse / 1048576:.1f} MB")
    print(f"von der CD (nicht hier drin): {len(cd)}")
    print(f"Teamvarianten ausgelassen:    {team}")

    if uebersprungen:
        print(f"uebersprungen:                {uebersprungen}")


if __name__ == "__main__":
    main()
