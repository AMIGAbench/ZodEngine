#!/usr/bin/env python3
"""Fasst ein Paket-Log (ZOD_PACKET_LOG) zusammen: Pakete je Richtung und
Pakettyp mit Namen aus der tcp_event-Aufzaehlung, Zeitverlauf je 10 s.

Aufruf: tools/golden_summary.py logs/golden/<lauf>.log
"""
import collections
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
EVENT_HEADER = ROOT / "upstream/ZodEgine_Libs/QZod_DnSeparate/event_handler.h"


def tcp_event_names():
    text = EVENT_HEADER.read_text(errors="replace")
    body = re.search(r"enum\s+tcp_event\s*\{(.*?)\};", text, re.S).group(1)
    body = re.sub(r"//.*", "", body)
    return [n.strip() for n in body.split(",") if n.strip() and n.strip() != "MAX_TCP_EVENTS"]


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    names = tcp_event_names()
    count = collections.Counter()
    size = collections.Counter()
    timeline = collections.Counter()
    first = last = None
    with open(sys.argv[1]) as fp:
        for line in fp:
            parts = line.split(" ", 5)
            if len(parts) < 5:
                continue
            t = float(parts[0])
            direction = parts[2]
            pack_id = int(parts[3].split("=")[1])
            n = int(parts[4].split("=")[1])
            key = (direction, pack_id)
            count[key] += 1
            size[key] += n
            timeline[int(t // 10) * 10] += 1
            first = t if first is None else first
            last = t

    if first is None:
        print("keine Pakete")
        return 1
    print(f"Pakete: {sum(count.values())}, Zeitbereich {first:.3f} .. {last:.3f} s")
    print(f"{'Ri':2} {'id':>3} {'Name':28} {'Anzahl':>7} {'Bytes':>9}")
    for (direction, pack_id) in sorted(count, key=lambda k: (k[1], k[0])):
        name = names[pack_id] if pack_id < len(names) else "?"
        print(f"{direction:2} {pack_id:3} {name:28} {count[(direction, pack_id)]:7} {size[(direction, pack_id)]:9}")
    print("Pakete je 10 s: " + " ".join(f"{t}s:{timeline[t]}" for t in sorted(timeline)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
