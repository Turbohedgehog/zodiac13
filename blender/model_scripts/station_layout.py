"""A full one-deck station assembled from the station_models segments, on a 1 m grid.

The layout is our own design with the usual departments (command, security, medical, science,
engineering, cargo, service, crew life). Every room builds its own walls on its cell edges, so a wall
between two rooms is double and each side shows its own face; doors are placed between named rooms.

Run in Blender: exec the module and call build_station().
Plan (metres, X east, Y north; the bow is +Y; a corridor is 4 m wide):
  y 108..132  command row: AI, heads, bridge, captain, vault, teleporter, EVA, gateway, theatre
  y  84..104  security and service row: brig, courtroom | library, chapel, bar, kitchen, dining, hydroponics
  y  66..80 and 40..62  medical (west) and science (east) on both sides of the main corridor y 62..66
  the atrium at the centre, y 16..36  dormitory block (west), cargo, gym (east)
  y   0..16   engineering at the stern; arrival and escape docks at the ends of the main corridor.
"""

import math
import pathlib
import sys
from collections import deque
from dataclasses import dataclass, field

import bpy

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import station_models as sm  # noqa: E402
import station_scenes as ss  # noqa: E402

COLLECTION = "station_full"
SLAB = sm.WALL_THICKNESS
DOOR_ROWS = 3
LAMP_SPACING = 6
LAMP_DROP = 0.6
LAMP_MAX_Z = 5.4
LAMP_DOOR_MARGIN = 1.5
DOOR_CLEARANCE = 2.6
WINDOW_PERIOD = 3
WINDOW_ROWS = (1, 2)
FACING_OF_EDGE = {"N": "S", "S": "N", "E": "W", "W": "E"}
STEP = {"N": (0, 1), "S": (0, -1), "E": (1, 0), "W": (-1, 0)}
INWARD = ss.INWARD
OPPOSITE = {"N": "S", "S": "N", "E": "W", "W": "E"}
OLD_KINDS = (
    "old_store", "old_workshop", "old_cabin", "old_canteen", "old_kitchen", "old_brig", "old_guard", "old_medbay",
    "old_office", "old_lab", "old_server", "old_lounge",
)
NO_WINDOWS = set(OLD_KINDS) | {"airlock_prep", "tunnel", "maint_room", "airlock", "engine", "vault", "server", "ai_core", "morgue", "armory", "prison", "brig", "toxins_test", "corridor"}
CORRIDOR_WIDTH = 4
MAINT_KINDS = ("tunnel", "maint_room") + OLD_KINDS
PRISON_DOOR_AT = 101


def alternate(a, b):
    return lambda i, row: a if (i + row) % 2 == 0 else b


STYLES = {
    "bridge": lambda i, row: "wall_plate_riveted" if row == 0 else "wall_seamed",
    "corridor": lambda i, row: "wall_hazard" if row == 0 else "wall_corrugated",
    "medical": lambda i, row: "wall_plate_riveted",
    "engineering": lambda i, row: "wall_hazard" if row == 0 else "wall_bulkhead",
    "cargo": lambda i, row: "wall_corrugated",
    "science": lambda i, row: "wall_seamed",
    "quarters": lambda i, row: "wall_plate_riveted",
    "public": lambda i, row: "wall_bulkhead" if row == 1 else "wall_plate_riveted",
    "security": lambda i, row: "wall_hazard" if row == 0 else "wall_seamed",
    "maint": lambda i, row: "wall_maint",
}


# the outline of the station: a bounding box with each corner cut off at 45 degrees (an octagon)
OCTAGON = (-4, 0, 164, 132, 30)
MIN_KEEP = 0.35


def inside_shape(x, y):
    """Whether grid cell (x, y) is inside the octagonal outline."""
    x0, y0, x1, y1, chamfer = OCTAGON
    if not (x0 <= x < x1 and y0 <= y < y1):
        return False
    left, bottom = x + 0.5 - x0, y + 0.5 - y0
    right, top = x1 - (x + 0.5), y1 - (y + 0.5)
    return min(left + bottom, right + bottom, left + top, right + top) >= chamfer


@dataclass
class Room:
    name: str
    x0: int
    y0: int
    x1: int
    y1: int
    kind: str
    height: int = 3
    style: str = "public"
    glass_north: bool = False
    outside: bool = False
    free: bool = False
    index: int = field(default=0, init=False)

    @property
    def corridor(self):
        return self.kind == "corridor"

    @property
    def windows(self):
        return self.kind not in NO_WINDOWS

    @property
    def cx(self):
        return (self.x0 + self.x1) / 2

    @property
    def cy(self):
        return (self.y0 + self.y1) / 2

    def cells(self):
        return [
            (x, y)
            for x in range(self.x0, self.x1)
            for y in range(self.y0, self.y1)
            if self.free or inside_shape(x, y) != self.outside
        ]


@dataclass
class Door:
    axis: str
    x: int
    y: int


class Layout:
    def __init__(self, rooms, links, exterior_doors=()):
        rooms = [
            room
            for room in rooms
            if room.cells()
            and (room.outside or len(room.cells()) >= MIN_KEEP * (room.x1 - room.x0) * (room.y1 - room.y0))
        ]
        names = {room.name for room in rooms}
        links = [link for link in links if link[0] in names and link[1] in names]
        exterior_doors = [(name, edge) for name, edge in exterior_doors if name in names]
        self.rooms = rooms
        self.by_name = {room.name: room for room in rooms}
        self.owner = {}
        for index, room in enumerate(rooms):
            room.index = index
            for cell in room.cells():
                assert cell not in self.owner, f"{room.name} overlaps {rooms[self.owner[cell]].name}"
                self.owner[cell] = index
        self.doors, self.links, self.dropped = [], [], []
        for link in links:
            a, b, *rest = link
            try:
                self.doors.append(self._resolve_door(self.by_name[a], self.by_name[b], *rest))
                self.links.append(link)
            except ValueError:
                self.dropped.append(link)
        self.door_cells = set()
        self.door_spans = {}
        self.door_hosts = []
        for door in self.doors:
            self._register_door(door)
        for name, edge in exterior_doors:
            self._register_exterior_door(self.by_name[name], edge)
        self._check_connected()

    def room_at(self, cell):
        index = self.owner.get(cell)
        return None if index is None else self.rooms[index]

    def _resolve_door(self, a, b, at=None):
        """A doorway, DOOR_WIDTH wide, on the wall the two rooms' cells really share: the longest shared stretch,
        in its middle (or at `at` when that fits)."""
        width = int(sm.DOOR_WIDTH)
        found = {}
        for x, y in a.cells():
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                if self.owner.get((x + dx, y + dy)) != b.index:
                    continue
                if dx:
                    key = ("v", x + 1 if dx == 1 else x, dx)
                    found.setdefault(key, set()).add(y)
                else:
                    key = ("h", y + 1 if dy == 1 else y, dy)
                    found.setdefault(key, set()).add(x)
        best = None
        for (axis, boundary, _), along in found.items():
            starts = sorted(s for s in along if all(s + i in along for i in range(width)))
            if not starts:
                continue
            middle = starts[len(starts) // 2]
            start = min(starts, key=lambda s: abs(s - at)) if at is not None else middle
            if best is None or len(along) > best[0]:
                best = (len(along), axis, boundary, start)
        if best is None:
            raise ValueError(f"{a.name} and {b.name} share no wall for a door")
        _, axis, boundary, start = best
        return Door("v", boundary, start) if axis == "v" else Door("h", start, boundary)

    def visible(self, x, y):
        """Whether a room owns the cell under (x, y): decor outside the station's outline is dropped."""
        return (math.floor(x), math.floor(y)) in self.owner

    def _door_sides(self, door):
        """(room, edge direction, fixed coordinate, along start) for both rooms of a door."""
        if door.axis == "h":
            low, high = self.room_at((door.x, door.y - 1)), self.room_at((door.x, door.y))
            return (low, "N", door.y - SLAB, door.x), (high, "S", door.y, door.x)
        low, high = self.room_at((door.x - 1, door.y)), self.room_at((door.x, door.y))
        return (low, "E", door.x - SLAB, door.y), (high, "W", door.x, door.y)

    def _register_door(self, door):
        sides = self._door_sides(door)
        host = sides[0] if owns_wall(sides[0][0], sides[1][0]) else sides[1]
        self.door_hosts.append((host, sides))
        for room, edge, _, start in sides:
            self.door_spans.setdefault((room.index, edge), []).append(start)
        room, edge, _, start = host
        for along in range(start, start + int(sm.DOOR_WIDTH)):
            self.door_cells.add((room.index, edge, along))

    def _register_exterior_door(self, room, edge):
        """A door in a room's outer wall that opens to space (an airlock's outer door)."""
        low, high = edge_range(room, edge)
        start = low + (high - low - int(sm.DOOR_WIDTH)) // 2
        fixed = {"N": room.y1 - SLAB, "S": room.y0, "E": room.x1 - SLAB, "W": room.x0}[edge]
        side = (room, edge, fixed, start)
        self.door_hosts.append((side, [side]))
        self.door_spans.setdefault((room.index, edge), []).append(start)
        for along in range(start, start + int(sm.DOOR_WIDTH)):
            self.door_cells.add((room.index, edge, along))

    def _check_connected(self):
        graph = {room.name: set() for room in self.rooms}
        for a, b, *_ in self.links:
            graph[a].add(b)
            graph[b].add(a)
        seen, queue = {"atrium"}, deque(["atrium"])
        while queue:
            for nxt in graph[queue.popleft()]:
                if nxt not in seen:
                    seen.add(nxt)
                    queue.append(nxt)
        missing = [room.name for room in self.rooms if room.name not in seen]
        assert not missing, f"unreachable rooms: {missing}"

    def inner_face(self, room, edge, along):
        """Where the wall face is for decor on the `edge` of a room at `along`: the room's own slab, or the back of the neighbour's."""
        step = int(along // 1)
        cell = {"N": (step, room.y1), "S": (step, room.y0 - 1), "E": (room.x1, step), "W": (room.x0 - 1, step)}[edge]
        neighbour = self.room_at(cell)
        if neighbour is None or owns_wall(room, neighbour):
            return {"N": room.y1 - SLAB, "S": room.y0, "E": room.x1 - SLAB, "W": room.x0}[edge]
        return {"N": room.y1, "S": room.y0 - SLAB, "E": room.x1, "W": room.x0 - SLAB}[edge]

    def near_door(self, room, edge, along):
        return any(
            start - LAMP_DOOR_MARGIN <= along <= start + sm.DOOR_WIDTH + LAMP_DOOR_MARGIN
            for start in self.door_spans.get((room.index, edge), [])
        )

    def door_edges(self, room):
        return [edge for edge in "NSEW" if (room.index, edge) in self.door_spans]

    def point_free(self, room, x, y):
        """False when (x, y) is in front of one of the room's doors."""
        for (index, edge), starts in self.door_spans.items():
            if index != room.index:
                continue
            for start in starts:
                centre = start + sm.DOOR_WIDTH / 2
                px, py = {
                    "N": (centre, room.y1),
                    "S": (centre, room.y0),
                    "E": (room.x1, centre),
                    "W": (room.x0, centre),
                }[edge]
                if abs(x - px) < DOOR_CLEARANCE and abs(y - py) < DOOR_CLEARANCE:
                    return False
        return True


# ----- Walls, floors, doors -----


def owns_wall(room, other):
    """Whether `room` builds the single wall it shares with `other` (None is space: the room always builds)."""
    if other is None:
        return True
    if room.height != other.height:
        return room.height > other.height
    if room.corridor != other.corridor:
        return not room.corridor
    return room.index < other.index


def edge_geometry(edge, cx, cy):
    """(facing, fixed, along) of the slab on a cell's edge, inside the cell."""
    if edge == "N":
        return "S", cy + 1 - SLAB, cx
    if edge == "S":
        return "N", cy, cx
    if edge == "E":
        return "W", cx + 1 - SLAB, cy
    return "E", cx, cy


def build_walls(s, layout):
    for room in layout.rooms:
        pick = STYLES[room.style]
        for cx, cy in room.cells():
            for edge, (dx, dy) in STEP.items():
                neighbour = layout.room_at((cx + dx, cy + dy))
                if neighbour is room or (neighbour is not None and not owns_wall(room, neighbour)):
                    continue
                facing, fixed, along = edge_geometry(edge, cx, cy)
                exterior = neighbour is None
                for row in range(room.height):
                    if (room.index, edge, along) in layout.door_cells and row < DOOR_ROWS:
                        continue
                    template = pick(along, row)
                    if exterior and room.glass_north and edge == "N":
                        if row == 1:
                            template = "wall_glass_slope"
                        elif row > 1:
                            continue
                    elif exterior and room.windows and row in WINDOW_ROWS and along % WINDOW_PERIOD:
                        template = "wall_window"
                    s.wall_piece(template, facing, fixed, along, row)


def build_doors(s, layout):
    for host, sides in layout.door_hosts:
        _, edge, fixed, start = host
        facing = FACING_OF_EDGE[edge]
        template = "wall_maint_door" if any(side[0].kind in MAINT_KINDS for side in sides) else "wall_door"
        s.wall_piece(template, facing, fixed, start, 0, length=sm.DOOR_WIDTH)
        s.wall_piece("wall_lintel", facing, fixed, start, sm.DOOR_HEIGHT, length=sm.DOOR_WIDTH)


FLOOR_OF_STYLE = {
    "bridge": "command",
    "corridor": "hall",
    "medical": "medical",
    "engineering": "engineering",
    "cargo": "cargo",
    "science": "science",
    "quarters": "quarters",
    "public": "service",
    "security": "security",
    "maint": "maint",
}


def build_floors(s, layout):
    for room in layout.rooms:
        template = f"floor_deck_{FLOOR_OF_STYLE[room.style]}"
        for x, y in room.cells():
            s._instance(template, (x, y, 0.0), clip=False)


# ----- Decor helpers -----


def wall_decor(s, layout, room, edge, template, along, z, inset=0.0):
    """Hang decor on the inner face of one of the room's walls, skipping door spans."""
    if layout.near_door(room, edge, along):
        return
    s.mount(template, FACING_OF_EDGE[edge], layout.inner_face(room, edge, along), along, z, inset=inset)


def edge_range(room, edge):
    return (room.x0, room.x1) if edge in "NS" else (room.y0, room.y1)


def lamps(s, layout, room, edges="NSEW"):
    z = min(room.height - LAMP_DROP, LAMP_MAX_Z)
    for edge in edges:
        low, high = edge_range(room, edge)
        along = low + LAMP_SPACING / 2
        while along < high - 1:
            wall_decor(s, layout, room, edge, "decor_lamp_tube", along, z)
            along += LAMP_SPACING


def accents(s, layout, room, edges, template, step, z=0.0, offset=0.0, inset=0.0):
    for edge in edges:
        low, high = edge_range(room, edge)
        along = low + step / 2 + offset
        while along < high - 1:
            wall_decor(s, layout, room, edge, template, along, z, inset=inset)
            along += step


def long_edges(room):
    return "NS" if (room.x1 - room.x0) >= (room.y1 - room.y0) else "EW"


def centres(low, high, count, margin):
    """`count` evenly spaced centres between low + margin and high - margin."""
    if count == 1:
        return [(low + high) / 2]
    span = high - low - 2 * margin
    return [low + margin + span * i / (count - 1) for i in range(count)]


def floor_grid(s, layout, room, template, nx, ny, facing="S", margin=2.0, z=0.0, margin_y=None):
    margin_y = margin if margin_y is None else margin_y
    for x in centres(room.x0, room.x1, nx, margin):
        for y in centres(room.y0, room.y1, ny, margin_y):
            if layout.point_free(room, x, y):
                s.stand(template, x, y, facing, z=z)


def station_row(s, layout, room, count, edge="N", margin=3.0):
    """Terminals with chairs along a wall, screens looking into the room."""
    low, high = edge_range(room, edge)
    for along in centres(low, high, count, margin):
        if layout.near_door(room, edge, along):
            continue
        if edge == "N":
            s.station(along, room.y1 - 1.8)
        elif edge == "S":
            s.stand("decor_terminal", along, room.y0 + TERMINAL_BACK + SLAB, "N")
            s.stand("decor_crew_chair", along, room.y0 + TERMINAL_BACK + SLAB + ss.TERMINAL_FRONT + ss.CHAIR_GAP, "S")


TERMINAL_BACK = ss.TERMINAL_BACK


def table_set(s, layout, room, nx, ny, margin=3.5):
    for x in centres(room.x0, room.x1, nx, margin):
        for y in centres(room.y0, room.y1, ny, margin):
            if layout.point_free(room, x, y):
                s.stand("decor_table", x, y)
                s.stand("decor_crew_chair", x, y - 0.95, "N")
                s.stand("decor_crew_chair", x, y + 0.95, "S")


def far_edge(layout, room):
    edges = layout.door_edges(room)
    return OPPOSITE[edges[0]] if edges else "N"


# ----- Room decor, by kind -----

DESK_HEIGHT = 0.77
DESK_MONITOR_BACK = 0.2
DESK_KEYBOARD_FRONT = 0.15
DESK_CHAIR_GAP = 0.95
AIRLOCK_LIGHT_Z = 2.3


def spaced(low, high, step, margin):
    """Centres at least `step` apart between low + margin and high - margin, evenly spread."""
    span = high - low - 2 * margin
    if span <= 0:
        return [(low + high) / 2]
    count = int(span // step) + 1
    return centres(low, high, count, margin)


def fill(s, layout, room, template, step_x, step_y, margin=2.5, facing="S", z=0.0, margin_y=None, skip=None):
    margin_y = margin if margin_y is None else margin_y
    for i, x in enumerate(spaced(room.x0, room.x1, step_x, margin)):
        for j, y in enumerate(spaced(room.y0, room.y1, step_y, margin_y)):
            if layout.point_free(room, x, y) and not (skip and skip(i, j)):
                s.stand(template, x, y, facing, z=z)


def desk(s, x, y, screen_facing="S", chair="decor_crew_chair"):
    """A table with a CRT monitor and a keyboard and a chair on the user side; the screen looks at the user."""
    ux, uy = INWARD[screen_facing]
    s.stand("decor_table", x, y, screen_facing)
    s.stand("decor_monitor_crt", x - ux * DESK_MONITOR_BACK, y - uy * DESK_MONITOR_BACK, screen_facing, z=DESK_HEIGHT)
    s.stand("decor_keyboard", x + ux * DESK_KEYBOARD_FRONT, y + uy * DESK_KEYBOARD_FRONT, screen_facing, z=DESK_HEIGHT)
    s.stand(chair, x + ux * DESK_CHAIR_GAP, y + uy * DESK_CHAIR_GAP, OPPOSITE[screen_facing])


def desk_line(s, layout, room, edge, step, margin=2.5, depth=1.0):
    """Desks against a wall, their users facing the wall."""
    low, high = edge_range(room, edge)
    ix, iy = INWARD[FACING_OF_EDGE[edge]]
    face = {"N": room.y1 - SLAB, "S": room.y0 + SLAB, "E": room.x1 - SLAB, "W": room.x0 + SLAB}[edge]
    for along in spaced(low, high, step, margin):
        if layout.near_door(room, edge, along):
            continue
        x, y = (along, face) if edge in "NS" else (face, along)
        desk(s, x + ix * depth, y + iy * depth, FACING_OF_EDGE[edge])


def dining_set(s, layout, room, step_x, step_y, margin=3.0):
    """Tables with four chairs, two along each long side."""
    for x in spaced(room.x0, room.x1, step_x, margin):
        for y in spaced(room.y0, room.y1, step_y, margin):
            if not layout.point_free(room, x, y):
                continue
            s.stand("decor_table", x, y)
            s.stand("decor_crew_chair", x - 0.45, y - DESK_CHAIR_GAP, "N")
            s.stand("decor_crew_chair", x + 0.45, y - DESK_CHAIR_GAP, "N")
            s.stand("decor_crew_chair", x - 0.45, y + DESK_CHAIR_GAP, "S")
            s.stand("decor_crew_chair", x + 0.45, y + DESK_CHAIR_GAP, "S")


def corner_planters(s, layout, room, inset=0.35):
    for edge, along in (("N", room.x0 + 1.5), ("N", room.x1 - 1.5), ("S", room.x0 + 1.5), ("S", room.x1 - 1.5)):
        wall_decor(s, layout, room, edge, "decor_planter", along, 0.0, inset=inset)


def d_corridor(s, layout, room):
    lamps(s, layout, room, long_edges(room))
    long = long_edges(room)
    accents(s, layout, room, long[:1], "decor_wall_vent_grille", 12, 1.6)
    accents(s, layout, room, long[1:], "decor_pipe_panel", 12, 1.6, offset=6)
    accents(s, layout, room, long[:1], "decor_vending", 24, 0.0, offset=9)
    accents(s, layout, room, long[1:], "decor_planter", 24, 0.0, offset=3, inset=0.35)


def d_bridge(s, layout, room):
    lamps(s, layout, room, "EWS")
    for y_offset, count, step in ((1.5, 8, 2.7), (6.5, 6, 3.2), (11.5, 4, 3.8)):
        for x in ss.spread(room.cx, count, step):
            s.station(x, room.y1 - y_offset)
    s.stand("decor_captain_chair", room.cx, room.y0 + 6, "N")
    desk_line(s, layout, room, "W", 3.2, margin=3.0)
    desk_line(s, layout, room, "E", 3.2, margin=3.0)
    accents(s, layout, room, "EW", "decor_switch_panel", 8, 1.3, offset=1.5)
    for x in (room.cx - 4, room.cx + 4):
        wall_decor(s, layout, room, "S", "decor_lamp_beacon", x, 2.3)
    wall_decor(s, layout, room, "S", "decor_monitor_crt_wall", room.cx, 1.6)


def d_atrium(s, layout, room):
    lamps(s, layout, room)
    accents(s, layout, room, "NSEW", "decor_planter", 5.5, inset=0.35)
    accents(s, layout, room, "NSEW", "decor_vending", 11, 0.0, offset=2.75)
    cx, cy, half = room.cx, room.cy, 5.0
    for dx in (-2.0, 2.0):
        s.stand("decor_planter", cx + dx, cy - half / 2, "S")
        s.stand("decor_planter", cx + dx, cy + half / 2, "N")
        s.stand("decor_bench", cx + dx, cy - half / 2 - 1.4, "S")
        s.stand("decor_bench", cx + dx, cy + half / 2 + 1.4, "N")
    for dy in (-1.1, 1.1):
        s.stand("decor_bench", cx - half - 2.4, cy + dy, "W")
        s.stand("decor_bench", cx + half + 2.4, cy + dy, "E")
    for dx in (-8.0, 8.0):
        for dy in (-8.0, 8.0):
            s.pillar(cx + dx, cy + dy, room.height)
            s.stand("decor_bench", cx + dx, cy + dy + (-1.4 if dy < 0 else 1.4), "S" if dy < 0 else "N")


def d_cabin(s, layout, room):
    far = far_edge(layout, room)
    horizontal = far in "NS"
    wall_decor(s, layout, room, far, "decor_lamp_tube", room.cx if horizontal else room.cy, 2.4)
    if horizontal:
        y = room.y1 - 1.4 if far == "N" else room.y0 + 1.4
        s.stand("decor_bed", room.x0 + 1.1, y)
        wall_decor(s, layout, room, far, "decor_locker", room.x1 - 0.9, 0.0)
        wall_decor(s, layout, room, far, "decor_locker", room.x1 - 1.6, 0.0)
        s.stand("decor_table", room.x1 - 1.0, room.cy, "E")
        s.stand("decor_crew_chair", room.x1 - 1.95, room.cy, "E")
    else:
        s.stand("decor_bed", room.x1 - 1.4 if far == "E" else room.x0 + 1.4, room.cy, "E")
        wall_decor(s, layout, room, far, "decor_locker", room.y1 - 1.0, 0.0)


def d_lounge(s, layout, room):
    lamps(s, layout, room, "EW")
    fill(s, layout, room, "decor_sofa", 4.5, 4.5, margin=3.5)
    accents(s, layout, room, "E", "decor_toilet", 3.0, 0.0)
    accents(s, layout, room, "W", "decor_shower", 3.0, 0.0, offset=0.5)
    accents(s, layout, room, "N", "decor_vending", 3.0, 0.0)
    corner_planters(s, layout, room)


def d_captain(s, layout, room):
    lamps(s, layout, room, "EW")
    s.stand("decor_bed", room.x0 + 2, room.y0 + 4)
    accents(s, layout, room, "E", "decor_locker", 2.0, 0.0)
    desk_line(s, layout, room, "N", 5.0, margin=3.0)
    s.stand("decor_table", room.cx + 1, room.cy)
    fill(s, layout, room, "decor_sofa", 6.0, 6.0, margin=3.5, skip=lambda i, j: j != 0)
    accents(s, layout, room, "W", "decor_shelf", 2.2, 0.0, offset=3.0)
    corner_planters(s, layout, room)


def d_hop(s, layout, room):
    lamps(s, layout, room)
    accents(s, layout, room, "S", "decor_counter", 2.3, 0.0, inset=0.3)
    desk_line(s, layout, room, "N", 3.3, margin=2.5)
    fill(s, layout, room, "decor_sofa", 4.0, 4.0, margin=3.5, skip=lambda i, j: j == 0)
    accents(s, layout, room, "E", "decor_shelf", 2.2, 0.0, offset=3.0)
    accents(s, layout, room, "W", "decor_locker", 2.0, 0.0, offset=4.0)


def d_heads(s, layout, room):
    lamps(s, layout, room)
    for y in spaced(room.y0, room.y1, 5.0, 3.0):
        s.stand("decor_bed", room.x0 + 1.4, y, "E")
    accents(s, layout, room, "W", "decor_locker", 2.2, 0.0)
    desk_line(s, layout, room, "E", 5.0, margin=3.0)
    dining_set(s, layout, room, 5.0, 5.0, margin=5.0)


def d_meeting(s, layout, room):
    lamps(s, layout, room)
    dining_set(s, layout, room, 5.0, 4.5, margin=3.5)
    wall_decor(s, layout, room, "N", "decor_monitor_crt_wall", room.cx - 3, 1.6)
    wall_decor(s, layout, room, "N", "decor_monitor_crt_wall", room.cx + 3, 1.6)
    corner_planters(s, layout, room)


def d_server(s, layout, room):
    lamps(s, layout, room, "NS")
    fill(s, layout, room, "decor_mainframe", 3.2, 3.2, margin=2.5)
    desk_line(s, layout, room, "S", 4.0, margin=2.0)


def d_ai_core(s, layout, room):
    lamps(s, layout, room, "NS")
    fill(s, layout, room, "decor_mainframe", 3.2, 3.2, margin=2.5, skip=lambda i, j: i == 1 and j == 3)
    s.stand("decor_pad", room.cx, room.cy)
    desk_line(s, layout, room, "S", 4.0, margin=2.0)


def d_vault(s, layout, room):
    lamps(s, layout, room, "NS")
    fill(s, layout, room, "decor_crate", 2.4, 2.4, margin=2.0)
    accents(s, layout, room, "EW", "decor_locker", 2.0, 0.0)


def d_pad_room(s, layout, room):
    lamps(s, layout, room)
    s.stand("decor_pad", room.cx, room.cy)
    desk_line(s, layout, room, "N", 4.0, margin=3.0)
    accents(s, layout, room, "EW", "decor_locker", 3.0, 0.0)
    accents(s, layout, room, "E", "decor_switch_panel", 6, 1.3, offset=1.5)


def d_eva(s, layout, room):
    lamps(s, layout, room)
    accents(s, layout, room, "EW", "decor_locker", 1.3, 0.0)
    accents(s, layout, room, "N", "decor_locker", 1.3, 0.0)
    fill(s, layout, room, "decor_bench", 6.0, 6.0, margin=3.5)
    fill(s, layout, room, "decor_crate", 6.0, 6.0, margin=6.0)


def d_theatre(s, layout, room):
    lamps(s, layout, room)
    fill(s, layout, room, "decor_sofa", 2.6, 2.6, margin=2.5, facing="S", margin_y=4.0)
    for x in spaced(room.x0, room.x1, 3.0, 2.5):
        s.stand("decor_table", x, room.y1 - 2.2, "S")
    corner_planters(s, layout, room)


def d_checkpoint(s, layout, room):
    lamps(s, layout, room, "NS")
    accents(s, layout, room, "S", "decor_counter", 2.3, 0.0, inset=0.3)
    desk_line(s, layout, room, "N", 3.3, margin=2.0)
    accents(s, layout, room, "EW", "decor_locker", 1.8, 0.0)
    fill(s, layout, room, "decor_bench", 6.0, 6.0, margin=3.0, skip=lambda i, j: j != 1)


def d_brig(s, layout, room):
    lamps(s, layout, room, "NS")
    cell_depth = 8
    bar_y = room.y0 + cell_depth
    cells = [room.x0 + 1, room.x0 + 5, room.x0 + 9, room.x0 + 13]
    for x in range(room.x0 + 1, room.x1 - 1):
        for row in range(DOOR_ROWS):
            s.wall_piece("wall_bars", "N", bar_y, x, row)
    for x in cells + [room.x1 - 1]:
        for y in range(room.y0 + 1, bar_y):
            for row in range(DOOR_ROWS):
                s.wall_piece("wall_plate_riveted", "E", x, y, row)
    for x in cells[:-1]:
        s.stand("decor_bed", x + 2.1, room.y0 + 4.0, "S")
    desk_line(s, layout, room, "N", 4.0, margin=3.0)
    accents(s, layout, room, "EW", "decor_locker", 2.0, 0.0, offset=11.0)


def d_prison(s, layout, room):
    lamps(s, layout, room, "NS")
    for y in spaced(room.y0, room.y1, 5.0, 2.5):
        s.stand("decor_bed", room.x0 + 1.4, y, "E")
        s.stand("decor_bed", room.x1 - 1.4, y, "E")
    accents(s, layout, room, "N", "decor_toilet", 3.0, 0.0)
    dining_set(s, layout, room, 5.0, 5.0, margin=4.5)


def d_interrogation(s, layout, room):
    lamps(s, layout, room, "NS")
    s.stand("decor_table", room.cx, room.cy)
    s.stand("decor_crew_chair", room.cx, room.cy - DESK_CHAIR_GAP, "N")
    s.stand("decor_crew_chair", room.cx, room.cy + DESK_CHAIR_GAP, "S")
    accents(s, layout, room, "EW", "decor_locker", 2.5, 0.0)


def d_armory(s, layout, room):
    lamps(s, layout, room, "NS")
    accents(s, layout, room, "EW", "decor_locker", 1.3, 0.0)
    accents(s, layout, room, "N", "decor_locker", 1.3, 0.0)
    fill(s, layout, room, "decor_table", 3.0, 4.0, margin=3.0)
    fill(s, layout, room, "decor_crate", 3.0, 4.0, margin=3.0, skip=lambda i, j: j % 2 == 0)


def d_warden(s, layout, room):
    lamps(s, layout, room, "NS")
    desk_line(s, layout, room, "N", 3.0, margin=1.5)
    accents(s, layout, room, "EW", "decor_locker", 2.0, 0.0, offset=6.0)
    accents(s, layout, room, "S", "decor_shelf", 2.2, 0.0)
    s.stand("decor_sofa", room.cx, room.cy - 3, "S")


def d_detective(s, layout, room):
    lamps(s, layout, room, "NS")
    desk_line(s, layout, room, "N", 4.0, margin=1.5)
    accents(s, layout, room, "EW", "decor_shelf", 2.2, 0.0, offset=4.0)
    fill(s, layout, room, "decor_sofa", 6.0, 6.0, margin=3.0, skip=lambda i, j: j != 0)
    s.stand("decor_table", room.cx, room.cy - 2)


def d_courtroom(s, layout, room):
    lamps(s, layout, room, "EW")
    fill(s, layout, room, "decor_sofa", 6.0, 2.4, margin=3.0, facing="N", margin_y=3.0, skip=lambda i, j: j > 4)
    s.stand("decor_table", room.cx, room.y1 - 3.0, "S")
    desk_line(s, layout, room, "S", 4.0, margin=2.0)
    accents(s, layout, room, "EW", "decor_shelf", 2.2, 0.0, offset=11.0)


def d_library(s, layout, room):
    lamps(s, layout, room, "NS")
    accents(s, layout, room, "N", "decor_shelf", 2.1, 0.0)
    accents(s, layout, room, "E", "decor_shelf", 2.1, 0.0)
    accents(s, layout, room, "W", "decor_shelf", 2.1, 0.0)
    dining_set(s, layout, room, 4.0, 5.0, margin=4.0)
    fill(s, layout, room, "decor_sofa", 6.0, 6.0, margin=3.0, skip=lambda i, j: j != 0)


def d_chapel(s, layout, room):
    lamps(s, layout, room)
    fill(s, layout, room, "decor_sofa", 3.2, 2.2, margin=2.2, facing="N", margin_y=3.0, skip=lambda i, j: j >= 6)
    s.stand("decor_table", room.cx, room.y1 - 2.5, "S")
    corner_planters(s, layout, room)
    accents(s, layout, room, "EW", "decor_shelf", 2.2, 0.0, offset=11.0)


def d_bar(s, layout, room):
    lamps(s, layout, room)
    for x in spaced(room.x0, room.x1, 2.2, 3.0):
        s.stand("decor_counter", x, room.y1 - 5.5, "S")
        s.stand("decor_crew_chair", x, room.y1 - 6.6, "N")
    accents(s, layout, room, "N", "decor_shelf", 2.1, 0.0)
    dining_set(s, layout, room, 4.5, 4.0, margin=3.0)
    accents(s, layout, room, "E", "decor_vending", 3.0, 0.0)


def d_kitchen(s, layout, room):
    lamps(s, layout, room)
    accents(s, layout, room, "N", "decor_counter", 2.1, 0.0, inset=0.3)
    accents(s, layout, room, "S", "decor_counter", 2.1, 0.0, inset=0.3)
    accents(s, layout, room, "E", "decor_locker", 1.5, 0.0)
    fill(s, layout, room, "decor_table", 3.5, 3.5, margin=4.5)


def d_dining(s, layout, room):
    lamps(s, layout, room)
    dining_set(s, layout, room, 4.2, 3.4, margin=3.0)
    accents(s, layout, room, "N", "decor_counter", 2.1, 0.0, inset=0.3)
    accents(s, layout, room, "S", "decor_vending", 4.0, 0.0)
    corner_planters(s, layout, room)


def d_hydroponics(s, layout, room):
    lamps(s, layout, room, "NS")
    fill(s, layout, room, "decor_planter", 3.2, 3.2, margin=3.0)
    accents(s, layout, room, "N", "decor_counter", 2.1, 0.0, inset=0.3)
    accents(s, layout, room, "EW", "decor_locker", 2.0, 0.0)


def d_cargo_bay(s, layout, room):
    lamps(s, layout, room)
    for i, x in enumerate(spaced(room.x0, room.x1, 3.5, 3.0)):
        for j, y in enumerate(spaced(room.y0, room.y1, 3.5, 3.0)):
            if i == 2 or (i + j) % 4 == 3 or not layout.point_free(room, x, y):
                continue
            for z in range((i + j) % 3 + 1):
                s.stand("decor_crate", x, y, z=float(z))
    desk_line(s, layout, room, "E", 5.0, margin=3.0)
    accents(s, layout, room, "S", "decor_locker", 2.0, 0.0)


def d_office(s, layout, room):
    lamps(s, layout, room)
    desk_line(s, layout, room, "N", 3.3, margin=2.0)
    accents(s, layout, room, "EW", "decor_locker", 2.0, 0.0, offset=5.0)
    accents(s, layout, room, "S", "decor_shelf", 2.2, 0.0)
    fill(s, layout, room, "decor_sofa", 6.0, 6.0, margin=3.0, skip=lambda i, j: j != 1)


def d_mail(s, layout, room):
    lamps(s, layout, room)
    accents(s, layout, room, "NS", "decor_counter", 2.1, 0.0, inset=0.3)
    fill(s, layout, room, "decor_crate", 3.5, 3.5, margin=4.0)
    accents(s, layout, room, "EW", "decor_locker", 1.5, 0.0)


def d_mining(s, layout, room):
    lamps(s, layout, room)
    fill(s, layout, room, "decor_crate", 3.5, 3.5, margin=3.0)
    accents(s, layout, room, "EW", "decor_locker", 1.5, 0.0)
    s.stand("decor_pad", room.cx, room.cy)
    desk_line(s, layout, room, "N", 5.0, margin=3.0)


def d_gym(s, layout, room):
    lamps(s, layout, room)
    fill(s, layout, room, "decor_bench", 4.0, 4.0, margin=3.0)
    accents(s, layout, room, "EW", "decor_locker", 1.5, 0.0)
    accents(s, layout, room, "N", "decor_vending", 4.0, 0.0)
    corner_planters(s, layout, room)


def d_engine(s, layout, room):
    lamps(s, layout, room)
    s.stand("decor_reactor", room.cx, room.cy)
    for x in (room.x0 + 3, room.x1 - 3):
        for y in (room.y0 + 3, room.y1 - 3):
            s.pillar(x, y, room.height)
    station_row(s, layout, room, 8, "S", margin=6.0)
    accents(s, layout, room, "N", "decor_pipe_panel", 6, 1.8)
    accents(s, layout, room, "EW", "decor_pipe_panel", 5, 1.8)
    accents(s, layout, room, "N", "decor_locker", 4.0, 0.0, offset=2.0)
    fill(s, layout, room, "decor_crate", 5.0, 5.0, margin=8.0, skip=lambda i, j: abs(i - j) < 4)


def d_power(s, layout, room):
    lamps(s, layout, room)
    fill(s, layout, room, "decor_mainframe", 3.0, 3.0, margin=3.0, skip=lambda i, j: j == 0)
    desk_line(s, layout, room, "N", 4.0, margin=3.0)
    accents(s, layout, room, "S", "decor_switch_panel", 3, 1.3)
    accents(s, layout, room, "EW", "decor_pipe_panel", 5, 1.6)


def d_atmos(s, layout, room):
    lamps(s, layout, room)
    accents(s, layout, room, "NS", "decor_pipe_panel", 4, 1.6)
    accents(s, layout, room, "EW", "decor_wall_vent_grille", 5, 1.8)
    fill(s, layout, room, "decor_crate", 3.0, 3.0, margin=3.5)
    desk_line(s, layout, room, "S", 6.0, margin=3.0)


def d_telecom(s, layout, room):
    lamps(s, layout, room)
    fill(s, layout, room, "decor_mainframe", 2.8, 3.0, margin=2.5)
    desk_line(s, layout, room, "N", 4.0, margin=2.0)


def d_tools(s, layout, room):
    lamps(s, layout, room)
    accents(s, layout, room, "NS", "decor_counter", 2.1, 0.0, inset=0.3)
    accents(s, layout, room, "EW", "decor_locker", 1.5, 0.0)
    fill(s, layout, room, "decor_crate", 3.5, 3.5, margin=3.5)


def d_solars(s, layout, room):
    lamps(s, layout, room, "NS")
    desk_line(s, layout, room, "N", 3.5, margin=2.0)
    accents(s, layout, room, "EW", "decor_pipe_panel", 4, 1.6)
    accents(s, layout, room, "S", "decor_switch_panel", 3, 1.3)


def d_disposal(s, layout, room):
    lamps(s, layout, room, "NS")
    fill(s, layout, room, "decor_crate", 2.8, 2.8, margin=2.5)
    accents(s, layout, room, "EW", "decor_pipe_panel", 4, 1.6)


def d_wards(s, layout, room):
    lamps(s, layout, room)
    fill(s, layout, room, "decor_bed", 3.2, 4.0, margin=2.5)
    desk_line(s, layout, room, "N", 4.0, margin=3.0)
    accents(s, layout, room, "W", "decor_counter", 2.1, 0.0, inset=0.3)
    accents(s, layout, room, "E", "decor_locker", 2.0, 0.0)


def d_cryo(s, layout, room):
    lamps(s, layout, room, "EW")
    fill(s, layout, room, "decor_cryo", 2.6, 4.0, margin=2.5)
    desk_line(s, layout, room, "N", 4.0, margin=2.5)
    accents(s, layout, room, "S", "decor_locker", 2.0, 0.0)


def d_genetics(s, layout, room):
    lamps(s, layout, room)
    desk_line(s, layout, room, "N", 3.2, margin=2.0)
    accents(s, layout, room, "E", "decor_counter", 2.1, 0.0, inset=0.3)
    fill(s, layout, room, "decor_cryo", 5.0, 6.0, margin=3.5, skip=lambda i, j: j == 0)
    accents(s, layout, room, "W", "decor_locker", 2.0, 0.0)


def d_garage(s, layout, room):
    lamps(s, layout, room, "EW")
    fill(s, layout, room, "decor_bed", 4.0, 5.0, margin=3.0)
    accents(s, layout, room, "EW", "decor_locker", 1.5, 0.0)
    fill(s, layout, room, "decor_crate", 4.0, 5.0, margin=3.0, skip=lambda i, j: (i + j) % 2 == 0)


def d_lockers(s, layout, room):
    lamps(s, layout, room, "EW")
    accents(s, layout, room, "EW", "decor_locker", 1.1, 0.0)
    accents(s, layout, room, "N", "decor_locker", 1.1, 0.0)
    fill(s, layout, room, "decor_bench", 6.0, 4.0, margin=3.0)


def d_surgery(s, layout, room):
    lamps(s, layout, room)
    for x in (room.cx - 3, room.cx + 3):
        s.stand("decor_bed", x, room.cy)
    accents(s, layout, room, "NS", "decor_counter", 2.1, 0.0, inset=0.3)
    accents(s, layout, room, "EW", "decor_locker", 2.0, 0.0)
    desk_line(s, layout, room, "N", 5.0, margin=3.0, depth=1.3)


def d_chemistry(s, layout, room):
    lamps(s, layout, room)
    accents(s, layout, room, "NS", "decor_counter", 2.1, 0.0, inset=0.3)
    accents(s, layout, room, "EW", "decor_locker", 2.0, 0.0)
    fill(s, layout, room, "decor_table", 3.5, 3.5, margin=4.5)
    desk_line(s, layout, room, "W", 5.0, margin=4.0)


def d_virology(s, layout, room):
    lamps(s, layout, room)
    fill(s, layout, room, "decor_bed", 4.0, 5.0, margin=3.0)
    accents(s, layout, room, "NS", "decor_counter", 2.1, 0.0, inset=0.3)
    desk_line(s, layout, room, "E", 5.0, margin=3.0)


def d_morgue(s, layout, room):
    lamps(s, layout, room, "NS")
    accents(s, layout, room, "N", "decor_morgue", 2.5, 0.0)
    accents(s, layout, room, "EW", "decor_morgue", 3.0, 0.0, offset=1.0)
    fill(s, layout, room, "decor_bed", 4.0, 4.0, margin=3.5)


def d_robotics(s, layout, room):
    lamps(s, layout, room)
    for y in spaced(room.y0, room.y1, 5.0, 4.0):
        for x in spaced(room.x0, room.x1, 4.0, 3.0):
            s.stand("decor_counter", x, y)
            s.stand("decor_monitor_crt", x, y, z=0.95)
    accents(s, layout, room, "N", "decor_switch_panel", 4.0, 1.3)
    accents(s, layout, room, "EW", "decor_locker", 2.0, 0.0)


def d_mech_bay(s, layout, room):
    lamps(s, layout, room)
    fill(s, layout, room, "decor_pad", 4.0, 4.0, margin=3.5)
    accents(s, layout, room, "EW", "decor_counter", 2.1, 0.0, inset=0.3)
    accents(s, layout, room, "S", "decor_locker", 2.0, 0.0)


def d_xenobio(s, layout, room):
    lamps(s, layout, room)
    fill(s, layout, room, "decor_planter", 3.2, 3.0, margin=2.5)
    accents(s, layout, room, "NS", "decor_counter", 2.1, 0.0, inset=0.3)
    desk_line(s, layout, room, "E", 5.0, margin=3.0)


def d_toxins(s, layout, room):
    lamps(s, layout, room)
    accents(s, layout, room, "NS", "decor_counter", 2.1, 0.0, inset=0.3)
    desk_line(s, layout, room, "E", 4.0, margin=3.0)
    accents(s, layout, room, "W", "decor_locker", 2.0, 0.0)
    fill(s, layout, room, "decor_crate", 4.0, 4.0, margin=4.0)


def d_toxins_test(s, layout, room):
    lamps(s, layout, room, "NS")
    for x in (room.x0 + 3, room.x1 - 3):
        s.pillar(x, room.cy, room.height)
    fill(s, layout, room, "decor_crate", 3.0, 3.0, margin=4.5)


def d_labs(s, layout, room):
    lamps(s, layout, room)
    for y in spaced(room.y0, room.y1, 4.5, 3.0):
        for x in spaced(room.x0, room.x1, 4.0, 3.0):
            s.stand("decor_counter", x, y)
            s.stand("decor_monitor_crt", x, y, z=0.95)
    accents(s, layout, room, "N", "decor_switch_panel", 4.0, 1.3)
    accents(s, layout, room, "EW", "decor_locker", 2.0, 0.0)


def d_janitor(s, layout, room):
    lamps(s, layout, room, "NS")
    accents(s, layout, room, "EW", "decor_locker", 1.5, 0.0)
    accents(s, layout, room, "N", "decor_shower", 2.5, 0.0)
    fill(s, layout, room, "decor_crate", 3.0, 3.5, margin=3.0)


def d_restrooms(s, layout, room):
    lamps(s, layout, room, "NS")
    accents(s, layout, room, "N", "decor_toilet", 1.8, 0.0)
    accents(s, layout, room, "S", "decor_shower", 2.2, 0.0, offset=0.4)
    accents(s, layout, room, "E", "decor_vending", 4.0, 0.0)


def d_dock(s, layout, room):
    lamps(s, layout, room, "NS")
    fill(s, layout, room, "decor_crew_chair", 2.2, 2.0, margin=2.5, facing="N", margin_y=2.5)
    accents(s, layout, room, "NS", "decor_lamp_beacon", 4, 2.3)
    accents(s, layout, room, "EW", "decor_locker", 2.0, 0.0)


def d_airlock(s, layout, room):
    long = long_edges(room)
    lamps(s, layout, room, long)
    for edge in "NSEW":
        wall_decor(s, layout, room, edge, "decor_lamp_beacon", sum(edge_range(room, edge)) / 2, AIRLOCK_LIGHT_Z)
    accents(s, layout, room, long, "decor_locker", 1.3, 0.0, offset=0.5)
    accents(s, layout, room, long[:1], "decor_switch_panel", 3.0, 1.3, offset=0.5)
    accents(s, layout, room, long[1:], "decor_pipe_panel", 5.0, 1.6)


DECOR = {
    name[2:]: fn for name, fn in list(globals().items()) if name.startswith("d_") and callable(fn)
}


# ----- The station -----


def R(name, x0, y0, x1, y1, kind=None, height=3, style="public", **extra):
    return Room(name, x0, y0, x1, y1, kind or name, height, style, **extra)


def corridor(name, x0, y0, x1, y1):
    return R(name, x0, y0, x1, y1, "corridor", style="corridor")


def tunnel(name, x0, y0, x1, y1, **extra):
    return R(name, x0, y0, x1, y1, "tunnel", style="maint", **extra)


def maint_room(name, x0, y0, x1, y1):
    return R(name, x0, y0, x1, y1, "maint_room", style="maint")


def old(name, x0, y0, x1, y1, kind):
    """A room of the abandoned west sector, left as the catastrophe left it."""
    return R(name, x0, y0, x1, y1, kind, style="maint")


AIRLOCK_PREP_DEPTH = 5


def airlock_pair(name, x0, y0, x1, y1, inner, free=False, depth=AIRLOCK_PREP_DEPTH):
    """An airlock in two zones: an equipment room on the station side and the chamber with the outer door beyond it."""
    cuts = {
        "E": ((x1 - depth, y0, x1, y1), (x0, y0, x1 - depth, y1)),
        "W": ((x0, y0, x0 + depth, y1), (x0 + depth, y0, x1, y1)),
        "N": ((x0, y1 - depth, x1, y1), (x0, y0, x1, y1 - depth)),
        "S": ((x0, y0, x1, y0 + depth), (x0, y0 + depth, x1, y1)),
    }
    prep, chamber = cuts[inner]
    return [stub(f"{name}_prep", *prep, kind="airlock_prep", free=free), stub(name, *chamber, free=free)]


def stub(name, x0, y0, x1, y1, kind="airlock", free=False):
    """A room outside the octagon, hugging its edge: only the cells beyond the outline exist (all of them when free)."""
    return R(name, x0, y0, x1, y1, kind, style="engineering", outside=True, free=free)


# (tunnel name, tunnel rect, lock name, lock rect, the lock's outer wall, the room the tunnel opens into)
SPURS = (
    ("mt_sp_s1", (36, -8, 38, 0), "lock_ts1", (36, -14, 38, -8), "S", "power"),
    ("mt_sp_s2", (100, -8, 102, 0), "lock_ts2", (100, -14, 102, -8), "S", "telecom"),
    ("mt_sp_s3", (132, -8, 134, 0), "lock_ts3", (132, -14, 134, -8), "S", "ore_processing"),
    ("mt_sp_n1", (34, 132, 36, 140), "lock_tn1", (34, 140, 36, 146), "N", "hop"),
    ("mt_sp_n2", (132, 132, 134, 140), "lock_tn2", (132, 140, 134, 146), "N", "barracks"),
    ("mt_sp_e1", (164, 74, 176, 76), "lock_te1", (176, 74, 182, 76), "E", "toxins_test"),
    ("mt_sp_e2", (164, 44, 172, 46), "lock_te2", (172, 44, 178, 46), "E", "xenobio"),
)
# technical airlocks at the west ends of the maintenance tunnels: (lock name, rect, the tunnel it closes, free rectangle)
WEST_LOCKS = (("lock_tw1", (1, 16, 9, 18), "mt_dorm_s", True), ("lock_tw2", (-14, 34, -4, 36), "mt_dorm_nw", False))
# the abandoned corridor runs north to south between the old sector (west) and the working station (east)
SPINE = (32, 18, 36, 104)
OLD_CABIN_X = -3
OLD_CABIN_COUNT = 7
ACTIVE_CABIN_X = 40
ACTIVE_CABIN_COUNT = 5
CABIN_WIDTH = 5
CABIN_DEPTH = 6


def spurs():
    rooms = []
    for name, tunnel_rect, lock, lock_rect, _, _ in SPURS:
        rooms.append(tunnel(name, *tunnel_rect, outside=True))
        rooms.append(stub(lock, *lock_rect))
    return rooms + [stub(lock, *rect, free=free) for lock, rect, _, free in WEST_LOCKS]


def cabins():
    rooms = []
    for side, y0 in (("n", 28), ("s", 18)):
        for k in range(OLD_CABIN_COUNT):
            x = OLD_CABIN_X + CABIN_WIDTH * k
            rooms.append(old(f"old_cabin_{side}{k}", x, y0, x + CABIN_WIDTH, y0 + CABIN_DEPTH, "old_cabin"))
        for k in range(ACTIVE_CABIN_COUNT):
            x = ACTIVE_CABIN_X + CABIN_WIDTH * k
            rooms.append(R(f"cabin_{side}{k}", x, y0, x + CABIN_WIDTH, y0 + CABIN_DEPTH, "cabin", style="quarters"))
    return rooms


HEAD_CABINS = (
    ("hc_sec", 38, 108),
    ("hc_med", 38, 114),
    ("hc_eng", 38, 120),
    ("hc_sci", 38, 126),
    ("hc_qm", 50, 108),
    ("hc_hop", 50, 114),
    ("hc_guest1", 50, 120),
    ("hc_guest2", 50, 126),
)
HEAD_CABIN_WIDTH = 8
HEAD_CABIN_DEPTH = 6


def head_cabins():
    return [
        R(name, x, y, x + HEAD_CABIN_WIDTH, y + HEAD_CABIN_DEPTH, "head_cabin", style="quarters")
        for name, x, y in HEAD_CABINS
    ]


ROOMS = [
    # the working station's corridors: three long east-west ones end at the abandoned corridor on their west side
    corridor("c1", 2, 104, 158, 108),
    corridor("c2", 36, 80, 164, 84),
    corridor("m_w", 36, 62, 68, 66),
    corridor("m_e", 92, 62, 164, 66),
    corridor("c3", 36, 36, 164, 40),
    corridor("s1", 78, 84, 82, 104),
    corridor("s2", 78, 76, 82, 80),
    corridor("s3", 78, 40, 82, 52),
    corridor("s4", 78, 16, 82, 36),
    tunnel("v3b", 120, 66, 124, 80),
    tunnel("v3c", 120, 40, 124, 62),
    tunnel("v3d", 120, 16, 124, 36),
    corridor("v4", 160, 84, 164, 104),
    corridor("dorm_hall", 40, 24, 65, 28),
    R("atrium", 68, 52, 92, 76, height=6),
    # the abandoned corridor and the old sector's own corridors, which it cuts off from the rest
    tunnel("old_spine", *SPINE),
    tunnel("mt_sa", 36, 18, 40, 36),
    tunnel("mt_sb", 36, 40, 40, 62),
    tunnel("mt_sc", 36, 66, 40, 80),
    tunnel("mt_sd", 36, 84, 40, 104),
    tunnel("old_c2", -4, 80, 32, 84),
    tunnel("old_m", -4, 62, 32, 66),
    tunnel("old_c3", -4, 36, 32, 40),
    tunnel("old_hall", OLD_CABIN_X, 24, 32, 28),
    # command row (the corner rooms the outline cuts away are gone; the AI core took the room beside)
    R("ai_core", 16, 108, 24, 132, style="security"),
    R("hop", 24, 108, 38, 132, style="bridge"),
    corridor("heads_hall", 46, 108, 50, 132),
    R("virology", 58, 108, 68, 132, style="medical"),
    R("bridge", 68, 108, 92, 132, height=4, style="bridge", glass_north=True),
    R("captain", 92, 108, 106, 132, style="quarters"),
    R("vault", 106, 108, 116, 132, style="security"),
    R("teleporter", 116, 108, 128, 132, "pad_room", style="science"),
    R("barracks", 128, 108, 140, 132, style="quarters"),
    R("gateway", 140, 108, 152, 132, "pad_room", style="science"),
    # security: the old block in the abandoned west, the temporary brig where the courtroom was
    old("old_canteen", -4, 84, 4, 104, "old_canteen"),
    old("old_checkpoint", 4, 84, 12, 104, "old_guard"),
    old("old_brig", 12, 84, 26, 104, "old_brig"),
    old("old_prison", 26, 84, 32, 104, "old_brig"),
    R("interrogation", 40, 84, 48, 104, style="security"),
    R("armory", 48, 84, 54, 104, style="security"),
    R("warden", 54, 84, 60, 104, style="security"),
    R("detective", 60, 84, 68, 94, style="security"),
    R("hos_office", 60, 94, 68, 104, "office", style="security"),
    R("temp_brig", 68, 84, 78, 104, "prison", style="security"),
    R("library", 82, 84, 94, 104, style="public"),
    R("chapel", 94, 84, 108, 104, style="public"),
    R("dining", 108, 84, 128, 104, style="public"),
    R("kitchen", 128, 84, 140, 104, style="public"),
    R("hydroponics", 140, 84, 160, 104, style="science"),
    # medical: the old kitchen, morgue, laboratories and clinic in the abandoned west; the surgery near the centre
    old("old_kitchen", -4, 66, 4, 80, "old_kitchen"),
    old("old_morgue", 4, 66, 20, 80, "old_medbay"),
    old("old_virology", 20, 66, 32, 80, "old_lab"),
    R("chemistry", 40, 66, 54, 80, style="medical"),
    R("surgery", 54, 66, 68, 80, style="medical"),
    old("old_lab", -4, 40, 4, 62, "old_lab"),
    old("old_cryo", 4, 40, 16, 62, "old_medbay"),
    old("old_genetics", 16, 40, 28, 62, "old_lab"),
    tunnel("old_garage", 28, 40, 32, 62),
    R("wards", 40, 40, 58, 62, style="medical"),
    R("morgue", 58, 40, 68, 54, style="medical"),
    R("cmo_office", 58, 54, 68, 62, "office", style="medical"),
    # east of the atrium: public facilities near the centre, science and its hazards towards the edge
    R("restrooms", 92, 66, 104, 80, style="quarters"),
    R("janitor", 104, 66, 112, 80, style="cargo"),
    R("server_room", 112, 66, 120, 80, "server", style="science"),
    R("labs", 124, 66, 140, 80, style="science"),
    R("toxins", 140, 66, 152, 80, style="science"),
    R("toxins_test", 152, 66, 164, 80, height=6, style="science"),
    R("rd_office", 92, 40, 102, 62, "office", style="science"),
    R("robotics", 102, 40, 120, 62, style="science"),
    R("mech_bay", 124, 40, 144, 62, style="science"),
    R("xenobio", 144, 40, 164, 62, style="science"),
    # docks
    R("arrivals", 164, 86, 188, 102, "dock", height=6, style="cargo", outside=True),
    R("escape", 164, 56, 188, 72, "dock", height=6, style="cargo", outside=True),
    R("cargo_dock", 164, 0, 188, 34, "dock", height=6, style="cargo", outside=True),
    # crew life: the lounge by the dorms, the gym and the bar south of the atrium
    R("lounge", 65, 18, 78, 36, style="quarters"),
    R("gym", 82, 18, 94, 36, style="quarters"),
    R("bar", 94, 18, 106, 36, style="public"),
    # cargo: the office and the mail near the centre, the bay and the mining hall by the dock
    R("mail", 106, 18, 114, 36, style="cargo"),
    R("quartermaster", 114, 18, 120, 36, "office", style="cargo"),
    R("cargo_bay", 124, 18, 148, 36, style="cargo"),
    R("mining", 148, 18, 164, 36, style="cargo"),
    # engineering, stern
    R("atmos", 20, 0, 30, 16, style="engineering"),
    R("power", 30, 0, 46, 16, style="engineering"),
    R("engine", 46, 0, 98, 16, height=6, style="engineering"),
    R("telecom", 98, 0, 108, 16, style="engineering"),
    R("tools", 108, 0, 116, 16, style="engineering"),
    R("ce_office", 116, 0, 124, 16, "office", style="engineering"),
    R("disposal", 124, 0, 130, 16, style="engineering"),
    R("ore_processing", 130, 0, 146, 16, style="cargo"),
    # maintenance: tunnels in the free strips inside the station; spurs with technical airlocks are added below
    tunnel("mt_dorm_s", 9, 16, 78, 18),
    tunnel("mt_dorm_nw", -4, 34, 32, 36),
    tunnel("mt_dorm_n", 40, 34, 65, 36),
    tunnel("mt_s2", 82, 16, 120, 18),
    tunnel("mt_s3", 124, 16, 164, 18),
    maint_room("mt_hub_w", 68, 40, 78, 52),
    maint_room("mt_hub_e", 82, 40, 92, 52),
    maint_room("mt_pocket_nw", 68, 76, 78, 80),
    maint_room("mt_pocket_ne", 82, 76, 92, 80),
    maint_room("mt_corner_nw", 2, 108, 16, 122),
    maint_room("mt_corner_sw", 10, 6, 20, 16),
    maint_room("mt_corner_ne", 152, 108, 158, 114),
    # airlocks at the ends of the corridors: an inner door to the corridor, an outer door to space
    *airlock_pair("lock_c1w", -8, 104, 2, 108, "E", free=True),
    *airlock_pair("lock_c1e", 158, 104, 168, 108, "W", free=True),
    *airlock_pair("lock_c2w", -14, 80, -4, 84, "E"),
    *airlock_pair("lock_c2e", 164, 80, 176, 84, "W"),
    *airlock_pair("lock_c3w", -14, 36, -4, 40, "E"),
    *airlock_pair("lock_c3e", 164, 36, 176, 40, "W"),
    *airlock_pair("lock_engine", 76, -6, 84, 0, "N", depth=3),
] + cabins() + head_cabins() + spurs()

EXTERIOR_DOORS = (
    [("lock_c1w", "W"), ("lock_c1e", "E"), ("lock_c2w", "W"), ("lock_c2e", "E"), ("lock_c3w", "W"), ("lock_c3e", "E"),
     ("lock_engine", "S")]
    + [(lock, edge) for _, _, lock, _, edge, _ in SPURS]
    + [(lock, "W") for lock, _, _, _ in WEST_LOCKS]
)

CORRIDOR_LINKS = (
    ("c1", "s1"), ("s1", "c2"), ("c2", "s2"), ("s2", "atrium"), ("atrium", "s3"), ("s3", "c3"), ("c3", "s4"),
    ("s4", "engine"), ("atrium", "m_w"), ("atrium", "m_e"),
    ("c2", "v3b"), ("v3b", "m_e"), ("m_e", "v3c"), ("v3c", "c3"), ("c3", "v3d"), ("v3d", "ce_office"),
    ("c1", "v4"), ("v4", "c2"),
)

LINKS = (
    list(CORRIDOR_LINKS)
    + [("v4", "arrivals"), ("m_e", "escape")]
    + [(room, "c1") for room in (
        "ai_core", "hop", "virology", "bridge", "captain", "vault", "teleporter", "barracks", "gateway",
        "old_checkpoint", "old_brig", "old_prison", "interrogation", "warden", "hos_office", "temp_brig",
        "library", "chapel", "dining", "kitchen", "hydroponics",
    )]
    + [(room, "m_w") for room in ("chemistry", "surgery", "wards", "cmo_office")]
    + [(room, "old_m") for room in ("old_morgue", "old_virology", "old_kitchen", "old_lab", "old_cryo", "old_genetics", "old_garage")]
    + [(room, "old_c2") for room in ("old_canteen", "old_kitchen")]
    + [(room, "old_c3") for room in ("old_lab", "old_cryo", "old_genetics", "old_garage")]
    + [(room, "m_e") for room in (
        "restrooms", "janitor", "server_room", "labs", "toxins", "toxins_test",
        "rd_office", "robotics", "mech_bay", "xenobio",
    )]
    + [(room, "c3") for room in ("lounge", "gym", "bar", "mail", "quartermaster", "cargo_bay", "mining", "morgue")]
    + [("detective", "c2"), ("lounge", "dorm_hall"), ("old_checkpoint", "old_canteen"), ("old_kitchen", "old_morgue"),
       ("old_brig", "old_prison"), ("armory", "warden"), ("dining", "kitchen"), ("kitchen", "hydroponics")]
    + [(f"cabin_{side}{k}", "dorm_hall") for side in "ns" for k in range(ACTIVE_CABIN_COUNT)]
    + [(f"old_cabin_{side}{k}", "old_hall") for side in "ns" for k in range(OLD_CABIN_COUNT)]
    + [("heads_hall", "c1")]
    + [(name, "heads_hall") for name, _, _ in HEAD_CABINS]
    + [link for lock, zone in (
        ("lock_c1w", "c1"), ("lock_c1e", "c1"), ("lock_c2w", "old_c2"), ("lock_c2e", "c2"), ("lock_c3w", "old_c3"),
        ("lock_c3e", "c3"), ("lock_engine", "engine"),
    ) for link in ((f"{lock}_prep", zone), (f"{lock}_prep", lock))]
    + [("atmos", "power"), ("power", "engine"), ("engine", "telecom"), ("telecom", "tools"), ("tools", "ce_office"),
       ("ce_office", "disposal"), ("disposal", "ore_processing"), ("mining", "cargo_bay"), ("cargo_dock", "mining")]
    + [
        # the abandoned corridor: the working corridors end at it on one side, the old sector's on the other
        ("old_spine", "c1"), ("old_spine", "c2"), ("old_spine", "m_w"), ("old_spine", "c3"),
        ("old_spine", "old_c2"), ("old_spine", "old_m"), ("old_spine", "old_c3"), ("old_spine", "old_hall"),
        ("old_spine", "mt_dorm_s"), ("old_spine", "mt_dorm_nw"),
        ("old_spine", "mt_sa"), ("old_spine", "mt_sb"), ("old_spine", "mt_sc"), ("old_spine", "mt_sd"),
        ("mt_sa", "c3"), ("mt_sa", "dorm_hall"), ("mt_sa", "mt_dorm_n"), ("mt_sb", "c3"), ("mt_sb", "m_w"), ("mt_sb", "wards"),
        ("mt_sc", "m_w"), ("mt_sc", "c2"), ("mt_sc", "chemistry"), ("mt_sd", "c2"), ("mt_sd", "c1"), ("mt_sd", "interrogation"),
        ("mt_corner_nw", "c1"), ("mt_corner_nw", "ai_core"), ("mt_corner_sw", "atmos"), ("mt_corner_sw", "mt_dorm_s"),
        ("mt_corner_ne", "gateway"), ("mt_corner_ne", "c1"),
        ("mt_dorm_nw", "old_c3"), ("mt_dorm_n", "c3"), ("mt_dorm_n", "lounge"),
        ("mt_dorm_s", "s4"), ("mt_dorm_s", "lounge"), ("mt_dorm_s", "atmos"), ("mt_dorm_s", "power"), ("mt_dorm_s", "engine"),
        ("mt_hub_w", "s3"), ("mt_hub_w", "c3"), ("mt_hub_w", "atrium"),
        ("mt_hub_e", "s3"), ("mt_hub_e", "c3"), ("mt_hub_e", "atrium"),
        ("mt_pocket_nw", "s2"), ("mt_pocket_nw", "c2"), ("mt_pocket_nw", "atrium"),
        ("mt_pocket_ne", "s2"), ("mt_pocket_ne", "c2"), ("mt_pocket_ne", "atrium"),
        ("s4", "mt_s2"), ("v3d", "mt_s2"), ("v3d", "mt_s3"),
        ("mt_s2", "engine"), ("mt_s2", "telecom"), ("mt_s2", "tools"), ("mt_s2", "bar"), ("mt_s2", "mail"),
        ("mt_s2", "quartermaster"),
        ("mt_s3", "disposal"), ("mt_s3", "ore_processing"), ("mt_s3", "mining"), ("mt_s3", "cargo_bay"),
        ("mt_s3", "cargo_dock"),
    ]
    + [link for name, _, lock, _, _, room in SPURS for link in ((name, room), (name, lock))]
    + [(lock, tunnel_name) for lock, _, tunnel_name, _ in WEST_LOCKS]
)


def build_station():
    sys.modules.setdefault("station_layout", sys.modules[__name__])
    import station_extras
    import station_extras2
    import station_fill
    import station_small
    import station_wing

    templates = ss.prepare_templates()
    ss.clear_scene_collection(COLLECTION)
    layout = Layout(ROOMS, LINKS, EXTERIOR_DOORS)
    s = ss.SceneBuilder(templates, COLLECTION, visible=layout.visible)
    build_floors(s, layout)
    build_walls(s, layout)
    build_doors(s, layout)
    for room in layout.rooms:
        DECOR[room.kind](s, layout, room)
    station_extras.add_extras(s, layout)
    station_extras2.add_extras2(s, layout)
    station_wing.add_wing(s, layout)
    station_small.add_small(s, layout)
    station_fill.add_signs(s, layout)
    return s
