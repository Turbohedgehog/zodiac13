"""Finds double walls: two wall segments, parallel, on the same decks, whose bodies touch or lie closer than
MAX_GAP cells (not walls sharing one volume) with a stretch side by side. Every grid is taken into the main grid's plane (origin, yaw).
Usage: check_double_walls.py [blueprint.json] (default: assets/station/blueprints/hub.json)."""
import json
import math
import sys
from pathlib import Path

DEFAULT = Path(__file__).resolve().parents[4] / "assets" / "station" / "blueprints" / "hub.json"
MIN_WALL_HEIGHT = 8  # shorter sticks are floors and ceilings
MAX_ANGLE_DEGREES = 3.0
MAX_GAP = 3.0  # cells between the two bodies that still make a sliver nobody can use
MIN_SIDE_BY_SIDE = 4.0  # cells along the walls that face each other
MIN_COMMON_HEIGHT = 8
TOUCH_TOLERANCE = 0.5  # cells: bodies touching make one thick wall, no sliver
SHARED_TOLERANCE = 1.0  # cells: walls overlapping to within this of their thickness are one wall
NODE_TOLERANCE = 1.0  # walls sharing a node meet end to end


def to_world(grid, x, y):
    yaw = math.radians(grid["yaw_degrees"])
    return (grid["origin"]["x"] + x * math.cos(yaw) - y * math.sin(yaw),
            grid["origin"]["y"] + x * math.sin(yaw) + y * math.cos(yaw))


def wall_of(grid, index, wall):
    a = to_world(grid, wall["node_a"]["x"], wall["node_a"]["y"])
    b = to_world(grid, wall["node_b"]["x"], wall["node_b"]["y"])
    z0 = wall["node_a"]["z"] + grid["origin"]["z"]
    return {"grid": grid["id"], "index": index, "a": a, "b": b, "t": wall["thickness"], "z": (z0, z0 + min(
        wall["length_a"], wall["length_b"]))}


def frame(wall):
    dx, dy = wall["b"][0] - wall["a"][0], wall["b"][1] - wall["a"][1]
    length = math.hypot(dx, dy)
    return (dx / length, dy / length), (-dy / length, dx / length), length


def along_and_across(wall, point):
    u, n, _ = frame(wall)
    px, py = point[0] - wall["a"][0], point[1] - wall["a"][1]
    return px * u[0] + py * u[1], px * n[0] + py * n[1]


def check(first, second):
    u1, n1, l1 = frame(first)
    u2, _, l2 = frame(second)
    cosine = abs(u1[0] * u2[0] + u1[1] * u2[1])
    if math.degrees(math.acos(min(1.0, cosine))) > MAX_ANGLE_DEGREES:
        return None
    low = max(first["z"][0], second["z"][0])
    high = min(first["z"][1], second["z"][1])
    if high - low < MIN_COMMON_HEIGHT:
        return None
    if any(math.dist(p, q) <= NODE_TOLERANCE for p in (first["a"], first["b"]) for q in (second["a"], second["b"])):
        return None
    s_a, c_a = along_and_across(first, second["a"])
    s_b, c_b = along_and_across(first, second["b"])
    start, end = max(0.0, min(s_a, s_b)), min(l1, max(s_a, s_b))
    if end - start < MIN_SIDE_BY_SIDE:
        return None
    # The first wall's body spans from its line to `t` across; the other's line is at c (a line leaning is its mean).
    near, far = sorted((first["t"], 0.0))
    other_near, other_far = sorted((c_a + (0 if second["t"] > 0 else second["t"]),
                                    c_a + (second["t"] if second["t"] > 0 else 0)))
    if abs(c_a - c_b) > 1.0:
        return None
    separation = max(near - other_far, other_near - far)
    if separation < -(min(abs(first["t"]), abs(second["t"])) - SHARED_TOLERANCE):
        return None  # one body: the walls share their volume, as a seam's two grids do
    gap = max(separation, 0.0)
    if gap < TOUCH_TOLERANCE:
        return None
    if gap > MAX_GAP:
        return None
    return gap, (start, end), (low, high)


def main():
    path = Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT
    blueprint = json.loads(path.read_text())
    main_grid = {"id": 0, "origin": {"x": 0, "y": 0, "z": 0}, "yaw_degrees": 0.0}
    walls = []
    for grid, grid_walls in [(main_grid, blueprint["walls"])] + [(g, g.get("walls", [])) for g in blueprint["grids"]]:
        walls += [wall_of(grid, i, w) for i, w in enumerate(grid_walls) if min(w["length_a"], w["length_b"]) >= MIN_WALL_HEIGHT]
    found = 0
    for i, first in enumerate(walls):
        for second in walls[i + 1:]:
            result = check(first, second)
            if result is None:
                continue
            gap, span, decks = result
            found += 1
            where = (first["a"][0] + frame(first)[0][0] * (span[0] + span[1]) / 2,
                     first["a"][1] + frame(first)[0][1] * (span[0] + span[1]) / 2)
            print(f"grid {first['grid']} wall {first['index']} and grid {second['grid']} wall {second['index']}: "
                  f"gap {gap:.1f} cells over {span[1] - span[0]:.0f} cells near ({where[0]:.0f}, {where[1]:.0f}), "
                  f"z {decks[0]:.0f}-{decks[1]:.0f}")
    print(f"{found} double wall(s)")
    return 1 if found else 0


if __name__ == "__main__":
    sys.exit(main())
