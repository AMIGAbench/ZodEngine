#!/usr/bin/env bash
# Baut das Projekt.
#   tools/build.sh host [make-Ziele...]              -> lokal mit g++ und SDL 1.2
#   tools/build.sh amiga [CPU=68040] [make-Ziele...] -> im Docker-Image der Cross-Toolchain
#
# Warum Docker: Bebbo gcc 6.5.0b, NDK und libdebug existieren nur im Image.
# Das Image-Entrypoint verwirft stdin, deshalb wird make direkt aufgerufen.
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

# Die eingebettete Zuordnungstabelle von ZExtract ist erzeugter Quelltext
# (1,2 MB) und gehoert deshalb nicht ins Repository -- erzeugt wird sie hier,
# aus den versionierten Tabellen in tools/cd/. Nur wenn noetig: ein Neuschreiben
# wuerde make jedes Mal neu uebersetzen lassen.
TBL=tools/cd/amiga/tables.c

if [ ! -f "$TBL" ] \
   || [ tools/cd/names.txt  -nt "$TBL" ] \
   || [ tools/cd/sounds.txt -nt "$TBL" ] \
   || [ tools/cd/xlat.txt   -nt "$TBL" ] \
   || [ tools/cd/gen_tables.py -nt "$TBL" ]; then
  python3 tools/cd/gen_tables.py || exit 3
fi

PLATFORM="${1:-host}"
shift || true
IMG="${AMIGA_IMAGE:-amigadev/crosstools:m68k-amigaos}"
JOBS="${JOBS:-$(nproc)}"

case "$PLATFORM" in
  host)
    make -j"$JOBS" PLATFORM=host "$@"
    ;;
  amiga)
    docker run --rm \
      --user "$(id -u):$(id -g)" -e HOME=/tmp \
      -v "$ROOT":/work -w /work "$IMG" \
      make -j"$JOBS" PLATFORM=amiga "$@"
    ;;
  *)
    echo "Aufruf: tools/build.sh host|amiga [CPU=680x0] [Ziele...]" >&2
    exit 3
    ;;
esac
rc=$?
if [ $rc -ne 0 ]; then
  echo "==== BUILD FEHLGESCHLAGEN ($PLATFORM, rc=$rc) ====" >&2
  exit $rc
fi

# Uebersetzerfehler-Wache: "btst #<n>,<speicher>" mit n ausserhalb 0..7.
#
# gcc 6.5.0b erzeugt fuer "flag & 0x1000" bei einem 32-Bit-Parameter
# "btst #-12,12(a5)". btst prueft auf einem Speicheroperanden aber nur EIN
# Byte (Bitnummer modulo 8); fuer Bit 12 muesste die Adresse um zwei Byte
# weiterruecken. Die Bedingung ist damit immer falsch -- am 18.09. war das die
# Ursache dafuer, dass KEIN Bild seinen Farbschluessel bekam und jedes Objekt
# einen schwarzen Kasten hatte.
#
# Auf ein REGISTER ("btst #12,d0") ist es harmlos: dort gilt modulo 32.
# Betroffen ist vor allem -m68040; -m68060 traf es an einer Stelle.
#
# Gesucht wird gezielt die NEGATIVE Bitnummer mit einfacher Adressierung ueber
# ein Adressregister -- genau so schreibt gcc den Fehler. Weiter zu fassen
# taugt nicht: objdump zerlegt auch die Datenbereiche und erfindet dort
# Befehle wie "btst #60,72(a6)" oder "btst #-8,(126879606,d0.l*2)".
#
# Auch die enge Fassung reicht am fertigen Hunk nicht aus: ZExtract traegt
# 188 KB konstante Tabellen, die im Hunk in .text liegen, und dort erfand
# objdump ein "btst #-59,32(a3)" mitten in den Daten. Deshalb wird von
# ZExtract die OBJEKTdatei geprueft -- dort sind Code und Daten getrennt.
if [ "$PLATFORM" = amiga ]; then
  # nur den gerade gebauten Baum pruefen -- ein alter Baum daneben ist kein
  # Befund ueber diesen Bau (genau darauf ist die Wache beim ersten Lauf
  # hereingefallen und meldete einen laengst veralteten Diagnose-Bau).
  cpu=68040
  diag=
  for a in "$@"; do
    case "$a" in
      CPU=*)  cpu=${a#CPU=} ;;
      DIAG=1) diag=-diag ;;
    esac
  done

  # Alle eigenstaendigen Programme des Baums pruefen, nicht nur die Engine:
  # Der btst-Fehler der Werkzeugkette trifft jeden Bau, der ein Bit ueber 7
  # in einem 32-Bit-Parameter testet.
  #
  # Von ZExtract wird die OBJEKTdatei geprueft, nicht das Binary. Im fertigen
  # Hunk liegt seine eingebettete Zuordnungstabelle (188 KB) in .text, und
  # objdump zerlegt Daten wie Befehle -- das meldete prompt ein "btst #-59"
  # mitten in der Tabelle. Am Objekt sind Code und Daten getrennt.
  #
  # Dasselbe gilt seit dem AMMX-Modul fuer die Engine selbst: AMMX-Opcodes
  # liegen im Line-F-Bereich ($FExx), objdump kennt sie nicht, gibt sie als
  # ".short" aus und zerlegt die FOLGENDEN Woerter falsch. Schon im heutigen
  # Stand erfindet es dort "btst d4,d1" und "or.b -(a0),d5" -- beides mit
  # Registeroperanden und damit unterhalb des Musters, aber das ist Glueck,
  # keine Zusage. Deshalb wird im 68080-Baum die Engine ueber ihre
  # OBJEKTdateien geprueft (ohne das vasm-Objekt, das absichtlich AMMX
  # enthaelt) statt ueber das gebundene Programm.
  # Ein Ausgabeverzeichnis fuer alle CPUs, die Ergebnisse heissen zod_040 usw.
  # Die Objekte liegen je CPU getrennt in obj-<cpu>/ bzw. cd-<cpu>/.
  aus="build/amiga$diag"
  suf="${cpu#68}"

  ziele="$aus/zod_$suf"

  if [ -f "$aus/obj-$cpu/port/amiga/ammx_blit.o" ]; then
    ziele=$(find "$aus/obj-$cpu" -name '*.o' ! -name 'ammx_blit.o' 2>/dev/null)

    if [ -z "$ziele" ]; then
      echo "==== FEHLER: keine Objektdateien zum Pruefen gefunden" >&2
      exit 4
    fi
  fi

  for bin in $ziele "$aus/cd-$cpu/zextract.o" \
             "$aus/ZodLaunch"; do
    [ -f "$bin" ] || continue

    treffer=$(docker run --rm --user "$(id -u):$(id -g)" -e HOME=/tmp \
                -v "$ROOT":/work -w /work "$IMG" \
                m68k-amigaos-objdump -d "$bin" 2>/dev/null \
              | grep -E "btst #-[0-9]+,-?[0-9]+\(a[0-7]\)" || true)

    if [ -n "$treffer" ]; then
      echo "==== FEHLER: $bin enthaelt btst auf Speicher mit Bitnummer ausserhalb 0..7" >&2
      echo "$treffer" | head -5 >&2
      echo "     Das prueft das falsche Byte und ist IMMER falsch." >&2
      exit 4
    fi
  done
fi

echo "==== BUILD OK ($PLATFORM) ===="
