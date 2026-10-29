#!/usr/bin/env python3
"""Turns the Blender station map (blender/export/station_map.json) into the Octant blueprint.

The blocks carry the physics and the metadata (floors, walls, windows, doors, ceilings, lamps,
spawn points); the map's decor stays a render-only layer. One metre of the map is 4 grid cells:
a floor tile is one cell thick, a wall slab one cell inside its owner's metre cell, and the walls
stand on the floor slab, so a map height z is the cell 1 + 4z. A room's ceiling slab sits on its
walls. Differences from the map: a door is the palette's 6 x 10 cell Door in the middle of the
map's door (the cell door, 4 cells wide, takes a cell from each neighbour), a glass slope is a
vertical window up to the ceiling, only ceiling-mounted lights become lamps.
"""

import json
import sys
from collections import Counter, defaultdict
from pathlib import Path

from station_blueprint import (
    CELLS_PER_M, DOOR_BLOCK, DOOR_HEIGHT_CELLS, DOOR_WIDTH_CELLS, FLOOR, LAMP, LAMP_CELLS, MAX_SIZE_CELLS,
    SLAB_CELLS, SPAWN_CELLS, SPAWN_POINT, TO_POS_Y, WALL_BLOCK, WINDOW_BLOCK, block, block_kinds,
    write_blueprint)

ROOT = Path(__file__).parent.parent.parent
MAP_FILE = ROOT / "blender" / "export" / "station_map.json"
BLUEPRINT_FILE = ROOT / "assets" / "station" / "blueprints" / "octant.json"
DECOR_FILE = ROOT / "assets" / "station" / "decor" / "octant.json"
STATS_FILE = Path(__file__).parent / "octant" / "stats.txt"

WALL_THICKNESS_M = 0.25
# Palette primitives with a texture per map template.
WALL_STYLES = {"wall_bulkhead": "WallBulkhead", "wall_maint": "WallMaint", "wall_corrugated": "WallCorrugated",
               "wall_plate_riveted": "WallPlateRiveted", "wall_seamed": "WallSeamed", "wall_hazard": "WallHazard",
               "wall_lintel": "WallBulkhead", "wall_door": "WallBulkhead", "wall_maint_door": "WallMaint",
               "wall_cell_door": "WallBulkhead"}
FLOOR_STYLES = {f"floor_deck_{name}": f"Floor{name.capitalize()}" for name in (
    "hall", "command", "engineering", "cargo", "medical", "quarters", "science", "security", "service", "maint")}
CEILING = "Ceiling"
# Map templates drawn as models (assets/models/props/<name>.glb), keyed by the template; the rest of the map's decor waits for its model.
PROP_MODELS = {"decor_crew_chair": "crew_chair", "decor_locker": "locker", "decor_table": "table"}
# The fixture hung where a Lamp block is, by the map light's template; the block itself is not drawn.
LAMP_MODELS = {"decor_lamp_beacon": "lamp_beacon"}
DEFAULT_LAMP_MODEL = "lamp_tube"
LAMP_PRIMITIVE = LAMP
HANGING_PITCH = 1.5708  # the map's lamps hang on a wall; turned a quarter about X they hang from a ceiling
PANEL_M, DOOR_M, CELL_DOOR_M = 1.0, 2.0, 1.0
DOOR_TEMPLATES = ("wall_door", "wall_maint_door", "wall_cell_door")
LINTEL, GLASS_SLOPE = "wall_lintel", "wall_glass_slope"
WINDOW_TEMPLATES = ("wall_window", GLASS_SLOPE)
GLASS_SLOPE_FIRST_ROW = 1  # the map's slope leans back to the ceiling; a vertical window must reach it to seal
PIECE_HEIGHT_M = {"wall_door": 2.5, "wall_maint_door": 2.5, "wall_cell_door": 3.0, LINTEL: 0.5}
LAMP_REACH_CELLS = 6  # how far a lamp moves off its map position to clear a wall
LAMP_OFFSETS = sorted(((dx, dy) for dx in range(-LAMP_REACH_CELLS, LAMP_REACH_CELLS + 1)
                       for dy in range(-LAMP_REACH_CELLS, LAMP_REACH_CELLS + 1)), key=lambda d: (d[0] ** 2 + d[1] ** 2, d))
LAMP_SPACING_M = 6  # lamps the map lacks in a room: one per this many metres, as in the other stations
ABANDONED_PREFIX = "old_"  # the abandoned wing: the map's lamps there are broken on purpose, so it stays dark
LAMP_BELOW_CEILING_M = 0.7  # a light this far under its room's ceiling or closer is ceiling-mounted
FLOOR_Z = 0
WALL_Z = FLOOR_Z + SLAB_CELLS
DOOR_RISE_CELLS = DOOR_HEIGHT_CELLS


def cells(value_m):
    return round(value_m * CELLS_PER_M)


def piece_box(instance):
    """(axis the wall runs along, thickness line cell, along range, z range) of a wall piece, in map cells."""
    template = instance["template"]
    x, y, z = instance["at"]
    width = DOOR_M if template in ("wall_door", "wall_maint_door", LINTEL) else \
        CELL_DOOR_M if template == "wall_cell_door" else PANEL_M
    turns = round(instance["yaw"] / 1.5707963) % 4
    t = WALL_THICKNESS_M
    x0, x1, y0, y1 = {0: (x, x + width, y, y + t), 2: (x - width, x, y - t, y),
                      1: (x - t, x, y, y + width), 3: (x, x + t, y - width, y)}[turns]
    height = PIECE_HEIGHT_M.get(template, PANEL_M)
    axis = 0 if turns in (0, 2) else 1
    line = cells(y0 if axis == 0 else x0)
    along = (cells(x0), cells(x1)) if axis == 0 else (cells(y0), cells(y1))
    return axis, line, along, (WALL_Z + cells(z), WALL_Z + cells(z + height))


def merge(filled):
    """Covers the (u, v) cells of `filled` with rectangles (u0, v0, u1, v1), exclusive ends: u runs merged over v."""
    by_v = defaultdict(list)
    for u, v in filled:
        by_v[v].append(u)
    open_runs, found = {}, []
    for v in range(min(by_v, default=0), max(by_v, default=-1) + 2):
        runs = set()
        us = sorted(by_v.get(v, ()))
        start = prev = None
        for u in us:
            if start is None:
                start = prev = u
            elif u == prev + 1:
                prev = u
            else:
                runs.add((start, prev + 1))
                start = prev = u
        if start is not None:
            runs.add((start, prev + 1))
        for run in [r for r in open_runs if r not in runs]:
            found.append((run[0], open_runs.pop(run), run[1], v))
        for run in runs:
            open_runs.setdefault(run, v)
    return found


def split(low, high, limit=MAX_SIZE_CELLS):
    return [(start, min(limit, high - start)) for start in range(low, high, limit)]


def slab_blocks(primitive, filled, z, step=1):
    """One-cell slabs of `primitive` over the (x, y) cells (in units of `step` cells), cut to the size limit."""
    blocks = []
    for x0, y0, x1, y1 in merge(filled):
        for px, sx in split(x0 * step, x1 * step):
            for py, sy in split(y0 * step, y1 * step):
                blocks.append(block(primitive, (px, py, z), (sx, sy, SLAB_CELLS)))
    return blocks


def wall_blocks_of(axis, line, kind_cells, primitive):
    blocks = []
    for a0, z0, a1, z1 in merge(kind_cells):
        for start, length in split(a0, a1):
            cell = (start, line, z0) if axis == 0 else (line, start, z0)
            blocks.append(block(primitive, cell, (length, 1, z1 - z0), None if axis == 0 else TO_POS_Y))
    return blocks


class Octant:
    def __init__(self, data):
        self.data = data
        self.rooms = {room["name"]: room for room in data["rooms"]}
        floors = [i["at"] for i in data["instances"] if i["template"].startswith("floor_")]
        self.origin = (-round(min(f[0] for f in floors)) * CELLS_PER_M, -round(min(f[1] for f in floors)) * CELLS_PER_M)
        self.floor_tiles = {(int(x), int(y)) for x, y, _ in floors}
        self.floor_style = {(int(i["at"][0]), int(i["at"][1])): FLOOR_STYLES[i["template"]]
                            for i in data["instances"] if i["template"].startswith("floor_")}
        self.tile_height = {(x, y): self.room_height_cells(room) for room in data["rooms"]
                            for x in range(room["box"][0], room["box"][2])
                            for y in range(room["box"][1], room["box"][3]) if (x, y) in self.floor_tiles}
        self.lines = defaultdict(lambda: defaultdict(set))  # (axis, line) -> kind -> {(along, z)}
        self.solid = set()
        self.doors = []
        self.holes = set()  # the cells of the doors; a partition ending in a door loses its last cell
        self.skipped = Counter()

    def shift(self, axis, line, along):
        ox, oy = self.origin
        return (line + (oy if axis == 0 else ox), (along[0] + (ox if axis == 0 else oy),
                                                   along[1] + (ox if axis == 0 else oy)))

    def sort_pieces(self):
        for instance in self.data["instances"]:
            template = instance["template"]
            if not template.startswith("wall_"):
                continue
            axis, line, along, (z0, z1) = piece_box(instance)
            if template == GLASS_SLOPE:
                tile = (along[0] // CELLS_PER_M, line // CELLS_PER_M) if axis == 0 else                     (line // CELLS_PER_M, along[0] // CELLS_PER_M)
                z0, z1 = WALL_Z + CELLS_PER_M * GLASS_SLOPE_FIRST_ROW, WALL_Z + self.tile_height[tile]
            line, along = self.shift(axis, line, along)
            kind = WINDOW_BLOCK if template in WINDOW_TEMPLATES else WALL_STYLES.get(template, WALL_BLOCK)
            filled = {(a, z) for a in range(*along) for z in range(z0, z1)}
            self.lines[(axis, line)][kind].update(filled)
            if template in DOOR_TEMPLATES:
                middle = (along[0] + along[1]) // 2
                start = middle - DOOR_WIDTH_CELLS // 2
                self.doors.append((axis, line, start))
            self.solid.update(self.cells_of(axis, line, filled))

    @staticmethod
    def cells_of(axis, line, filled):
        return {((a, line, z) if axis == 0 else (line, a, z)) for a, z in filled}

    def carve_doors(self):
        for axis, line, start in self.doors:
            hole = {(a, z) for a in range(start, start + DOOR_WIDTH_CELLS)
                    for z in range(WALL_Z, WALL_Z + DOOR_RISE_CELLS)}
            self.holes |= self.cells_of(axis, line, hole)
        self.solid -= self.holes

    def blocks(self):
        ox, oy = self.origin
        blocks = []
        for style in sorted(set(self.floor_style.values())):
            tiles = {(x + ox // CELLS_PER_M, y + oy // CELLS_PER_M) for (x, y), kind in self.floor_style.items()
                     if kind == style}
            blocks += slab_blocks(style, tiles, FLOOR_Z, CELLS_PER_M)
        ceilings, self.ceiling_at = self.ceiling_slabs()
        blocks += ceilings
        self.sort_pieces()
        self.carve_doors()
        claimed = set(self.holes)  # a corner cell is filled by both of its walls; the first line keeps it
        for (axis, line), kinds in sorted(self.lines.items()):
            for primitive in sorted(kinds, key=lambda kind: (kind != WINDOW_BLOCK, kind)):
                filled = {(a, z) for a, z in kinds[primitive]
                          if ((a, line, z) if axis == 0 else (line, a, z)) not in claimed}
                claimed.update(self.cells_of(axis, line, filled))
                blocks += wall_blocks_of(axis, line, filled, primitive)
        for axis, line, start in sorted(self.doors):
            cell = (start, line, WALL_Z) if axis == 0 else (line, start, WALL_Z)
            blocks.append(block(DOOR_BLOCK, cell, (DOOR_WIDTH_CELLS, 1, DOOR_HEIGHT_CELLS),
                                None if axis == 0 else TO_POS_Y))
        blocks += self.lamps()
        blocks += self.spawns()
        return blocks

    def room_height_cells(self, room):
        return CELLS_PER_M * room["height"]

    def ceiling_slabs(self):
        """A slab on the walls of every room's metre cells; `ceiling_at` maps a grid cell to its slab's z and room."""
        ox, oy = self.origin
        by_height = defaultdict(set)
        ceiling_at = {}
        for room in self.data["rooms"]:
            x0, y0, x1, y1 = room["box"]
            z = WALL_Z + self.room_height_cells(room)
            for x in range(x0, x1):
                for y in range(y0, y1):
                    if (x, y) in self.floor_tiles:
                        by_height[z].add((x + ox // CELLS_PER_M, y + oy // CELLS_PER_M))
                        for cx in range(CELLS_PER_M):
                            for cy in range(CELLS_PER_M):
                                ceiling_at[((x * CELLS_PER_M + ox) + cx, (y * CELLS_PER_M + oy) + cy)] = (z, room["name"])
        blocks = []
        for z, filled in sorted(by_height.items()):
            blocks += slab_blocks(CEILING, filled, z, CELLS_PER_M)
        return blocks, ceiling_at

    def lamps(self):
        self.lamp_cells = set()
        self.lamp_props = []
        blocks = []
        lit = Counter()
        for light in self.data["lights"]:
            room = self.rooms[light["room"]]
            if light["at"][2] < room["height"] - LAMP_BELOW_CEILING_M:
                self.skipped["light not on the ceiling"] += 1
                continue
            ox, oy = self.origin
            lamp = self.hang_lamp(room, cells(light["at"][0]) + ox, cells(light["at"][1]) + oy)
            if lamp:
                self.add_lamp_prop(lamp, LAMP_MODELS.get(light["lamp"].split(".")[0], DEFAULT_LAMP_MODEL), light["yaw"])
                blocks.append(lamp)
                lit[room["name"]] += 1
        for room in self.data["rooms"]:
            if lit[room["name"]] == 0 and not room["name"].startswith(ABANDONED_PREFIX):
                blocks += self.fill_room_lamps(room)
        return blocks

    def hang_lamp(self, room, cx, cy):
        """A lamp under the room's ceiling near the cell (cx, cy), or None when no spot within reach is free."""
        top = WALL_Z + self.room_height_cells(room) - 1
        for dx, dy in LAMP_OFFSETS:
            footprint = [(cx + dx - 1 + i, cy + dy - 1 + j) for i in range(LAMP_CELLS) for j in range(LAMP_CELLS)]
            if (all(self.ceiling_at.get(c) == (top + 1, room["name"]) for c in footprint)
                    and not any((x, y, top) in self.solid for x, y in footprint)
                    and not self.lamp_cells.intersection((x, y, top) for x, y in footprint)):
                self.lamp_cells.update((x, y, top) for x, y in footprint)
                return block(LAMP, (*footprint[0], top), (LAMP_CELLS, LAMP_CELLS, 1))
        self.skipped["lamp found no free spot"] += 1
        return None

    def add_lamp_prop(self, lamp, model, yaw):
        """The fixture under the ceiling over a Lamp block: its middle, flush with the slab above."""
        cell, size = lamp["cell"], lamp["size"]
        self.lamp_props.append({
            "model": model,
            "position": {"x": (cell["x"] + size["x"] / 2) / CELLS_PER_M, "y": (cell["y"] + size["y"] / 2) / CELLS_PER_M,
                         "z": (cell["z"] + 1) / CELLS_PER_M},
            "yaw": round(yaw, 4), "pitch": HANGING_PITCH})

    def fill_room_lamps(self, room):
        """A lattice of LAMP_SPACING_M over a room the map lights with no lamp of its own."""
        ox, oy = self.origin
        x0, y0, x1, y1 = room["box"]
        columns = max(1, round((x1 - x0) / LAMP_SPACING_M))
        rows = max(1, round((y1 - y0) / LAMP_SPACING_M))
        lamps = []
        for j in range(rows):
            for i in range(columns):
                x = x0 + (x1 - x0) * (2 * i + 1) / (2 * columns)
                y = y0 + (y1 - y0) * (2 * j + 1) / (2 * rows)
                lamp = self.hang_lamp(room, cells(x) + ox, cells(y) + oy)
                if lamp:
                    self.add_lamp_prop(lamp, DEFAULT_LAMP_MODEL, 0.0)
                    lamps.append(lamp)
        if not lamps:
            self.skipped["room left dark"] += 1
        return lamps

    def spawns(self):
        ox, oy = self.origin
        blocks = []
        for spawn in self.data["spawn_points"]:
            low = (cells(spawn["at"][0]) + ox - SPAWN_CELLS // 2, cells(spawn["at"][1]) + oy - SPAWN_CELLS // 2, WALL_Z)
            footprint = [(low[0] + i, low[1] + j, WALL_Z) for i in range(SPAWN_CELLS) for j in range(SPAWN_CELLS)]
            if any(c in self.solid for c in footprint):
                self.skipped["spawn point inside a wall"] += 1
                continue
            blocks.append(block(SPAWN_POINT, low, (SPAWN_CELLS, SPAWN_CELLS, 1)))
        return blocks


def decor_props(station):
    """The map's props of PROP_MODELS, in meters over the station's floor slab (the map's floor top is z = 0)."""
    ox, oy = (value / CELLS_PER_M for value in station.origin)
    floor_top = WALL_Z / CELLS_PER_M
    return [{"model": PROP_MODELS[i["template"]],
             "position": {"x": round(i["at"][0] + ox, 3), "y": round(i["at"][1] + oy, 3),
                          "z": round(i["at"][2] + floor_top, 3)},
             "yaw": round(i["yaw"], 4)}
            for i in station.data["instances"] if i["template"] in PROP_MODELS] + station.lamp_props


def write_decor(path, props, replaces):
    """One prop per line, in FlatBuffers JSON (src/lib_z13/primitives/fbs/decor.fbs)."""
    lines = [json.dumps(entry, separators=(", ", ": ")) for entry in props]
    path.parent.mkdir(parents=True, exist_ok=True)
    body = ",\n    ".join(lines)
    hidden = json.dumps(replaces)
    path.write_text(f'{{\n  "props": [\n    {body}\n  ],\n  "replaces": {hidden}\n}}\n', encoding="ascii")


def main():
    station = Octant(json.loads(MAP_FILE.read_text(encoding="utf-8")))
    blocks = station.blocks()
    blocks = write_blueprint(BLUEPRINT_FILE, blocks)
    props = decor_props(station)
    write_decor(DECOR_FILE, props, [LAMP_PRIMITIVE])
    lines = [f"Octant: {len(blocks)} blocks ({block_kinds(blocks)})", f"origin of the map in cells: {station.origin}", f"props: {len(props)} ({', '.join(f'{k} {v}' for k, v in Counter(p['model'] for p in props).items())})",
             *(f"skipped, {reason}: {count}" for reason, count in sorted(station.skipped.items()))]
    STATS_FILE.parent.mkdir(exist_ok=True)
    STATS_FILE.write_text("\n".join(lines) + "\n", encoding="ascii")
    print("\n".join(lines))


if __name__ == "__main__":
    sys.exit(main())
