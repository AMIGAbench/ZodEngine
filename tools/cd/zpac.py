#!/usr/bin/env python3
"""Leser für den Container z.pac der Original-CD von Z (Bitmap Brothers, 1996).

Warum diese Datei existiert
---------------------------
Das Auslieferungsmodell sieht vor, dass nur die Engine ausgeliefert wird und der
Nutzer die Daten von seiner eigenen CD holt. Dafür braucht es einen Extraktor.
Eine frühere Analyse dieses Formats lag nur in einem Arbeitsverzeichnis und ist
verlorengegangen -- deshalb steht das Ergebnis diesmal im Baum, mit klarer
Trennung zwischen dem, was BELEGT ist, und dem, was noch offen ist.

Belegt (nachgemessen am 18.09. an /media/cem/Z_280796/z.pac, 5 420 010 Byte)
---------------------------------------------------------------------------
Kopf, 8 Byte, durchgehend Little-Endian (DOS-Herkunft):

    0  char[2]  Kennung "NI"
    2  u16      277        -> Bedeutung UNKLAR, es sind NICHT die 343 Einträge
    4  u32      22 494 568 -> Bedeutung UNKLAR, auch nicht die Summe der Größen

Verzeichnis ab **0x81**, **29 Byte** je Eintrag, **343 Einträge**:

     0  char[12] Name im DOS-Format 8+3, mit Leerzeichen aufgefüllt ("ZED     EXE ")
    12  u32      Zeitstempel im DOS-Format (Datum im Hochwort)
    16  u32      Größe der ENTPACKTEN Datei
    20  u8       immer 1
    21  u32      **Ende** der gepackten Daten dieses Eintrags (nicht der Anfang!)
    25  u32      0x00000001 oder 0x00000002, Bedeutung unklar

Daraus folgt der Aufbau:

    Datenbeginn      = 0x81 + 343 * 29 = 0x275c = 10 076
    Daten Eintrag k  = [ Ende(k-1) , Ende(k) )     mit Ende(-1) = Datenbeginn

Der **letzte** Eintrag ist verkürzt: Name, Zeitstempel, Größe und Kennbyte,
aber **kein Endfeld** -- seine Daten laufen bis zum Dateiende. Der Datenbeginn
liegt deshalb 8 Byte früher als die einfache Rechnung sagt:

    Datenbeginn = 0x81 + (343−1) · 29 + 21 = 10 068

Probe: Nur so bekommt der erste Eintrag (SAMPLE.AD) 2770 Byte, und der Block
dort meldet 2765 + 5 = 2770 -- aufs Byte. Ebenso fehlt dem allerletzten Block
am Dateiende sein Nachspannbyte. Das waren die **zwei** Einträge, die sich
lange nicht entpacken ließen; seither sind es **343 von 343**.

Gegenproben, die aufgehen:
  * alle 343 Namen bestehen aus druckbaren Zeichen,
  * die Enden steigen monoton (das ENDE des letzten Eintrags ist unbrauchbar,
    seine Daten laufen bis zum Dateiende -- 266 Byte für 306 entpackt),
  * **für alle 343 Einträge liegt das Verhältnis gepackt/entpackt zwischen
    0,05 und 1,3** -- das ist die eigentliche Bestätigung des Aufbaus.

Blockrahmen innerhalb eines Eintrags:

     0  u8    unklar (wechselt je Block)
     1  u8    Verfahren: **1 = unkomprimiert, 8 = gepackt (weitere folgen),
                          9 = gepackt (letzter Block)**
     2  u16   Länge der Nutzlast
     4  ...   Nutzlast
           +1 Byte hinter der Nutzlast (Prüfsumme? unklar)

Also: Blockgröße = 4 + Länge + 1. Die Blöcke eines Eintrags liegen lückenlos
hintereinander.

**Bewiesen an echtem Klartext:** Zwölf Blöcke tragen Verfahren 1, alle aus
Klangdateien. Ihre Nutzlast ab Versatz 4 ist **byteweise identisch** mit den
entsprechenden `.wav` im Bestand (PINGU2, LASERGUN, LAVALOOP geprüft --
8 Bit, mono, 11025 Hz, gleiche Länge, gleiche Bytes). Damit steht fest, dass
die Nutzlast bei 4 beginnt und ein Byte hinten übrig bleibt.

Verfahren über das ganze Archiv: 9 (331x), 8 (122x), 1 (12x).

**Ein Block entpackt zu 65 000 Byte** (der letzte weniger). Gegenprobe:
SPRITES.RSC 3 920 557 -> 61 Blöcke, ZED.EXE 2 316 209 -> 36, DOS4GW.EXE
265 420 -> 5, LOADER.LBM 64 824 -> 1. Alle vier passen auf ceil(Größe/65000).

Die Zahl der Einträge steht nirgends im Kopf und wird deshalb abgetastet:
Name druckbar und Ende monoton, bis es bricht.

> **Zweimal richtiggestellt am selben Tag, beide Male durch die Gegenproben:**
> Zuerst stand hier, das Feld bei 21 sei der ANFANG der Daten und es gebe 277
> Einträge -- dann bekam SPRITES.RSC 179 Byte für 3,9 MB. Danach 342 Einträge,
> womit der erste Eintrag noch Verzeichnisreste enthielt (sein Strom begann
> sichtbar mit "H_INIT  XMI"). Richtig sind **343** Einträge und das Feld ist
> das ENDE. Erst damit stimmen alle Verhältnisse.
>
> Die Lehre steht hier, weil sie sich wiederholt hat: Eine falsche Struktur
> bestätigt sich selbst, solange man nur an ihr entlang misst. Erst eine Probe,
> die etwas Unabhängiges prüft -- hier "ist das Verhältnis physikalisch
> möglich?" -- bricht das auf.

Das Packverfahren -- GEKNACKT
----------------------------
Es ist keines der Standardverfahren (zlib, rohes Deflate, gzip, bz2, lzma, LZW
in acht Varianten, LZHUF nach Okumura, adaptives Huffman nach FGK, LHA -lh1-
bis -lh7- -- alle geprüft, alle abgelehnt), und eine öffentliche Beschreibung
gibt es nicht. Erschlossen wurde es am 18.09. gegen **echten Klartext**: Die
`.wav` des vorhandenen Bestands sind bitgleich die `.RAW` der CD, das ergibt
200 Paare aus Klartext und Geheimtext. Beschreibung und Prüfstand stehen in
`nicecomp.py`; 199 der 200 Dateien kommen bitgleich heraus.

Damit sind auch `SPRITES.RSC` (siehe `sprites.py`) und die Klänge lesbar, und
beide Extraktoren stehen: `extract.py` (Linux) und `amiga/zextract.c`.

Was drin steckt (aus dem Verzeichnis, gesichert)
------------------------------------------------
    211x RAW   Klangdaten            80x XMI   Musik
     15x MDI   Musiktreiber          10x DIG   Klangtreiber
      7x EXE   Programme              7x LST   Listen
      2x LBM   Bilder (LOADER.LBM = Ladebild, 64 824 Byte)
      1x RSC   **SPRITES.RSC, 3 920 557 entpackt -> 1 608 332 gepackt** (0,41)
      1x PAC   DOOR.PAC (Container im Container)

Aufruf
------
    tools/cd/zpac.py --dump  [--cd /media/cem/Z_280796]
    tools/cd/zpac.py --list  [--cd ...]           nur die Dateiliste
    tools/cd/zpac.py --blocks SPRITES.RSC         Blockkette eines Eintrags
"""
import argparse
import collections
import pathlib
import struct
import sys

HEADER_SIZE = 8
DIR_START = 0x81
ENTRY_SIZE = 29

# Der letzte Eintrag hat kein Endfeld und keinen Nachspann (siehe ZPac.__init__).
LAST_ENTRY_SIZE = 21

# Grenzen, innerhalb derer ein Verhältnis gepackt/entpackt glaubhaft ist.
# Darüber wird die abgetastete Eintragszahl geprüft -- bei einer falschen
# Lesart fällt das sofort auf (siehe Richtigstellung im Modulkopf).
RATIO_MIN, RATIO_MAX = 0.05, 1.3

# Ein Block entpackt zu so vielen Byte (der letzte weniger). Belegt an vier
# Einträgen: die Blockzahl trifft jeweils ceil(Größe / 65000).
BLOCK_PLAIN = 65000


class Block:
    """Ein Block: 4 Byte Kopf, Nutzlast, ein Byte hinterher."""
    __slots__ = ("pos", "b0", "method", "length")

    STORED = 1
    PACKED_MORE = 8
    PACKED_LAST = 9

    def __init__(self, pos, b0, method, length):
        self.pos = pos
        self.b0 = b0
        self.method = method
        self.length = length

    @property
    def data_at(self):
        return self.pos + 4


class Entry:
    __slots__ = ("index", "name", "stamp", "size", "flag", "offset", "tail", "start")

    def __init__(self, index, raw):
        self.index = index
        self.name = raw[0:12].decode("latin1")
        self.stamp, self.size = struct.unpack("<II", raw[12:20])
        self.flag = raw[20]
        self.offset = struct.unpack("<I", raw[21:25])[0]
        self.tail = struct.unpack(">I", raw[25:29])[0]
        self.start = None          # wird von ZPac gesetzt

    @property
    def packed(self):
        """Laenge der gepackten Daten. offset ist das ENDE, start der Anfang."""
        return self.offset - self.start

    @property
    def dos_name(self):
        """'ZED     EXE ' -> 'ZED.EXE'"""
        stem = self.name[0:8].strip()
        ext = self.name[8:11].strip()
        return f"{stem}.{ext}" if ext else stem


class ZPac:
    def __init__(self, path):
        self.path = pathlib.Path(path)
        self.data = self.path.read_bytes()

        if self.data[0:2] != b"NI":
            raise ValueError(f"{path}: keine NI-Kennung, ist das z.pac?")

        self.head_u16, self.head_u32 = struct.unpack("<HI", self.data[2:8])
        self.entries = self._scan()
        # Der LETZTE Verzeichniseintrag ist verkuerzt: Er traegt Name,
        # Zeitstempel, Groesse und das Kennbyte -- aber kein Endfeld, denn seine
        # Daten laufen bis zum Dateiende. Damit beginnen die Daten 8 Byte
        # frueher, als die einfache Rechnung sagt.
        #
        # Das ist keine Vermutung: Nur mit diesem Anfang bekommt der erste
        # Eintrag (SAMPLE.AD) 2770 Byte, und der Block dort meldet
        # 2765 + 5 = 2770 -- aufs Byte. Vorher war SAMPLE.AD einer von zwei
        # Eintraegen, die sich nicht entpacken liessen.
        self.dir_end = (DIR_START + (len(self.entries) - 1) * ENTRY_SIZE
                        + LAST_ENTRY_SIZE)

        # Ende(k-1) ist der Anfang von k, fuer k=0 der Datenbeginn.
        anfang = self.dir_end

        for e in self.entries:
            e.start = anfang
            anfang = e.offset

    def _scan(self):
        """Die Eintragszahl steht NICHT im Kopf -- sie wird abgetastet.

        Abbruch, sobald ein Name nicht mehr druckbar ist oder das Ende nicht
        mehr monoton steigt. Beides trifft unmittelbar hinter dem Verzeichnis
        zu, weil dort die gepackten Daten beginnen."""
        out = []
        letztes = -1
        i = 0

        while True:
            off = DIR_START + i * ENTRY_SIZE

            if off + ENTRY_SIZE > len(self.data):
                break

            roh = self.data[off:off + ENTRY_SIZE]

            if not all(32 <= c < 127 for c in roh[0:12]):
                break

            ende = struct.unpack("<I", roh[21:25])[0]

            # Das ENDE des letzten Eintrags ist unbrauchbar (im geprüften
            # Archiv 181 209 573). Der Eintrag zählt trotzdem -- seine Daten
            # laufen bis zum Dateiende. Erkannt daran, dass danach kein
            # druckbarer Name mehr folgt.
            if ende < letztes or ende > len(self.data):
                folgt = self.data[off + ENTRY_SIZE:off + 2 * ENTRY_SIZE]

                if len(folgt) >= 12 and all(32 <= c < 127 for c in folgt[0:12]):
                    break

                e = Entry(i, roh)
                e.offset = len(self.data)
                out.append(e)
                break

            letztes = ende
            out.append(Entry(i, roh))
            i += 1

        return out

    @property
    def count(self):
        return len(self.entries)

    # ------------------------------------------------------------- Pruefungen
    def check(self):
        """Gegenproben. Die dritte ist die eigentliche Bestaetigung des Aufbaus."""
        out = []

        lesbar = sum(1 for e in self.entries if all(32 <= ord(c) < 127 for c in e.name))
        out.append((lesbar == self.count,
                    f"alle Namen druckbar: {lesbar} von {self.count}"))

        monoton = sum(1 for a, b in zip(self.entries, self.entries[1:])
                      if b.offset >= a.offset)
        out.append((monoton == self.count - 1,
                    f"Enden monoton: {monoton} von {self.count - 1}"))

        gut = sum(1 for e in self.entries
                  if e.size and RATIO_MIN <= e.packed / e.size <= RATIO_MAX)
        out.append((gut == self.count,
                    f"Verhaeltnis gepackt/entpackt zwischen {RATIO_MIN} und "
                    f"{RATIO_MAX}: {gut} von {self.count}"))

        rest = len(self.data) - self.entries[-1].offset
        out.append((0 <= rest < 4096,
                    f"Rest hinter dem letzten Eintrag: {rest} Byte"))

        # Der letzte Eintrag ist ausgenommen: sein Ende-Feld ist unbrauchbar,
        # und das Feld bei 20 traegt dort ebenfalls Muell (13 statt 1).
        flags = {e.flag for e in self.entries[:-1]}
        out.append((flags == {1},
                    f"Feld bei 20 immer 1 (ohne den letzten): {sorted(flags)}"))

        ganz = 0

        for e in self.entries:
            if self.block_chain(e) is not None:
                ganz += 1

        out.append((ganz == self.count,
                    f"Blockkette trifft das Eintragsende: {ganz} von {self.count}"))

        return out

    def block_chain(self, entry):
        """Blöcke eines Eintrags als (Kopfbyte0, letzter?, Länge).

        None, wenn die Kette das Eintragsende nicht genau trifft -- dann
        stimmt die Deutung des Rahmens nicht."""
        pos = entry.start
        out = []

        while pos < entry.offset:
            if pos + 4 > len(self.data):
                return None

            b0, art, laenge = struct.unpack("<BBH", self.data[pos:pos + 4])
            out.append(Block(pos, b0, art, laenge))
            pos += 4 + laenge + 1

        if pos == entry.offset:
            return out

        # Dem allerletzten Block fehlt am Dateiende sein Nachspannbyte. Das
        # war der zweite der beiden Eintraege, die sich nicht entpacken
        # liessen (H_INIT.XMI). Eine Abweichung von genau einem Byte am
        # Dateiende ist deshalb erlaubt -- mehr nicht.
        if pos == entry.offset + 1 and entry.offset == len(self.data):
            return out

        return None

    def blob(self, entry):
        """Die gepackten Bytes eines Eintrags."""
        return self.data[entry.start:entry.offset]

    def extract(self, entry):
        """Inhalt eines Eintrags -- nur solange alle Bloecke unkomprimiert sind.

        Gibt None zurueck, sobald ein gepackter Block dabei ist: Das Verfahren
        dafuer ist noch nicht geknackt (siehe Modulkopf)."""
        bl = self.block_chain(entry)

        if bl is None:
            return None

        out = bytearray()

        for b in bl:
            if b.method != Block.STORED:
                return None

            out += self.data[b.data_at:b.data_at + b.length]

        return bytes(out) if len(out) == entry.size else None


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--cd", default="/media/cem/Z_280796",
                    help="Wurzel der gemounteten CD oder Verzeichnis mit z.pac")
    ap.add_argument("--dump", action="store_true", help="Kopf, Gegenproben, Uebersicht")
    ap.add_argument("--list", action="store_true", help="alle Eintraege auflisten")
    ap.add_argument("--head", metavar="NAME", help="erste Bytes eines Eintrags zeigen")
    args = ap.parse_args()

    root = pathlib.Path(args.cd)
    path = root / "z.pac" if (root / "z.pac").exists() else root

    if not path.exists():
        sys.exit(f"z.pac nicht gefunden unter {root}")

    pac = ZPac(path)

    if args.dump or not (args.list or args.head):
        print(f"Datei       {path}  ({len(pac.data)} Byte)")
        print(f"Kennung     NI")
        print(f"Kopf u16    {pac.head_u16}   (nicht die Eintragszahl)")
        print(f"Kopf u32    {pac.head_u32}   (Bedeutung unklar)")
        print(f"Eintraege   {pac.count}   (abgetastet, steht nirgends im Kopf)")
        print(f"Verzeichnis 0x{DIR_START:x} .. 0x{pac.dir_end:x}, {ENTRY_SIZE} Byte je Eintrag")
        print(f"Daten ab    0x{pac.dir_end:x} = {pac.dir_end}")
        print()
        print("Gegenproben:")

        for ok, text in pac.check():
            print(f"  [{'ok  ' if ok else 'NEIN'}] {text}")

        ext = collections.Counter(e.name[8:11].strip() for e in pac.entries)
        print()
        print("Endungen:", ", ".join(f"{k or '(ohne)'}={v}" for k, v in sorted(ext.items())))

        entpackt = sum(e.size for e in pac.entries)
        gepackt = sum(e.packed for e in pac.entries)
        print(f"Summe entpackt {entpackt}, gepackt {gepackt}, "
              f"Verhaeltnis {gepackt / entpackt:.2f}")
        print()
        print("Groesste Eintraege:")

        for e in sorted(pac.entries, key=lambda x: -x.size)[:8]:
            bl = pac.block_chain(e)
            soll = -(-e.size // BLOCK_PLAIN)
            print(f"  {e.dos_name:14s} entpackt {e.size:9d}  gepackt {e.packed:9d}"
                  f"  {e.packed / e.size:5.2f}  Bloecke {len(bl) if bl else '?':>3}"
                  f" (erwartet {soll})")

    if args.list:
        print(f"{'idx':>4s} {'Name':14s} {'entpackt':>9s} {'gepackt':>9s} {'Anfang':>9s} {'Ende':>9s}")

        for e in pac.entries:
            print(f"{e.index:4d} {e.dos_name:14s} {e.size:9d} {e.packed:9d}"
                  f" {e.start:9d} {e.offset:9d}")

    if args.head:
        ziel = [e for e in pac.entries if e.dos_name.upper() == args.head.upper()]

        if not ziel:
            sys.exit(f"kein Eintrag namens {args.head}")

        e = ziel[0]
        b = pac.blob(e)
        print(f"{e.dos_name}: entpackt {e.size}, gepackt {e.packed}, "
              f"Anfang {e.start}, Ende {e.offset}")

        for zeile in range(0, min(64, len(b)), 16):
            teil = b[zeile:zeile + 16]
            lesbar = "".join(chr(c) if 32 <= c < 127 else "." for c in teil)
            print(f"  {zeile:04x}  {teil.hex(' '):48s}  {lesbar}")


if __name__ == "__main__":
    main()
