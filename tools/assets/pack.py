#!/usr/bin/env python3
"""
Packt die Spielgrafiken in wenige Archivdateien (.zpk) im Amiga-Format.

Warum: Das Original öffnet rund 6300 Einzeldateien beim Start. Auf AmigaOS ist
jedes Öffnen teuer (Verzeichnissuche auf FFS/PFS), und PNG müsste zur Laufzeit
dekodiert werden. Die Packs enthalten die Bilder bereits fertig entpackt.

Format (alles Big-Endian, also Amiga-Reihenfolge):

  Kopf      "ZPK1", u32 Anzahl, u32 Offset Namenstabelle, u32 Länge Namenstabelle
  Einträge  je 24 Byte:
              u32 Offset Name (in der Namenstabelle), u16 Länge Name
              u16 Breite, u16 Höhe
              u8  Format (1 = indiziert+Palette, 2 = RGB565)
              u8  Transparenzindex (nur Format 1; 255 = keiner)
              u16 Palettenlänge in Farben (nur Format 1)
              u32 Offset Bilddaten, u32 Länge Bilddaten
              u16 reserviert (0) — füllt den Eintrag auf glatte 24 Byte auf
  Namen     die Originalpfade ("assets/units/...png"), damit der Ladecode der
            Engine unverändert bleiben kann
  Daten     Format 1: Palette (3 Byte je Farbe, RGB) + ein Byte je Pixel
            Format 2: zwei Byte je Pixel, RGB565 big-endian

Indizierte Bilder bleiben indiziert: Der Lader expandiert sie beim Laden nach
RGB565 (eine Tabelle je Bild) und der spätere 8-Bit-Pfad kann sie direkt nutzen.
Nur Bilder mit echten Farbverläufen (Startbild, Porträts) werden als RGB565
abgelegt.

Aufruf:  tools/assets/pack.py [--src data/game/assets] [--out data/game/packs]
"""
import argparse
import pathlib
import struct
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import palette as palette_tool

try:
    from PIL import Image
except ImportError:
    sys.exit("PIL/Pillow wird benötigt: pip install pillow")

FMT_INDEXED = 1
FMT_RGB565 = 2
FMT_PCM8 = 3      # rohes signed 8-Bit-Mono, Abtastrate im Reservefeld
FMT_SHARED8 = 4   # ein Byte je Pixel, Index in die GEMEINSAME Palette

# Index 0 ist der Farbschluessel; alles andere steht in der Palettendatei.
IDX_KEY = 0

# Gruppen -> eigene Archivdatei. Alles andere landet in "misc".
GROUPS = ["units", "buildings", "planets", "fonts", "cursors", "other", "teams"]


def load_palette(path):
    """palette.zpl im Format ZPL2 lesen (siehe tools/assets/palette.py)."""
    d = pathlib.Path(path).read_bytes()

    if d[:4] != b"ZPL2":
        sys.exit(f"{path}: kein ZPL2 -- mit tools/assets/palette.py neu erzeugen")

    n_planets, n_spare, n_teams, ramp_len = struct.unpack(">HHHH", d[4:12])
    off = 12

    base = [tuple(d[off + i * 3: off + i * 3 + 3]) for i in range(256)]
    off += 256 * 3

    banks = []

    for _ in range(n_planets):
        count = struct.unpack(">H", d[off:off + 2])[0]
        off += 2
        entries = []

        for _ in range(count):
            entries.append((d[off], (d[off + 1], d[off + 2], d[off + 3])))
            off += 4

        banks.append(entries)

    teams = []

    for _ in range(n_teams):
        teams.append(list(d[off:off + 256]))
        off += 256

    return {"base": base, "banks": banks, "teams": teams,
            "planets": palette_tool.PLANETS}


def nearest(color, palette):
    """(Platz, Abstand) der naechsten Farbe; Platz 0 bleibt der Farbschluessel."""
    best, best_d = 1, 1 << 30

    for i in range(1, len(palette)):
        c = palette[i]
        d = max(abs(color[0] - c[0]), abs(color[1] - c[1]), abs(color[2] - c[2]))

        if d < best_d:
            best, best_d = i, d

            if d == 0:
                break

    return best, best_d


def convert_shared(path, rel, pal):
    """Bild auf die gemeinsame Palette umindizieren (Format 4).

    Gehoert das Bild zu einem Planeten, gilt dessen Bank: sie ersetzt die
    Plaetze, die im Kern ohnehin frei sind."""
    planet = palette_tool.planet_of(rel)
    table = list(pal["base"])

    if planet and planet in pal["planets"]:
        for slot, color in pal["banks"][pal["planets"].index(planet)]:
            table[slot] = color

    im = Image.open(path)
    w, h = im.size
    px = im.convert("RGBA").load()

    cache = {}
    out = bytearray(w * h)
    err_max = 0
    shifted = 0
    opaque = 0
    i = 0

    for y in range(h):
        for x in range(w):
            r, g, b, a = px[x, y]

            if a < 128:
                out[i] = 0                 # Farbschluessel
                i += 1
                continue

            key = (r, g, b)
            hit = cache.get(key)

            if hit is None:
                hit = nearest(key, table)
                cache[key] = hit

            out[i] = hit[0]
            err_max = max(err_max, hit[1])
            opaque += 1

            if hit[1] > 4:                 # mehr als eine VGA-Stufe daneben
                shifted += 1

            i += 1

    quote = (100.0 * shifted / opaque) if opaque else 0.0

    return FMT_SHARED8, w, h, 0, b"", bytes(out), (err_max, quote)


def rgb565(r, g, b):
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def convert(path):
    """Ein Bild einlesen und als (format, breite, hoehe, transparenzindex,
    palette, pixeldaten) zurückgeben."""
    im = Image.open(path)
    w, h = im.size

    if im.mode == "P":
        pal = im.getpalette() or []
        # Transparenzindex: PNG speichert ihn in der tRNS-Angabe
        trans = im.info.get("transparency", None)
        if isinstance(trans, bytes):
            trans = next((i for i, a in enumerate(trans) if a == 0), None)
        if not isinstance(trans, int):
            trans = 255

        used = max((i for i, _ in ((i, n) for i, n in enumerate(im.histogram()) if n)), default=0)
        colors = min(256, max(used + 1, 1))
        palette = bytes(pal[: colors * 3]).ljust(colors * 3, b"\0")

        return FMT_INDEXED, w, h, trans, palette, im.tobytes()

    # Vollfarbig: nach RGB565, voll transparente Pixel auf Magenta (Farbschlüssel)
    im = im.convert("RGBA")
    out = bytearray(w * h * 2)
    px = im.load()
    i = 0
    for y in range(h):
        for x in range(w):
            r, g, b, a = px[x, y]
            v = 0xF81F if a == 0 else rgb565(r, g, b)   # 0xF81F = Magenta
            out[i] = (v >> 8) & 0xFF
            out[i + 1] = v & 0xFF
            i += 2

    return FMT_RGB565, w, h, 255, b"", bytes(out)


def convert_wav(path):
    """WAV einlesen und als rohe, vorzeichenbehaftete 8-Bit-Mono-Daten
    zurückgeben (Format, Rate, Daten).

    AHI erwartet vorzeichenbehaftete Samples, WAV speichert 8 Bit aber
    vorzeichenlos -- die Umrechnung passiert hier einmalig statt auf dem Amiga.
    """
    d = pathlib.Path(path).read_bytes()
    if len(d) < 44 or d[:4] != b"RIFF" or d[8:12] != b"WAVE":
        return None

    # Chunks durchgehen, statt feste Offsets anzunehmen
    pos = 12
    fmt = None
    data = None
    while pos + 8 <= len(d):
        cid = d[pos:pos+4]
        clen = struct.unpack_from("<I", d, pos + 4)[0]
        body = d[pos+8: pos+8+clen]
        if cid == b"fmt ":
            fmt = struct.unpack_from("<HHIIHH", body, 0)
        elif cid == b"data":
            data = body
        pos += 8 + clen + (clen & 1)

    if not fmt or data is None:
        return None

    _tag, channels, rate, _br, _ba, bits = fmt

    if bits == 8:
        pcm = bytes((b - 128) & 0xFF for b in data)      # unsigned -> signed
    elif bits == 16:
        pcm = bytes(((struct.unpack_from("<h", data, i)[0] >> 8) & 0xFF)
                    for i in range(0, len(data) - 1, 2))  # auf 8 Bit bringen
    else:
        return None

    if channels == 2:                                     # auf Mono mischen
        pcm = bytes(pcm[i] for i in range(0, len(pcm), 2))

    return rate, pcm


def build_sound_entries(src):
    """Alle WAVs als Einträge im Format 3 (rohes signed 8-Bit-Mono).

    Breite und Höhe bleiben 0, die Länge steht ohnehin im Datenfeld, und die
    Abtastrate kommt ins Reservefeld des Eintrags.
    """
    entries = []
    for f in sorted((src / "sounds").glob("*.wav")):
        res = convert_wav(f)
        if not res:
            continue
        rate, pcm = res
        if not pcm:
            continue
        name = str(f.relative_to(src.parent)).replace("\\", "/")
        entries.append((name, FMT_PCM8, 0, 0, 255, b"", pcm, rate))
    return entries


def build_pack(entries, out_path):
    """entries: Liste (name, fmt, w, h, trans, palette, data)"""
    names = bytearray()
    name_off = {}
    for name, *_ in entries:
        if name not in name_off:
            name_off[name] = len(names)
            names += name.encode("latin1")

    header_size = 16
    dir_size = 24 * len(entries)
    names_off = header_size + dir_size
    data_off = names_off + len(names)

    directory = bytearray()
    blobs = bytearray()
    for entry in entries:
        name, fmt, w, h, trans, palette, data = entry[:7]
        extra = entry[7] if len(entry) > 7 else 0    # Reservefeld (Klänge: Rate)
        blob = palette + data
        # genau 24 Byte je Eintrag (letztes Feld ist Reserve) -- die Offsets
        # weiter oben rechnen mit dieser Groesse
        directory += struct.pack(
            ">IHHHBBHIIH",
            name_off[name], len(name), w, h,
            fmt, trans if trans is not None else 255,
            len(palette) // 3,
            data_off + len(blobs), len(blob),
            extra,
        )
        blobs += blob

    with open(out_path, "wb") as fp:
        fp.write(b"ZPK1")
        fp.write(struct.pack(">III", len(entries), names_off, len(names)))
        fp.write(directory)
        fp.write(names)
        fp.write(blobs)

    return header_size + dir_size + len(names) + len(blobs)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--src", default="data/game/assets")
    ap.add_argument("--out", default="data/game/packs")
    ap.add_argument("--palette", default="",
                    help="palette.zpl: alle Bilder auf die GEMEINSAME Palette "
                         "umindizieren (Format 4, ein Byte je Pixel)")
    ap.add_argument("--keep-teams", action="store_true",
                    help="auch die vorgefärbten Teamvarianten aufnehmen "
                         "(normalerweise erzeugt ZTeam::Make sie zur Laufzeit aus Rot)")
    args = ap.parse_args()

    src = pathlib.Path(args.src)
    out = pathlib.Path(args.out)
    out.mkdir(parents=True, exist_ok=True)

    pal = load_palette(args.palette) if args.palette else None

    buckets = {g: [] for g in GROUPS}
    buckets["misc"] = []
    skipped = 0
    team_skipped = 0
    worst = []

    # Blau, Gruen und Gelb liegen zwar als Dateien vor, werden aber nicht
    # geladen: ZTeam::Make erzeugt alle Teamfarben zur Laufzeit aus Rot.
    # Nur in units/ und buildings/ filtern -- anderswo sind Farbwoerter im
    # Namen gewoehnliche Oberflaechenelemente (entry_bar_green, button_yellow).
    TEAM_MARKERS = ("_blue", "_green", "_yellow")
    TEAM_DIRS = ("units", "buildings")

    for f in sorted(src.rglob("*")):
        if f.suffix.lower() not in (".png", ".bmp"):
            continue
        rel = f.relative_to(src.parent)          # "assets/units/..."

        parts = f.relative_to(src).parts
        in_team_dir = len(parts) > 1 and parts[0] in TEAM_DIRS

        if not args.keep_teams and in_team_dir and any(m in f.name.lower() for m in TEAM_MARKERS):
            team_skipped += 1
            continue

        group = f.relative_to(src).parts[0] if len(f.relative_to(src).parts) > 1 else "misc"
        if group not in buckets:
            group = "misc"
        try:
            rel_src = str(f.relative_to(src)).replace("\\", "/")

            #Ausgenommen bleiben NUR die Palettendateien der Teams: ZTeam liest
            #aus ihnen Farbpaare, Umindizieren wuerde sie verfaelschen. Sie
            #werden nie geblittet, ihre Farbtiefe ist also gleichgueltig.
            #
            #Alles andere geht auf die gemeinsame Palette -- auch das Startbild
            #und die Fabrikvorschau. Sie bleiben zwar aus der MESSUNG der
            #Palette heraus (sie wuerden sie mit Verlaufsfarben verderben,
            #siehe palette.py), aber ein 16-Bit-Bild hat auf einem 8-Bit-Schirm
            #nichts verloren: der Blitter kann 16->8 nicht.
            shared_ok = pal and not rel_src.lower().startswith("teams/")

            if shared_ok:
                fmt, w, h, trans, palette, data, e = convert_shared(f, rel_src, pal)

                worst.append((e[1], e[0], rel_src))
            else:
                fmt, w, h, trans, palette, data = convert(f)
        except Exception as exc:                  # defekte Datei überspringen
            print(f"  übersprungen: {rel} ({exc})")
            skipped += 1
            continue
        buckets[group].append((str(rel).replace("\\", "/"), fmt, w, h, trans, palette, data))

    # Klänge in ein eigenes Archiv: auf dem Amiga entfallen damit 261
    # Dateiöffnungen und das Zerlegen der WAV-Köpfe.
    sound_entries = build_sound_entries(src)

    total = 0
    print(f"{'Archiv':14} {'Bilder':>7} {'Größe':>12}")
    for group, entries in buckets.items():
        if not entries:
            continue
        size = build_pack(entries, out / f"zod_{group}.zpk")
        total += size
        print(f"zod_{group + '.zpk':10} {len(entries):7} {size / 1024 / 1024:9.1f} MB")

    if sound_entries:
        size = build_pack(sound_entries, out / "zod_sounds.zpk")
        total += size
        print(f"zod_sounds.zpk {len(sound_entries):7} {size / 1024 / 1024:9.1f} MB  (signed 8 Bit mono)")

    print(f"\nGesamt: {total / 1024 / 1024:.1f} MB, übersprungen: {skipped}")
    if pal:
        worst.sort(reverse=True)
        n_bad = sum(1 for q, _, _ in worst if q > 5.0)
        print(f"Gemeinsame Palette: {n_bad} von {len(worst)} Bildern mit mehr als "
              f"5 % verschobenen Pixeln")
        print("  Anteil  maxFehler  Datei")
        for q, e, name in worst[:10]:
            print(f"  {q:5.1f}%  {e:9d}  {name}")
    if team_skipped:
        print(f"Teamvarianten ausgelassen (werden zur Laufzeit erzeugt): {team_skipped}")
    print(f"Archive in {out}")


if __name__ == "__main__":
    main()
