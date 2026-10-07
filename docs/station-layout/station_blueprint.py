"""Shared by the station generators: decks drawn as text and their translation into blueprint blocks.

One character is 1 m (4 grid cells). A wall of the plan becomes blocks one cell thick on the grid
line through its characters' first cells: x m on the plan is cell 4x.
"""

import json

CELLS_PER_M = 4
DECK_PITCH_M = 4  # floor slab + 3.5 m room + ceiling slab
DECK_PITCH_CELLS = DECK_PITCH_M * CELLS_PER_M
ROOM_HEIGHT_CELLS = 14
SLAB_CELLS = 1

WALL, WINDOW, DOOR, SEALED, RAMP, SHAFT, DANCE, COUNTER = "#", "=", "D", "X", "/", "L", ":", "_"
HIDDEN, TUNNEL, UNDER_LEDGE, LADDER = "S", ".", "~", "H"
DOORS = DOOR + SEALED + HIDDEN


class Deck:
    def __init__(self, number, name, width, depth, sealed=False):
        self.number = number
        self.name = name
        self.sealed = sealed
        self.width = width
        self.depth = depth
        self.grid = [[" "] * width for _ in range(depth)]
        self.rooms = []
        self.rects = []
        self.counters = {}
        self.berths = 0
        # Rooms whose middle gets the station's spawn points.
        self.spawn_rooms = ()
        # Drawn over the plan only when rendering: label letters must not read as doors or ladders.
        self.labels = []

    @property
    def entry(self):
        """The doors leading onto the deck, welded shut on a sealed one."""
        return SEALED if self.sealed else DOOR

    def put(self, x, y, ch):
        self.grid[y][x] = ch

    def rect(self, x0, y0, x1, y1):
        for x in range(x0, x1 + 1):
            self.put(x, y0, WALL)
            self.put(x, y1, WALL)
        for y in range(y0, y1 + 1):
            self.put(x0, y, WALL)
            self.put(x1, y, WALL)

    def numbered(self, prefix):
        self.counters[prefix] = self.counters.get(prefix, 0) + 1
        return f"{prefix}{self.counters[prefix]}"

    def room(self, x0, y0, x1, y1, label, door=None):
        """`door`: (side, offset[, width]) with side in N/S/W/E; offset from the room's start, None centres it."""
        self.rect(x0, y0, x1, y1)
        self.rooms.append(label)
        self.rects.append((x0, y0, x1, y1, label))
        self.label(x0, y0, x1, y1, label)
        if door:
            self.door_on(x0, y0, x1, y1, *door)

    def label(self, x0, y0, x1, y1, text):
        y = (y0 + y1) // 2
        x = max(x0 + 1, (x0 + x1 - len(text)) // 2 + 1)
        self.labels.append((x, y, text[: x1 - x0 - 1]))

    def door_on(self, x0, y0, x1, y1, side, offset=None, width=2, ch=None):
        ch = ch or DOOR
        if side in "NS":
            y = y0 if side == "N" else y1
            start = x0 + (offset if offset is not None else (x1 - x0 - width) // 2 + 1)
            for x in range(start, start + width):
                self.put(x, y, ch)
        else:
            x = x0 if side == "W" else x1
            start = y0 + (offset if offset is not None else (y1 - y0 - width) // 2 + 1)
            for y in range(start, start + width):
                self.put(x, y, ch)

    def window(self, x0, y0, x1, y1):
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                if self.grid[y][x] == WALL:
                    self.put(x, y, WINDOW if not self.sealed else WALL)

    def fill(self, x0, y0, x1, y1, ch):
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                self.put(x, y, ch)


def door_boxes(grid):
    """Doors are connected groups of door cells, whatever their width: one box each."""
    depth, width = len(grid), len(grid[0])
    seen = set()
    boxes = []
    for y in range(depth):
        for x in range(width):
            if grid[y][x] not in DOORS or (x, y) in seen:
                continue
            cells = []
            stack = [(x, y)]
            while stack:
                cx, cy = stack.pop()
                if (cx, cy) in seen or not (0 <= cx < width and 0 <= cy < depth) or grid[cy][cx] not in DOORS:
                    continue
                seen.add((cx, cy))
                cells.append((cx, cy))
                stack += [(cx + 1, cy), (cx - 1, cy), (cx, cy + 1), (cx, cy - 1)]
            xs = [c[0] for c in cells]
            ys = [c[1] for c in cells]
            boxes.append((min(xs), min(ys), max(xs), max(ys)))
    return boxes


def rectangles(mask):
    """Covers the True cells of `mask` with rectangles: row runs merged down while they repeat."""
    open_runs = {}
    found = []
    for y in range(len(mask) + 1):
        row_runs = set()
        if y < len(mask):
            x = 0
            while x < len(mask[y]):
                if mask[y][x]:
                    end = x
                    while end < len(mask[y]) and mask[y][end]:
                        end += 1
                    row_runs.add((x, end - 1))
                    x = end
                else:
                    x += 1
        for run, top in list(open_runs.items()):
            if run not in row_runs:
                found.append((run[0], top, run[1], y - 1))
                del open_runs[run]
        for run in row_runs:
            open_runs.setdefault(run, y)
    return found


# ----- Blueprint ----------------------------------------------------------------------------

# Primitive names in assets/station/palette.json.
FLOOR, WALL_BLOCK, WINDOW_BLOCK, DOOR_BLOCK, HIDDEN_DOOR_BLOCK, SLOPE, SPAWN_POINT = (
    "Floor", "Wall", "Window", "Door", "HiddenDoor", "Slope", "SpawnPoint")
# Orientations (blueprint.fbs) turning a primitive's +X to +Y or -Y.
TO_POS_Y, TO_NEG_Y = "FacePosYUpPosZ", "FaceNegYUpPosZ"
MAX_SIZE_CELLS = 256
WALL_HEIGHT_CELLS = DECK_PITCH_CELLS - SLAB_CELLS
COUNTER_HEIGHT_CELLS = CELLS_PER_M
DOOR_WIDTH_CELLS, DOOR_HEIGHT_CELLS = 6, 10
WEDGE_CELLS = 4
FLIGHT_WEDGES = DECK_PITCH_CELLS // WEDGE_CELLS
FLIGHT_WIDTH_WEDGES = 2
SPAWN_CELLS = 4
SPAWN_OFFSETS = (-6, 6)  # a 2 x 2 group of markers around a room's centre

WALL_LIKE = WALL + WINDOW + COUNTER + DOORS
WALL_KIND, WINDOW_KIND, COUNTER_KIND, DOOR_KIND = "wall", "window", "counter", "door"
LINE_PRIMITIVES = {WALL_KIND: (WALL_BLOCK, WALL_HEIGHT_CELLS), WINDOW_KIND: (WINDOW_BLOCK, WALL_HEIGHT_CELLS),
                   COUNTER_KIND: (WALL_BLOCK, COUNTER_HEIGHT_CELLS)}


def wall_like(grid, x, y):
    return 0 <= y < len(grid) and 0 <= x < len(grid[y]) and grid[y][x] in WALL_LIKE


def kind_of(ch):
    if ch in DOORS:
        return DOOR_KIND
    return {WINDOW: WINDOW_KIND, COUNTER: COUNTER_KIND}.get(ch, WALL_KIND)


def block(primitive, cell, size, orientation=None):
    entry = {"primitive": primitive, "cell": dict(zip("xyz", cell)), "size": dict(zip("xyz", size))}
    if orientation:
        entry["orientation"] = orientation
    return entry


def line_blocks(primitive, axis, start, line, z, length, height):
    """A wall-like primitive along x (axis 0) or y (axis 1), cut to the size limit."""
    blocks = []
    for offset in range(0, length, MAX_SIZE_CELLS):
        piece = min(MAX_SIZE_CELLS, length - offset)
        cell = (start + offset, line, z) if axis == 0 else (line, start + offset, z)
        blocks.append(block(primitive, cell, (piece, 1, height), None if axis == 0 else TO_POS_Y))
    return blocks


def door_axes(grid, skipped=()):
    """Every door group in a wall with the axis the wall runs along; a door stands in a straight wall.

    `skipped`: door boxes that are hatches in the floor, not doors in a wall.
    """
    doors = []
    for x0, y0, x1, y1 in door_boxes(grid):
        if (x0, y0, x1, y1) in skipped:
            continue
        axes = []
        for axis in (0, 1):
            if (axis == 0 and y0 != y1) or (axis == 1 and x0 != x1):
                continue
            ends = ((x0 - 1, y0), (x1 + 1, y1)) if axis == 0 else ((x0, y0 - 1), (x1, y1 + 1))
            if all(wall_like(grid, *end) for end in ends):
                axes.append(axis)
        if len(axes) != 1:
            raise ValueError(f"the door at {x0},{y0} doesn't stand in one straight wall")
        doors.append(((x0, y0, x1, y1), axes[0]))
    return doors


def line_cells(grid, doors):
    """The cells of the deck's wall lines with their kind; a door's line is a door cell."""
    axis_of = {(x, y): axis for (x0, y0, x1, y1), axis in doors
               for y in range(y0, y1 + 1) for x in range(x0, x1 + 1)}
    cells = {}
    for y, row in enumerate(grid):
        for x, ch in enumerate(row):
            if ch not in WALL_LIKE:
                continue
            cells[(x * CELLS_PER_M, y * CELLS_PER_M)] = kind_of(ch)
            for axis, (nx, ny) in ((0, (x + 1, y)), (1, (x, y + 1))):
                if not wall_like(grid, nx, ny):
                    continue
                ends = [(x, y), (nx, ny)]
                along_door = [end for end in ends if axis_of.get(end) == axis]
                kinds = {kind_of(grid[ey][ex]) for ex, ey in ends if (ex, ey) not in axis_of}
                if along_door:
                    kind = DOOR_KIND
                elif kinds == {WINDOW_KIND}:
                    kind = WINDOW_KIND
                else:  # a wall meeting a door's line from the side stops at the door
                    kind = COUNTER_KIND if COUNTER_KIND in kinds else WALL_KIND
                for step in range(1, CELLS_PER_M):
                    cell = (x * CELLS_PER_M + step, y * CELLS_PER_M) if axis == 0 else \
                        (x * CELLS_PER_M, y * CELLS_PER_M + step)
                    cells[cell] = kind
    return cells


def line_runs(cells):
    """Splits the line cells into runs of one kind without overlaps: runs along x of at least two
    cells first, so a corner or junction belongs to them, then runs along y of the rest."""
    left = dict(cells)
    runs = []
    for axis in (0, 1):
        step = (1, 0) if axis == 0 else (0, 1)
        for x, y in sorted(left, key=lambda c: (c[1], c[0]) if axis == 0 else c):
            kind = left.get((x, y))
            if kind is None or left.get((x - step[0], y - step[1])) == kind:
                continue
            length = 1
            while left.get((x + step[0] * length, y + step[1] * length)) == kind:
                length += 1
            if axis == 0 and length < 2:
                continue
            for i in range(length):
                del left[(x + step[0] * i, y + step[1] * i)]
            runs.append((kind, axis, (x, y), length))
    return runs


def door_blocks(grid, doors, z):
    """Each door group: one door in the middle of its opening, walls beside it and over it."""
    blocks = []
    for (x0, y0, x1, y1), axis in doors:
        chars = {grid[y][x] for y in range(y0, y1 + 1) for x in range(x0, x1 + 1)}
        if len(chars) != 1:
            raise ValueError(f"the door at {x0},{y0} mixes kinds {sorted(chars)}")
        primitive = HIDDEN_DOOR_BLOCK if chars == {HIDDEN} else DOOR_BLOCK
        first, last = (x0, x1) if axis == 0 else (y0, y1)
        line = (y0 if axis == 0 else x0) * CELLS_PER_M
        start = (first - 1) * CELLS_PER_M + 1
        length = (last + 1) * CELLS_PER_M - start
        before = (length - DOOR_WIDTH_CELLS) // 2
        after = length - DOOR_WIDTH_CELLS - before
        door_start = start + before
        door_cell = (door_start, line, z) if axis == 0 else (line, door_start, z)
        blocks.append(block(primitive, door_cell, (DOOR_WIDTH_CELLS, 1, DOOR_HEIGHT_CELLS),
                            None if axis == 0 else TO_POS_Y))
        if before:
            blocks += line_blocks(WALL_BLOCK, axis, start, line, z, before, WALL_HEIGHT_CELLS)
        if after:
            blocks += line_blocks(WALL_BLOCK, axis, door_start + DOOR_WIDTH_CELLS, line, z, after, WALL_HEIGHT_CELLS)
        blocks += line_blocks(WALL_BLOCK, axis, door_start, line, z + DOOR_HEIGHT_CELLS, DOOR_WIDTH_CELLS,
                              WALL_HEIGHT_CELLS - DOOR_HEIGHT_CELLS)
    return blocks


def wall_blocks(grid, z, skipped_doors=()):
    """Every wall, window, counter and door of a deck plan."""
    doors = door_axes(grid, skipped_doors)
    blocks = []
    for kind, axis, (x, y), length in line_runs(line_cells(grid, doors)):
        if kind == DOOR_KIND:
            continue
        primitive, height = LINE_PRIMITIVES[kind]
        start, line = (x, y) if axis == 0 else (y, x)
        blocks += line_blocks(primitive, axis, start, line, z, length, height)
    return blocks + door_blocks(grid, doors, z)


def deck_floor_z(deck):
    return (deck.number - 1) * DECK_PITCH_CELLS + SLAB_CELLS


def stairs_open(decks, index):
    """Whether the slab under deck `index` (counted from 0) opens over the flights below it."""
    return 0 < index < len(decks) and not (decks[index - 1].sealed and not decks[index].sealed)


# A stairwell is its walls' (x0, y0, x1, y1) and the way its flight climbs along y: away from
# the door, up a 2 m wide flight over the first ramp column, to a landing by the far wall.

def flight(stairwell):
    """A stairwell's flight: its x, its y from bottom to top and the slopes' orientation."""
    (x0, y0, x1, y1), climb = stairwell
    door_wall = (y1 if climb < 0 else y0) * CELLS_PER_M
    bottom = door_wall + climb * FLIGHT_WEDGES * WEDGE_CELLS
    return (x0 + 1) * CELLS_PER_M, bottom, climb


def flight_footprint(stairwell):
    x, bottom, climb = flight(stairwell)
    top = bottom + climb * FLIGHT_WEDGES * WEDGE_CELLS
    return x, min(bottom, top), x + FLIGHT_WIDTH_WEDGES * WEDGE_CELLS - 1, max(bottom, top) - 1


def flight_blocks(stairwells, z):
    blocks = []
    for stairwell in stairwells:
        x, bottom, climb = flight(stairwell)
        for step in range(FLIGHT_WEDGES):
            y = bottom + step * WEDGE_CELLS if climb > 0 else bottom - (step + 1) * WEDGE_CELLS
            for column in range(FLIGHT_WIDTH_WEDGES):
                blocks.append(block(SLOPE, (x + column * WEDGE_CELLS, y, z + step * WEDGE_CELLS),
                                    (WEDGE_CELLS,) * 3, TO_POS_Y if climb > 0 else TO_NEG_Y))
    return blocks


def clear_flights(mask, stairwells):
    for stairwell in stairwells:
        fx0, fy0, fx1, fy1 = flight_footprint(stairwell)
        for y in range(fy0, fy1 + 1):
            for x in range(fx0, fx1 + 1):
                mask[y][x] = False


def mask_blocks(mask, z, primitive=FLOOR):
    """A one-cell-thick slab of `primitive` over the True cells of `mask`, cut to the size limit."""
    blocks = []
    for x0, y0, x1, y1 in rectangles(mask):
        for px in range(x0, x1 + 1, MAX_SIZE_CELLS):
            for py in range(y0, y1 + 1, MAX_SIZE_CELLS):
                size = (min(MAX_SIZE_CELLS, x1 + 1 - px), min(MAX_SIZE_CELLS, y1 + 1 - py), SLAB_CELLS)
                blocks.append(block(primitive, (px, py, z), size))
    return blocks


def spawn_blocks(deck, z):
    blocks = []
    for x0, y0, x1, y1, label in deck.rects:
        if label not in deck.spawn_rooms:
            continue
        centre_x, centre_y = (x0 + x1) * CELLS_PER_M // 2, (y0 + y1) * CELLS_PER_M // 2
        for dy in SPAWN_OFFSETS:
            for dx in SPAWN_OFFSETS:
                cell = (centre_x + dx - SPAWN_CELLS // 2, centre_y + dy - SPAWN_CELLS // 2, z)
                blocks.append(block(SPAWN_POINT, cell, (SPAWN_CELLS, SPAWN_CELLS, 1)))
    return blocks


def write_blueprint(path, blocks):
    """One block per line, in FlatBuffers JSON (src/lib_z13/schemas/fbs/blueprint.fbs)."""
    lines = [json.dumps(entry, separators=(", ", ": ")) for entry in blocks]
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("{\n  \"blocks\": [\n    " + ",\n    ".join(lines) + "\n  ]\n}\n", encoding="ascii")


def block_kinds(blocks):
    kinds = {}
    for entry in blocks:
        kinds[entry["primitive"]] = kinds.get(entry["primitive"], 0) + 1
    return ", ".join(f"{name} {count}" for name, count in kinds.items())


def ruler(width):
    return ("    " + "".join(f"{x:<10}" for x in range(0, width, 10))).rstrip()


def render_plan(deck, header):
    plan = [row[:] for row in deck.grid]
    for x, y, text in deck.labels:
        plan[y][x:x + len(text)] = list(text)
    body = [f"{y:3} " + "".join(row).rstrip() for y, row in enumerate(plan)]
    return "\n".join(header + ["", ruler(deck.width)] + body) + "\n"
