#!/usr/bin/env python3
"""
Baut die gemeinsame 256-Farben-Palette fuer den 8-Bit-Pfad -- aus der
ORIGINALPALETTE des Spiels, nicht gerechnet.

Der Bestand teilt sich 38 Paletten mit je 241-245 Farben; die groesste deckt
4578 Dateien. Gemessen (18.09.): Bildet man ALLE Bilder auf diese eine Palette
ab, verschiebt sich bei der Haelfte kein einziges Pixel (Median 0 %), und nur
1048 von 9405 Bildern verlieren mehr als 5 % -- fast alle davon die
nachbearbeiteten Oberflaechenbilder (factory_gui, main_menu_gui, hud) sowie
einzelne Krater auf Dschungel und Vulkan.

Ein erster Versuch, die Palette selbst zu rechnen (Farben nach Pixelhaeufigkeit
zusammenlegen), war deutlich schlechter: 3841 von 6941 Bildern ueber 5 %. Die
Originalpalette ist der Sache gewachsen -- das Spiel wurde fuer sie gezeichnet.

Ergebnis (Datei palette.zpl, Big-Endian):

  "ZPL2"
  u16 Zahl der Planeten, u16 Plaetze je Planetenbank
  u16 Zahl der Teams, u16 Laenge einer Rampe
  256*3 Byte   Grundpalette (Index 0 = Farbschluessel)
  je Planet:   u16 Anzahl, dann je Eintrag u8 Platz + u8 r + u8 g + u8 b
  je Team:     256 Byte Umsetztabelle (Index -> Index), Team 0 (rot) ist die
               Gleichheit

Die Engine laedt die Grundpalette einmal, schreibt beim Kartenwechsel die Bank
des Planeten hinein und faerbt Teams ueber die Umsetztabelle beim Zeichnen --
kein Kopieren von Bildern mehr.

Aufruf:
  tools/assets/palette.py [--src data/game/assets] [--out data/game/packs/palette.zpl]
"""
import argparse
import collections
import pathlib
import struct
import sys

try:
    from PIL import Image
except ImportError:
    sys.exit("PIL/Pillow wird benoetigt: pip install pillow")

# Reihenfolge WIE planet_type in constants.h -- der Lader nimmt die Bank
# ueber den Kartenwert basic_info.terrain_type ohne Umrechnung.
PLANETS = ["desert", "volcanic", "arctic", "jungle", "city"]

# Dateien, die der Code nie laedt (sie haben die alte Farbmessung verdorben),
# und das Startbild, das allein gezeigt wird und eine eigene Palette bekommt.
SKIP_DIRS = ["buildings/fort_old"]
SKIP_FILES = [
    "other/factory_gui/defragen_sidebarpreview2.png",
    "splash.png", "splash.bmp", "icon.png",
]


def is_skipped(rel):
    return any(rel.startswith(d + "/") for d in SKIP_DIRS) or rel in SKIP_FILES


def planet_of(rel):
    """Planet, zu dem eine Datei gehoert -- oder None, wenn sie ueberall gilt."""
    low = rel.lower()

    for p in PLANETS:
        if low.startswith("planets/") and p in low:
            return p
        if "_" + p in low or "/" + p + "_" in low:
            return p

    return None


def image_colors(path):
    """(r,g,b) -> Pixelzahl, ohne die durchsichtigen Stellen."""
    im = Image.open(path)
    out = collections.Counter()

    if im.mode == "P":
        pal = im.getpalette() or []
        ncolors = len(pal) // 3
        trans = im.info.get("transparency")

        for idx, cnt in im.getcolors(1 << 24) or []:
            if idx == 0 or idx >= ncolors:     # Index 0 ist durchgehend der Schluessel
                continue
            if isinstance(trans, int) and idx == trans:
                continue
            out[(pal[idx * 3], pal[idx * 3 + 1], pal[idx * 3 + 2])] += cnt

        return out

    for cnt, px in im.convert("RGBA").getcolors(1 << 24) or []:
        if px[3] < 128:
            continue
        out[px[:3]] += cnt

    return out


def dist(a, b):
    return max(abs(a[0] - b[0]), abs(a[1] - b[1]), abs(a[2] - b[2]))


def nearest(color, palette, skip_zero=True):
    """(Platz, Abstand) der naechsten Farbe; Platz 0 bleibt der Schluessel."""
    best, best_d = 0, 1 << 30

    for i, c in enumerate(palette):
        if skip_zero and i == 0:
            continue

        d = dist(color, c)

        if d < best_d:
            best, best_d = i, d

            if d == 0:
                break

    return best, best_d


def team_ramps(src):
    """Basisrampe (rot) und Ersatzrampen je Team aus assets/teams/*_palette.bmp.

    Jede Datei ist 2 Pixel breit: links die Basisfarbe, rechts der Ersatz."""
    RAMP_MAX = 16       # ZTEAM_PALETTE_MAX in constants.h
    ramps = {}
    base = []

    for path in sorted((src / "teams").glob("*_palette.bmp")):
        name = path.stem.replace("_palette", "")

        if name.lower().startswith("copy of"):     # Doppel im Bestand
            continue

        im = Image.open(path).convert("RGB")
        b, r = [], []

        for y in range(min(im.size[1], RAMP_MAX)):
            b.append(im.getpixel((0, y)))
            r.append(im.getpixel((1, y)))

        if not base:
            base = b

        ramps[name] = r

    return base, ramps


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--src", default="data/game/assets")
    ap.add_argument("--out", default="")
    ap.add_argument("--tol", type=int, default=4,
                    help="bis zu diesem Kanalabstand gilt eine Farbe als vorhanden "
                         "(4 = eine VGA-Stufe)")
    args = ap.parse_args()

    src = pathlib.Path(args.src)

    if not src.is_dir():
        sys.exit(f"{src} nicht gefunden")

    # 1. Die Originalpalette finden: die, die die meisten Dateien benutzen
    pal_count = collections.Counter()

    for f in sorted(src.rglob("*")):
        if f.suffix.lower() not in (".png", ".bmp"):
            continue

        im = Image.open(f)

        if im.mode == "P":
            pal_count[tuple((im.getpalette() or [])[:768])] += 1

    if not pal_count:
        sys.exit("keine palettierten Bilder gefunden")

    master, files = pal_count.most_common(1)[0]
    base_palette = [tuple(master[i * 3: i * 3 + 3]) for i in range(256)]
    print(f"{len(pal_count)} Paletten im Bestand; Grundpalette aus {files} Dateien, "
          f"{len(set(base_palette))} verschiedene Farben")

    # 2. Welche Plaetze benutzt werden und was die Grundpalette nicht hergibt
    used_slots = set()
    core_needed = collections.Counter()
    planet_needed = {p: collections.Counter() for p in PLANETS}
    files_read = 0
    cache = {}

    for f in sorted(src.rglob("*")):
        if f.suffix.lower() not in (".png", ".bmp"):
            continue

        rel = str(f.relative_to(src)).replace("\\", "/")

        if is_skipped(rel) or rel.startswith("teams/"):
            continue

        low = rel.lower()

        if any(t in low for t in ("_blue", "_green", "_yellow")) and (
                low.startswith("units/") or low.startswith("buildings/")):
            continue        # entstehen zur Laufzeit aus Rot

        files_read += 1
        planet = planet_of(rel)

        for color, weight in image_colors(f).items():
            hit = cache.get(color)

            if hit is None:
                hit = nearest(color, base_palette)
                cache[color] = hit

            slot, d = hit

            if d <= args.tol:
                used_slots.add(slot)
            elif planet:
                planet_needed[planet][color] += weight
            else:
                core_needed[color] += weight

    print(f"{files_read} Dateien geprueft; die Grundpalette deckt {len(used_slots)} "
          f"Plaetze ab")

    seen = {}
    spare = []

    for i, c in enumerate(base_palette):
        if i == 0:
            continue                       # Farbschluessel

        if i not in used_slots or c in seen:
            spare.append(i)
        else:
            seen[c] = i

    print(f"freie Plaetze fuer die Planetenbaenke: {len(spare)}")
    print(f"nicht abgedeckt: Kern {len(core_needed)} Farben, "
          + ", ".join(f"{p}={len(planet_needed[p])}" for p in PLANETS))

    banks = {}

    for p in PLANETS:
        wanted = [c for c, _ in planet_needed[p].most_common(len(spare))]
        banks[p] = list(zip(spare, wanted))

        rest = len(planet_needed[p]) - len(wanted)
        print(f"  {p:9s} {len(wanted):3d} Plaetze belegt, {rest:3d} Farben werden "
              f"auf die naechste abgebildet")

    # 3. Umsetztabellen der Teams: Platz der Rampenfarbe -> Platz der Ersatzfarbe
    base_ramp, ramps = team_ramps(src)

    #Reihenfolge wie enum team_type in constants.h -- dann ist der Team-Index
    #der Engine zugleich der Index in dieser Datei.
    team_names = ["null", "red", "blue", "green", "yellow",
                  "purple", "teal", "white", "black"]
    xlat = {}

    for name in team_names:
        table = list(range(256))

        if name in ramps:
            for i, bc in enumerate(base_ramp):
                if i >= len(ramps[name]):
                    break

                src_slot, d_src = nearest(bc, base_palette)
                dst_slot, _ = nearest(ramps[name][i], base_palette)

                if d_src <= args.tol:
                    table[src_slot] = dst_slot

        xlat[name] = table

    print("Teams (Reihenfolge wie team_type): " + ", ".join(
        f"{i}={n}/{sum(1 for j, v in enumerate(xlat[n]) if j != v)}"
        for i, n in enumerate(team_names)))

    if args.out:
        out = pathlib.Path(args.out)
        out.parent.mkdir(parents=True, exist_ok=True)

        with out.open("wb") as fp:
            fp.write(b"ZPL2")
            fp.write(struct.pack(">HHHH", len(PLANETS), len(spare),
                                 len(team_names), len(base_ramp)))

            for c in base_palette:
                fp.write(bytes(c))

            for p in PLANETS:
                fp.write(struct.pack(">H", len(banks[p])))

                for slot, color in banks[p]:
                    fp.write(bytes([slot]) + bytes(color))

            for name in team_names:
                fp.write(bytes(xlat[name]))

        print(f"{out} geschrieben ({out.stat().st_size} Byte); Teams in dieser "
              f"Reihenfolge: {', '.join(team_names)}")


if __name__ == "__main__":
    main()
