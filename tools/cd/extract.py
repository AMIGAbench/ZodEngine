#!/usr/bin/env python3
"""Extraktor für Linux: Original-CD von *Z* -> Archive der Engine.

Die Engine wird ohne Spieldaten ausgeliefert. Wer sie spielen will, braucht die
Original-CD (The Bitmap Brothers, 1996) und dieses Werkzeug. Es liest die CD
und schreibt unmittelbar die `.zpk`-Archive, die die Engine lädt -- ohne Umweg
über tausende Einzeldateien.

    tools/cd/extract.py --cd /media/cem/Z_280796 --out data/game/packs

Es entspricht Zug um Zug dem Amiga-Werkzeug `ZExtract`
(`tools/cd/amiga/zextract.c`); beide erzeugen **bitgleiche** Archive. Wer einen
Amiga hat, braucht nie einen PC.

Woher was kommt
---------------
* **Bilder** aus `SPRITES.RSC` in `z.pac`. Die Sätze liegen in
  Mode-X-Ebenenfolge, siehe `sprites.py`. Welcher Satz welche Datei ist, steht
  in `names.txt` -- belegt über bitgleichen Inhalt, siehe `match.py`.
* **Klänge** aus den `.RAW`-Einträgen in `z.pac`, Zuordnung in `sounds.txt`.
  Aus vorzeichenlosen 8 Bit werden vorzeichenbehaftete, wie AHI sie erwartet.

Was NICHT von der CD kommt
--------------------------
HUD, Menüs, Startbild, Echtfarb-Cursor und die Fabrikfenster stammen von
Nighsoft, nicht von den Bitmap Brothers; sie werden mit der Engine
ausgeliefert. Ebenso `palette.zpl`: Das ist eine reine Engine-Tabelle
(Grundpalette, Planetenbänke, Team-Umsetztabellen) ohne Bildmaterial.

Format der Archive: siehe `tools/assets/pack.py`. Geschrieben wird
ausschließlich Format 4 (ein Byte je Pixel, Index in die gemeinsame Palette)
und Format 3 (Klang).

Die Umsetzung der Farben ist **eine Tabelle, keine Suche je Pixel**
-------------------------------------------------------------------
Die CD hat genau eine Palette für alles, und sie liegt der gemeinsamen Palette
der Engine zugrunde -- stellenweise um eine VGA-Stufe verschoben. Die sechs
256-Byte-Umsetztabellen stehen fertig in `xlat.txt`; `match.py` hat sie aus den
belegten Paaren abgeleitet. Hier ist jedes Pixel damit **ein
Tabellenzugriff**, keine Farbsuche. Das ist der Grund, warum derselbe Weg auf
dem Amiga trägt.

Ein Versuch, die Umsetzung über Farbabstände zu *schätzen*, kam auf 1877
bitgleiche Bilder; die abgeleiteten Tabellen kommen auf 3847 von 4053.
"""
import argparse
import pathlib
import struct
import sys

sys.path.insert(0, str(pathlib.Path(__file__).parent))

from sprites import Sprites                    # noqa: E402
from zpac import ZPac                          # noqa: E402
from nicecomp import entpacke_eintrag          # noqa: E402

HIER = pathlib.Path(__file__).parent

FMT_PCM8 = 3
FMT_SHARED8 = 4
IDX_KEY = 0

GRUPPEN = ("units", "buildings", "planets", "fonts", "cursors", "other", "teams")
PLANETEN = ("desert", "volcanic", "arctic", "jungle", "city")



def planet_von(pfad):
    """Wie tools/assets/palette.py:planet_of, auf 'assets/'-Pfade bezogen."""
    rel = pfad[len("assets/"):].lower() if pfad.startswith("assets/") else pfad.lower()

    for p in PLANETEN:
        if rel.startswith("planets/") and p in rel:
            return p

        if "_" + p in rel or "/" + p + "_" in rel:
            return p

    return None



def gruppe_von(pfad):
    """assets/units/... -> units; assets/splash.png -> misc"""
    teile = pfad.split("/")

    if len(teile) < 3:
        return "misc"

    return teile[1] if teile[1] in GRUPPEN else "misc"


def lies_tabelle(pfad, felder):
    """Zeilen einer Tabellendatei, Kommentare und Leerzeilen weg."""
    aus = []

    for zeile in pathlib.Path(pfad).read_text().splitlines():
        zeile = zeile.strip()

        if not zeile or zeile.startswith("#"):
            continue

        aus.append(zeile.split(None, felder - 1))

    return aus


def schreibe_pack(eintraege, ziel):
    """Ein .zpk schreiben. Gleicher Aufbau wie tools/assets/pack.py."""
    namen = bytearray()
    versatz = {}

    for name, *_ in eintraege:
        if name not in versatz:
            versatz[name] = len(namen)
            namen += name.encode("latin1")

    kopf = 16
    verz = 24 * len(eintraege)
    namen_ab = kopf + verz
    daten_ab = namen_ab + len(namen)

    verzeichnis = bytearray()
    daten = bytearray()

    for name, fmt, w, h, trans, rohdaten, reserve in eintraege:
        verzeichnis += struct.pack(
            ">IHHHBBHIIH",
            versatz[name], len(name), w, h,
            fmt, trans, 0,
            daten_ab + len(daten), len(rohdaten),
            reserve,
        )
        daten += rohdaten

    with open(ziel, "wb") as f:
        f.write(b"ZPK1")
        f.write(struct.pack(">III", len(eintraege), namen_ab, len(namen)))
        f.write(verzeichnis)
        f.write(namen)
        f.write(daten)

    return kopf + verz + len(namen) + len(daten)


def hole_sprites(pac):
    e = [x for x in pac.entries if x.dos_name == "SPRITES.RSC"]

    if not e:
        sys.exit("SPRITES.RSC nicht in z.pac -- ist das die Z-CD?")

    return Sprites(entpacke_eintrag(pac, e[0]))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--cd", default="/media/cem/Z_280796",
                    help="eingelegte CD oder ein Verzeichnis mit z.pac")
    ap.add_argument("--out", default="data/game/packs")
    ap.add_argument("--namen", default=str(HIER / "names.txt"))
    ap.add_argument("--klaenge", default=str(HIER / "sounds.txt"))
    ap.add_argument("--umsetzung", default=str(HIER / "xlat.txt"))
    args = ap.parse_args()

    quelle = pathlib.Path(args.cd)
    pac_pfad = quelle / "z.pac"

    if not pac_pfad.exists():
        sys.exit(f"{pac_pfad} nicht gefunden -- CD eingelegt?")

    ziel = pathlib.Path(args.out)
    ziel.mkdir(parents=True, exist_ok=True)

    print(f"Quelle: {pac_pfad}")
    pac = ZPac(pac_pfad)
    sprites = hole_sprites(pac)
    print(f"SPRITES.RSC: {sprites.count} Saetze")

    # --- Umsetztabellen: eine je Planetenbank, plus die ohne -------------
    tabellen = {}

    for gruppe, hexziffern in lies_tabelle(args.umsetzung, 2):
        tabellen[None if gruppe == "-" else gruppe] = bytes.fromhex(hexziffern)

    if None not in tabellen:
        sys.exit(f"{args.umsetzung}: Grundtabelle '-' fehlt")

    print(f"Umsetztabellen: {len(tabellen)}")

    # --- Bilder ---------------------------------------------------------
    eimer = {}
    fehlend = 0

    for satz, w, h, pfad in lies_tabelle(args.namen, 4):
        r = int(satz)
        t = sprites.satz(r)

        if not t or (t[0], t[1]) != (int(w), int(h)):
            fehlend += 1
            continue

        tab = tabellen.get(planet_von(pfad), tabellen[None])

        eimer.setdefault(gruppe_von(pfad), []).append(
            (pfad, FMT_SHARED8, t[0], t[1], IDX_KEY, t[2].translate(tab), 0))

    if fehlend:
        print(f"WARNUNG: {fehlend} Saetze passen nicht zur Tabelle "
              f"-- andere CD-Fassung?")

    # --- Klaenge --------------------------------------------------------
    nach_dos = {e.dos_name: e for e in pac.entries}
    klang = []

    for dos, rate, pfad in lies_tabelle(args.klaenge, 3):
        e = nach_dos.get(dos)

        if not e:
            fehlend += 1
            continue

        roh = entpacke_eintrag(pac, e)
        # vorzeichenlos -> vorzeichenbehaftet, wie AHI es erwartet
        klang.append((pfad, FMT_PCM8, 0, 0, 255,
                      bytes((b - 128) & 0xFF for b in roh), int(rate)))

    # --- schreiben ------------------------------------------------------
    gesamt = 0
    print(f"\n{'Archiv':18} {'Eintraege':>9} {'Groesse':>10}")

    for g in sorted(eimer):
        n = schreibe_pack(eimer[g], ziel / f"zod_{g}.zpk")
        gesamt += n
        print(f"zod_{g + '.zpk':14} {len(eimer[g]):9} {n / 1048576:7.1f} MB")

    if klang:
        n = schreibe_pack(klang, ziel / "zod_sounds.zpk")
        gesamt += n
        print(f"{'zod_sounds.zpk':18} {len(klang):9} {n / 1048576:7.1f} MB")

    print(f"\nGesamt {gesamt / 1048576:.1f} MB in {ziel}")

    if not (ziel / "palette.zpl").exists():
        print("\nHINWEIS: palette.zpl fehlt im Zielverzeichnis. Sie gehoert "
              "zur Engine, nicht zur CD, und wird mitgeliefert.")


if __name__ == "__main__":
    main()
