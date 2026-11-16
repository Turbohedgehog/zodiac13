"""Lists the bends of wall chains: a node two walls of one deck share where the wall turns by a few degrees to 60.
Each bend is mitred into a panel whose corner shows as a tooth; a straighter chord, or a turn made on the lattice,
removes it. Usage: check_wall_bends.py [BLUEPRINT.json]; exit 1 when any bend is listed."""
import json
import math
import sys
from collections import defaultdict

PATH = "assets/station/blueprints/hub.json"
MIN_TURN, MAX_TURN = 5.0, 60.0


def main():
    blueprint = json.load(open(sys.argv[1] if len(sys.argv) > 1 else PATH, encoding="utf-8"))
    at = defaultdict(list)
    for wall in blueprint["walls"]:
        a, b = wall["node_a"], wall["node_b"]
        at[(a["x"], a["y"], a["z"])].append((b["x"] - a["x"], b["y"] - a["y"]))
        at[(b["x"], b["y"], b["z"])].append((a["x"] - b["x"], a["y"] - b["y"]))
    bends = []
    for node, directions in at.items():
        if len(directions) != 2:
            continue
        (ax, ay), (bx, by) = directions
        turn = 180.0 - math.degrees(math.acos(max(-1, min(1, (ax * bx + ay * by) / math.hypot(ax, ay) / math.hypot(bx, by)))))
        if MIN_TURN <= turn <= MAX_TURN:
            bends.append((node, turn))
    by_place = defaultdict(list)
    for (x, y, z), turn in sorted(bends):
        by_place[(x, y)].append((z, turn))
    for (x, y), levels in sorted(by_place.items()):
        print(f"bend at ({x}, {y}) turn {levels[0][1]:.0f} deg, decks z {', '.join(str(z) for z, _ in levels)}")
    print(f"{len(by_place)} bend place(s)")
    return 1 if by_place else 0


if __name__ == "__main__":
    sys.exit(main())
