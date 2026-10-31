"""Finds free wall ends that meet another wall without a shared node: the T-joints where cut ends leave chamfers
and burrs. Usage: check_wall_joints.py [blueprint.json] (default: assets/station/blueprints/hub.json)."""
import json
import math
import sys
from pathlib import Path

DEFAULT = Path(__file__).resolve().parents[4] / "assets" / "station" / "blueprints" / "hub.json"
MIN_WALL_HEIGHT = 8  # shorter sticks are floors and ceilings, which overlap walls on purpose
NODE_TOLERANCE = 1.0  # cells: two nodes this close are one node
REACH_MARGIN = 1.0  # cells beyond the thickness that still count as touching


def point(node):
    return node["x"], node["y"]


def distance_to_segment(p, a, b):
    ax, ay = a
    bx, by = b
    dx, dy = bx - ax, by - ay
    length2 = dx * dx + dy * dy
    t = 0.0 if length2 == 0 else max(0.0, min(1.0, ((p[0] - ax) * dx + (p[1] - ay) * dy) / length2))
    return math.hypot(p[0] - (ax + t * dx), p[1] - (ay + t * dy))


def z_range(wall):
    return wall["node_a"]["z"], wall["node_a"]["z"] + min(wall["length_a"], wall["length_b"])


def main():
    path = Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT
    walls = [w for w in json.loads(path.read_text())["walls"] if min(w["length_a"], w["length_b"]) >= MIN_WALL_HEIGHT]
    ranges = [z_range(w) for w in walls]
    found = 0
    for i, wall in enumerate(walls):
        low, high = z_range(wall)
        for key in ("node_a", "node_b"):
            p = point(wall[key])
            if any(math.dist(p, point(n)) <= NODE_TOLERANCE for j, other in enumerate(walls) if j != i
                   and ranges[j][1] > low and ranges[j][0] < high for n in (other["node_a"], other["node_b"])):
                continue  # shared with another wall's node: the game stitches it
            for j, other in enumerate(walls):
                other_low, other_high = ranges[j]
                if i == j or other_high <= low or other_low >= high:
                    continue
                reach = abs(other["thickness"]) + REACH_MARGIN
                gap = distance_to_segment(p, point(other["node_a"]), point(other["node_b"]))
                if gap <= reach:
                    found += 1
                    print(f"wall {i} {key} {p} z {low}-{high} meets wall {j} ({gap:.1f} cells from its line) "
                          "without a shared node")
    print(f"{found} free end(s) at another wall")
    return 1 if found else 0


if __name__ == "__main__":
    sys.exit(main())
