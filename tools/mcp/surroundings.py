"""Prints what is around the local player: where the camera is, what it looks at and what is under it, for every grid
the camera is near (the camera is taken into each grid's own cells), and the main grid's rooms, doors and conduits.

    python3 tools/mcp/surroundings.py [--radius METERS]
"""

import argparse
import math

import mcp_client

CELL_M = 0.25
DEFAULT_RADIUS_M = 6.0
BLOCK_LIMIT = 3000
RAY_RANGE_M = 20.0
NEAR_LIST = 8
MAIN_GRID = 0


def forward(camera):
    yaw, pitch = math.radians(camera["yaw_deg"]), math.radians(camera["pitch_deg"])
    return (math.cos(pitch) * math.cos(yaw), math.cos(pitch) * math.sin(yaw), -math.sin(pitch))


def rotation(grid):
    """The matrix taking a grid's meters to the world's: turn about Z, then tilt about the grid's Y (GridToWorld)."""
    yaw, tilt = math.radians(grid["yaw_degrees"]), -math.radians(grid["pitch_degrees"])
    cy, sy, ct, st = math.cos(yaw), math.sin(yaw), math.cos(tilt), math.sin(tilt)
    z_turn = [[cy, -sy, 0], [sy, cy, 0], [0, 0, 1]]
    y_tilt = [[ct, 0, st], [0, 1, 0], [-st, 0, ct]]
    return [[sum(z_turn[r][k] * y_tilt[k][c] for k in range(3)) for c in range(3)] for r in range(3)]


def to_grid(grid, point, vector=False):
    """A world point (or direction) in the grid's own meters: the transpose of its rotation."""
    rot = rotation(grid)
    origin = [0, 0, 0] if vector else [grid["origin"][a] * CELL_M for a in "xyz"]
    shifted = [point[i] - origin[i] for i in range(3)]
    return [sum(rot[r][c] * shifted[r] for r in range(3)) for c in range(3)]


def box_of(item):
    low = [item["cell"][axis] * CELL_M for axis in "xyz"]
    return low, [low[i] + item["size"][axis] * CELL_M for i, axis in enumerate("xyz")]


def ray_hit(eye, direction, low, high):
    """Distance along the ray to the box, or None (slab method)."""
    near, far = 0.0, RAY_RANGE_M
    for i in range(3):
        if abs(direction[i]) < 1e-9:
            if not low[i] <= eye[i] <= high[i]:
                return None
            continue
        a, b = (low[i] - eye[i]) / direction[i], (high[i] - eye[i]) / direction[i]
        near, far = max(near, min(a, b)), min(far, max(a, b))
        if near > far:
            return None
    return near


def distance_to(eye, item):
    low, high = box_of(item)
    return math.dist(eye, [min(max(eye[i], low[i]), high[i]) for i in range(3)])


def describe(item):
    return (f"{item['name']} {item.get('primitive', item.get('kind', ''))} cell={tuple(item['cell'].values())} "
            f"size={tuple(item['size'].values())}")


def report_grid(grid, eye_world, direction_world, radius):
    eye = to_grid(grid, eye_world)
    direction = to_grid(grid, direction_world, vector=True)
    reach = int(radius / CELL_M)
    box = {"grid": grid["id"], "x": int(eye[0] / CELL_M) - reach, "y": int(eye[1] / CELL_M) - reach,
           "z": int(eye[2] / CELL_M) - reach, "size_x": 2 * reach, "size_y": 2 * reach, "size_z": 2 * reach,
           "limit": BLOCK_LIMIT}
    blocks = mcp_client.ask("blocks", box)["result"]
    if not blocks:
        return
    print(f"\ngrid {grid['id']} (origin {tuple(grid['origin'].values())}, yaw {grid['yaw_degrees']:.0f}, "
          f"pitch {grid['pitch_degrees']:.0f}): eye at cell ({eye[0] / CELL_M:.1f}, {eye[1] / CELL_M:.1f}, "
          f"{eye[2] / CELL_M:.1f}), {len(blocks)} blocks within {radius} m")
    hits = sorted(((d, b) for b in blocks for d in [ray_hit(eye, direction, *box_of(b))] if d is not None), key=lambda hit: hit[0])
    print("  looking at:", f"{describe(hits[0][1])}, {hits[0][0]:.2f} m away" if hits else "nothing within reach")
    below = [b for b in blocks if box_of(b)[0][0] <= eye[0] <= box_of(b)[1][0]
             and box_of(b)[0][1] <= eye[1] <= box_of(b)[1][1] and box_of(b)[1][2] <= eye[2]]
    print("  under the eye:", describe(max(below, key=lambda b: box_of(b)[1][2])) if below else "no block")
    for item in sorted(blocks, key=lambda b: distance_to(eye, b))[:NEAR_LIST]:
        print("   ", f"{distance_to(eye, item):.1f} m", describe(item))


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--radius", type=float, default=DEFAULT_RADIUS_M)
    radius = parser.parse_args().radius
    camera = mcp_client.ask("render_stats")["result"]["camera"]
    eye = (camera["x"], camera["y"], camera["z"])
    print(f"camera ({eye[0]:.2f}, {eye[1]:.2f}, {eye[2]:.2f}) m; yaw {camera['yaw_deg']:.0f}, "
          f"pitch {camera['pitch_deg']:.0f} down")
    for grid in mcp_client.ask("grids")["result"]:
        report_grid(grid, eye, forward(camera), radius)
    rooms = [r for r in mcp_client.ask("rooms")["result"]
             if all(r["min"][a] * CELL_M <= eye[i] <= (r["min"][a] + r["size"][a]) * CELL_M for i, a in enumerate("xyz"))]
    print("\nmain grid rooms around the eye:",
          [(r["id"], "open to space" if r["open_to_space"] else "sealed") for r in rooms] or "none")
    doors = [d for d in mcp_client.ask("doors")["result"]
             if math.dist(eye, [d["cell"][a] * CELL_M for a in "xyz"]) < radius]
    for door in doors:
        print("  door", door["name"], tuple(door["cell"].values()), door["mode"],
              "open" if door["open_share"] else "shut", door["interlock"] or "")


if __name__ == "__main__":
    main()
