"""The station graph pipeline of the editor (docs/station-editor-plan.md): a graph of rooms and openings, with
properties checked against assets/station/graph/properties.json, to the blocks and walls of a blueprint.

Prototype stage (`f/station-editor-prototype`): rooms are boxes in cells that include their own wall ring, and two
rooms that meet share one wall cell line (their boxes overlap by exactly one cell). Each room draws its own closed loop
of four walls along the inner faces, thickness outward, sharing the corner nodes (.claude/skills/station-geometry);
one room draws a shared wall (the other leaves its side out) and an edge cuts its opening there. The joints where
a room's wall meets a neighbour's mid-wall are not mitred (the generators' joint checker is not run on this output).
Usage: python station_graph.py graph.json blueprint.json
"""

import json
import pathlib
import sys

from station_blueprint import (
    DOOR_HEIGHT_CELLS, DOOR_WIDTH_CELLS, FLOOR, LAMP, LAMP_CELLS, SPAWN_CELLS, SPAWN_POINT, block, rectangles,
    wall_opening, wall_segment, write_blueprint,
)

GRAPH_DIR = pathlib.Path(__file__).resolve().parent.parent.parent / "assets" / "station" / "graph"
PROPERTIES_PATH = GRAPH_DIR / "properties.json"
GRAPH_VERSION = 1
MIN_ROOM_CELLS = 5  # two walls, and a free cell between them to stand in
SLAB_CELLS = 1
OPENING_MARGIN_CELLS = 1  # a wall keeps a step at each end of an opening (wall_opening)
WINDOW_SILL_CELLS, WINDOW_HEIGHT_CELLS = 4, 5
# The sides of a room: the axis its wall runs along, which of the room's inner lines it stands on, and which way its
# body goes (the sign of the wall's thickness along the other axis).
SIDES = {"south": ("x", "low", -1), "north": ("x", "high", 1), "west": ("y", "low", -1), "east": ("y", "high", 1)}


class GraphError(Exception):
    """The graph cannot be built; `errors` lists every reason found, one line each."""

    def __init__(self, errors):
        super().__init__("; ".join(errors))
        self.errors = list(errors)


def load_properties(path=PROPERTIES_PATH):
    return json.loads(pathlib.Path(path).read_text(encoding="utf-8"))


def _check_value(owner, name, spec, value, errors):
    kind = spec["type"]
    if kind == "bool" and not isinstance(value, bool):
        errors.append(f"{owner}: '{name}' must be true or false")
    elif kind == "int":
        if isinstance(value, bool) or not isinstance(value, int):
            errors.append(f"{owner}: '{name}' must be a whole number")
        elif not spec["min"] <= value <= spec["max"]:
            errors.append(f"{owner}: '{name}' is {value}, allowed {spec['min']}..{spec['max']}")
    elif kind == "enum" and value not in spec["values"]:
        errors.append(f"{owner}: '{name}' is {value!r}, allowed {', '.join(spec['values'])}")


def resolved(schema_part, given, owner, errors):
    """The properties of one node or edge: its own over the defaults; unknown names and bad values are errors."""
    values = {name: spec["default"] for name, spec in schema_part.items()}
    for name, value in given.items():
        if name not in schema_part:
            errors.append(f"{owner}: unknown property '{name}'")
            continue
        _check_value(owner, name, schema_part[name], value, errors)
        values[name] = value
    return values


def _overlap(a, b):
    low, high = max(a[0], b[0]), min(a[1], b[1])
    return (low, high) if low < high else None


class Room:
    def __init__(self, node, values):
        box = node["box"]
        self.id = node["id"]
        self.x, self.y, self.z = box["x"], box["y"], box["z"]
        self.w, self.d = box["w"], box["d"]
        self.props = values

    @property
    def xs(self):
        return self.x, self.x + self.w

    @property
    def ys(self):
        return self.y, self.y + self.d

    def inner(self, axis):
        """The lines of the room's inner faces along `axis`: (low, high)."""
        low, size = (self.x, self.w) if axis == "x" else (self.y, self.d)
        return low + 1, low + size - 1


def parse(graph, schema=None):
    """(rooms by id, edges), the properties resolved; raises GraphError with every problem found."""
    schema = schema or load_properties()
    errors = []
    if graph.get("version") != GRAPH_VERSION:
        raise GraphError([f"graph version must be {GRAPH_VERSION}"])
    rooms = {}
    for node in graph.get("nodes", []):
        owner = f"node '{node.get('id')}'"
        if node.get("id") in rooms:
            errors.append(f"{owner}: duplicate id")
            continue
        box = node.get("box", {})
        if not all(isinstance(box.get(key), int) for key in ("x", "y", "z", "w", "d")):
            errors.append(f"{owner}: box needs whole x, y, z, w, d")
            continue
        if box["w"] < MIN_ROOM_CELLS or box["d"] < MIN_ROOM_CELLS:
            errors.append(f"{owner}: a room is at least {MIN_ROOM_CELLS} cells across")
            continue
        rooms[node["id"]] = Room(node, resolved(schema["node"], node.get("properties", {}), owner, errors))
    edges = []
    for edge in graph.get("edges", []):
        owner = f"edge '{edge.get('id')}'"
        if edge.get("from") not in rooms or edge.get("to") not in rooms:
            errors.append(f"{owner}: both ends must be nodes")
            continue
        edges.append({"id": edge["id"], "a": rooms[edge["from"]], "b": rooms[edge["to"]],
                      "props": resolved(schema["edge"], edge.get("properties", {}), owner, errors)})
    if errors:
        raise GraphError(errors)
    return rooms, edges


def _check_rooms(rooms):
    errors = []
    ordered = list(rooms.values())
    for i, a in enumerate(ordered):
        for b in ordered[i + 1:]:
            if a.z != b.z:
                continue
            across_x, across_y = _overlap(a.xs, b.xs), _overlap(a.ys, b.ys)
            if across_x and across_y and (across_x[1] - across_x[0] > 1 and across_y[1] - across_y[0] > 1):
                errors.append(f"rooms '{a.id}' and '{b.id}' overlap by more than a shared wall")
    return errors


def shared_wall(a, b):
    """(side of `a`, side of `b`, (low, high) of the stretch along the wall both rooms' inner faces share), or None."""
    across_x, across_y = _overlap(a.xs, b.xs), _overlap(a.ys, b.ys)
    if across_x and across_x[1] - across_x[0] == 1 and across_y:
        stretch = _overlap(a.inner("y"), b.inner("y"))
        if stretch:
            return ("east", "west", stretch) if a.x < b.x else ("west", "east", stretch)
    if across_y and across_y[1] - across_y[0] == 1 and across_x:
        stretch = _overlap(a.inner("x"), b.inner("x"))
        if stretch:
            return ("north", "south", stretch) if a.y < b.y else ("south", "north", stretch)
    return None


def _wall_of(room, side):
    """(node_a, node_b, thickness) of one side of the room's loop, from the low to the high end."""
    axis, which, sign = SIDES[side]
    other = "y" if axis == "x" else "x"
    line = room.inner(other)[0 if which == "low" else 1]
    low, high = room.inner(axis)
    z = room.z + SLAB_CELLS
    if axis == "x":
        return (low, line, z), (high, line, z), sign
    return (line, low, z), (line, high, z), sign


def _opening_for(edge, stretch):
    """(start along the wall, width, sill, height, kind) of the edge's opening, or an error text."""
    kind, width = edge["props"]["kind"], edge["props"]["width"]
    if kind == "Door":
        width = DOOR_WIDTH_CELLS
    low, high = stretch
    if width + 2 * OPENING_MARGIN_CELLS > high - low:
        return f"edge '{edge['id']}': an opening {width} wide does not fit the {high - low}-cell shared wall"
    start = low + (high - low - width) // 2
    sill, height = (WINDOW_SILL_CELLS, WINDOW_HEIGHT_CELLS) if kind == "Window" else (0, DOOR_HEIGHT_CELLS)
    return start, width, sill, height, kind


def _contains(outer, inner):
    return outer[0] <= inner[0] and inner[1] <= outer[1]


def _shared_pairs(rooms):
    """The rooms that share a wall: {frozenset of ids: (drawer, drawer side, skipper, skipper side, stretch)} and the
    errors. One room draws a shared wall and the other leaves its side out: the one whose wall is longer, or the taller
    of two equal ones (the lower one, if they are as tall)."""
    pairs, errors = {}, []
    ordered = list(rooms.values())
    for i, a in enumerate(ordered):
        for b in ordered[i + 1:]:
            found = shared_wall(a, b) if a.z == b.z else None
            if found is None:
                continue
            side_a, side_b, stretch = found
            axis = "y" if side_a in ("east", "west") else "x"
            range_a, range_b = a.inner(axis), b.inner(axis)
            if range_a == range_b:
                a_draws = a.props["height"] >= b.props["height"]
            elif _contains(range_a, range_b):
                a_draws = True
            elif _contains(range_b, range_a):
                a_draws = False
            else:
                errors.append(f"rooms '{a.id}' and '{b.id}' share a wall that neither's side holds whole")
                continue
            drawer, drawer_side, skipper, skipper_side = (a, side_a, b, side_b) if a_draws else (b, side_b, a, side_a)
            pairs[frozenset((a.id, b.id))] = (drawer, drawer_side, skipper, skipper_side, stretch)
    return pairs, errors


def _wall_cells(rooms, side_heights):
    """{(x, y): [(lowest, highest) z of a wall over the cell]}: where a neighbour's taller wall stands."""
    cells = {}
    for room in rooms:
        low, high = room.z + SLAB_CELLS, room.z + room.props["height"]
        for x in range(*room.xs):
            for y in range(*room.ys):
                if x in (room.x, room.x + room.w - 1) or y in (room.y, room.y + room.d - 1):
                    cells.setdefault((x, y), []).append((low, high))
    for (room, side), height in side_heights.items():
        node_a, node_b, thickness = _wall_of(room, side)
        axis = SIDES[side][0]
        along = 0 if axis == "x" else 1
        across = 1 - along
        body = node_a[across] if thickness > 0 else node_a[across] - 1
        for position in range(node_a[along], node_b[along]):
            cell = (position, body) if axis == "x" else (body, position)
            cells.setdefault(cell, []).append((room.z + SLAB_CELLS, room.z + height))
    return cells


def _slabs(cells, z):
    """Floor slabs at `z` over the (x, y) cells in disjoint rectangles: rooms that meet share their wall line, and a
    block per room would take its cells twice."""
    if not cells:
        return []
    low_x, low_y = min(x for x, _ in cells), min(y for _, y in cells)
    high_x, high_y = max(x for x, _ in cells), max(y for _, y in cells)
    mask = [[(x, y) in cells for x in range(low_x, high_x + 1)] for y in range(low_y, high_y + 1)]
    return [block(FLOOR, (low_x + x0, low_y + y0, z), (x1 - x0 + 1, y1 - y0 + 1, SLAB_CELLS))
            for x0, y0, x1, y1 in rectangles(mask)]


def build(graph, schema=None):
    """(blocks, walls) of the blueprint for the graph; raises GraphError."""
    rooms, edges = parse(graph, schema)
    errors = _check_rooms(rooms)
    pairs, pair_errors = _shared_pairs(rooms)
    errors += pair_errors
    # A drawn wall is as tall as the tallest room it stands for; a side a neighbour draws is left out.
    side_heights = {(room.id, side): room.props["height"] for room in rooms.values() for side in SIDES}
    skipped = set()
    for drawer, drawer_side, skipper, skipper_side, _ in pairs.values():
        skipped.add((skipper.id, skipper_side))
        key = (drawer.id, drawer_side)
        side_heights[key] = max(side_heights[key], skipper.props["height"])
    openings = {}  # (room id, side) -> [wall_opening]
    for edge in edges:
        pair = pairs.get(frozenset((edge["a"].id, edge["b"].id)))
        if pair is None:
            if not any("share a wall" in line and edge["a"].id in line and edge["b"].id in line for line in errors):
                errors.append(f"edge '{edge['id']}': rooms '{edge['a'].id}' and '{edge['b'].id}' share no wall")
            continue
        drawer, drawer_side, _, _, stretch = pair
        opening = _opening_for(edge, stretch)
        if isinstance(opening, str):
            errors.append(opening)
            continue
        start, width, sill, height, kind = opening
        node_a, _, _ = _wall_of(drawer, drawer_side)
        offset = start - node_a[0 if SIDES[drawer_side][0] == "x" else 1]
        if offset < OPENING_MARGIN_CELLS or sill + height + OPENING_MARGIN_CELLS > side_heights[(drawer.id, drawer_side)]:
            errors.append(f"edge '{edge['id']}': the opening does not fit the wall of room '{drawer.id}'")
            continue
        openings.setdefault((drawer.id, drawer_side), []).append(wall_opening(offset, width, height, kind, sill))
    if errors:
        raise GraphError(errors)

    blocks, walls = [], []
    floors, ceilings = {}, {}
    for room in rooms.values():
        height = room.props["height"]
        footprint = {(x, y) for x in range(*room.xs) for y in range(*room.ys)}
        floors.setdefault(room.z, set()).update(footprint)
        if room.props["ceiling"]:
            ceilings.setdefault(room.z + SLAB_CELLS + height, set()).update(footprint)
        if room.props["lamp"]:
            centre = (room.x + room.w // 2 - LAMP_CELLS // 2, room.y + room.d // 2 - LAMP_CELLS // 2)
            blocks.append(block(LAMP, (*centre, room.z + height), (LAMP_CELLS, LAMP_CELLS, SLAB_CELLS)))
        if room.props["spawn"]:
            centre = (room.x + room.w // 2 - SPAWN_CELLS // 2, room.y + room.d // 2 - SPAWN_CELLS // 2)
            blocks.append(block(SPAWN_POINT, (*centre, room.z + SLAB_CELLS), (SPAWN_CELLS, SPAWN_CELLS, SLAB_CELLS)))
        for side in SIDES:
            if (room.id, side) in skipped:
                continue
            node_a, node_b, thickness = _wall_of(room, side)
            walls.append(wall_segment(node_a, node_b, thickness, side_heights[(room.id, side)], 2,
                                      openings.get((room.id, side), ())))
    wall_cells = _wall_cells(rooms.values(), {(rooms[room_id], side): height
                                              for (room_id, side), height in side_heights.items()
                                              if (room_id, side) not in skipped})
    for z, cells in sorted(floors.items()):
        blocks.extend(_slabs(cells, z))
    for z, cells in sorted(ceilings.items()):
        # A taller wall of a neighbour already stands where this ceiling would cross it.
        blocks.extend(_slabs({c for c in cells if not any(low <= z <= top for low, top in wall_cells.get(c, ()))}, z))
    return blocks, walls


def main(argv):
    if len(argv) != 3:
        print(__doc__.strip().splitlines()[-1], file=sys.stderr)
        return 2
    try:
        graph = json.loads(pathlib.Path(argv[1]).read_text(encoding="utf-8"))
        blocks, walls = build(graph)
    except GraphError as error:
        for line in error.errors:
            print(line, file=sys.stderr)
        return 1
    written = write_blueprint(pathlib.Path(argv[2]), blocks, walls)
    print(f"{len(written)} blocks, {len(walls)} walls")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
