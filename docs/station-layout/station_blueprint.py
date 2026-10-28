"""Shared by the station generators: decks drawn as text and their translation into blueprint blocks.

One character is 1 m (4 grid cells). A wall of the plan becomes blocks one cell thick on the grid
line through its characters' first cells: x m on the plan is cell 4x.
"""

import itertools
import json

CELLS_PER_M = 4
DECK_PITCH_M = 4  # floor slab + 3.5 m room + ceiling slab
DECK_PITCH_CELLS = DECK_PITCH_M * CELLS_PER_M
ROOM_HEIGHT_CELLS = 14
SLAB_CELLS = 1

WALL, WINDOW, DOOR, SEALED, RAMP, SHAFT, DANCE, COUNTER = "#", "=", "D", "X", "/", "L", ":", "_"
HIDDEN, TUNNEL, UNDER_LEDGE, LADDER = "S", ".", "~", "H"
MASS = "@"  # solid fill that is no wall line: free_areas and lamps keep out of it
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
BLOCKED = WALL_LIKE + MASS
WALL_KIND, WINDOW_KIND, COUNTER_KIND, DOOR_KIND = "wall", "window", "counter", "door"
# A height of None is the deck's wall height.
LINE_PRIMITIVES = {WALL_KIND: (WALL_BLOCK, None), WINDOW_KIND: (WINDOW_BLOCK, None),
                   COUNTER_KIND: (WALL_BLOCK, COUNTER_HEIGHT_CELLS)}


def wall_like(grid, x, y):
    return 0 <= y < len(grid) and 0 <= x < len(grid[y]) and grid[y][x] in WALL_LIKE


def kind_of(ch):
    if ch in DOORS:
        return DOOR_KIND
    return {WINDOW: WINDOW_KIND, COUNTER: COUNTER_KIND}.get(ch, WALL_KIND)


def block(primitive, cell, size, orientation=None, slant=0):
    entry = {"primitive": primitive, "cell": dict(zip("xyz", cell)), "size": dict(zip("xyz", size))}
    if orientation:
        entry["orientation"] = orientation
    if slant:
        entry["slant"] = slant
    return entry


# The wedge's limits in assets/station/palette.json (Slope).
WEDGE_MAX_SIZE = (16, 16, 4)
WEDGE_MAX_RUN_PER_RISE = 4
# In units of the size: the run along x, the rise along z, the width along y.
WEDGE_VERTICES = ((0, 0, 0), (1, 0, 0), (1, 0, 1), (0, 1, 0), (1, 1, 0), (1, 1, 1))


def _orientations():
    """(name, columns) of the 24 orientations, the identity first; the columns are the world
    vectors of a block's x, y and z (primitives/placement.cpp)."""
    found = []
    for face_axis in range(3):
        for face_sign in (1, -1):
            for up_axis in range(3):
                for up_sign in (1, -1):
                    if up_axis == face_axis:
                        continue
                    face, up = [0, 0, 0], [0, 0, 0]
                    face[face_axis], up[up_axis] = face_sign, up_sign
                    side = [up[1] * face[2] - up[2] * face[1], up[2] * face[0] - up[0] * face[2],
                            up[0] * face[1] - up[1] * face[0]]
                    name = (f"Face{'Pos' if face_sign > 0 else 'Neg'}{'XYZ'[face_axis]}"
                            f"Up{'Pos' if up_sign > 0 else 'Neg'}{'XYZ'[up_axis]}")
                    found.append((name, (face, side, up)))
    found.sort(key=lambda entry: entry[0] != "FacePosXUpPosZ")
    return found


ORIENTATIONS = _orientations()


def wedge_block(vertices):
    """The Slope block whose solid has exactly these six world vertices (cell corners): its size,
    its orientation and the corner of its box."""
    wanted = {tuple(2 * c for c in vertex) for vertex in vertices}
    low = [min(vertex[i] for vertex in vertices) for i in range(3)]
    extent = [max(vertex[i] for vertex in vertices) - low[i] for i in range(3)]
    for name, columns in ORIENTATIONS:
        size = [extent[next(row for row in range(3) if columns[i][row])] for i in range(3)]
        if not (size[2] <= size[0] <= WEDGE_MAX_RUN_PER_RISE * size[2]
                and all(size[i] <= WEDGE_MAX_SIZE[i] for i in range(3))):
            continue
        # Doubled coordinates keep the box's middle on an integer.
        placed = {tuple(2 * low[row] + extent[row] + sum(columns[i][row] * (2 * vertex[i] * size[i] - size[i])
                                                          for i in range(3)) for row in range(3))
                  for vertex in WEDGE_VERTICES}
        if placed == wanted:
            return block(SLOPE, low, size, None if name == "FacePosXUpPosZ" else name)
    raise ValueError(f"no wedge has the vertices {sorted(vertices)}")


# ----- Oblique walls -------------------------------------------------------------------------

PANEL = "Panel"
PANEL_MAX_SIZE_CELLS = 256


def plan_axes(stick_axis, span):
    """(major, minor): the axes of the stick axis's plane that carry the panel's run (the longer of
    `span`, the lower on a tie) and its rise (primitives/wall_panel.h, PlanAxesOf)."""
    first = 1 if stick_axis == 0 else 0
    second = 1 if stick_axis == 2 else 2
    major = first if abs(span[first]) >= abs(span[second]) else second
    return major, (second if major == first else first)


def min_wall_thickness(node_a, node_b, stick_axis=2):
    """The thinnest panel between the nodes that holds air back (MinWallThickness): `t * run >= run + rise`."""
    span = [b - a for a, b in zip(node_a, node_b)]
    span[stick_axis] = 0
    major, minor = plan_axes(stick_axis, span)
    run, rise = abs(span[major]), abs(span[minor])
    if run == 0:
        raise ValueError("the wall's nodes coincide in its plane")
    return (run + rise + run - 1) // run


def wall_panel_block(node_a, node_b, thickness, stick_length, stick_axis=2, primitive=PANEL):
    """The Panel block whose prism runs from node a to node b (b's offset along the stick axis dropped) over
    the stick, `stick_length` cells long (signed), thickened by |thickness| cells along the lesser axis of the
    plane to the side of its sign (primitives/wall_panel.cpp, WallPanelBlock, for sticks of one length). The
    ends are cut by the grid. Raises ValueError for what the C++ function refuses."""
    offset = [b - a for a, b in zip(node_a, node_b)]
    offset[stick_axis] = 0
    if not any(offset):
        raise ValueError("the wall's nodes coincide in its plane")
    if stick_length == 0 or thickness == 0:
        raise ValueError("the wall has no stick length or thickness")
    major, minor = plan_axes(stick_axis, offset)
    run, rise, depth = abs(offset[major]), abs(offset[minor]), abs(thickness)
    size = (run, abs(stick_length), rise + depth)
    if depth * run < run + rise or any(extent > PANEL_MAX_SIZE_CELLS for extent in size):
        raise ValueError(f"a panel of run {run}, rise {rise} and thickness {depth} isn't allowed")

    thicken = [0, 0, 0]
    thicken[minor] = (1 if thickness > 0 else -1) * depth
    along = [0, 0, 0]
    along[stick_axis] = stick_length
    high = [a + o for a, o in zip(node_a, offset)]
    bases = [list(node_a), [a + t for a, t in zip(node_a, thicken)], high, [h + t for h, t in zip(high, thicken)]]
    target = {tuple(base) for base in bases} | {tuple(b + a for b, a in zip(base, along)) for base in bases}
    lowest = tuple(min(point[i] for point in target) for i in range(3))
    section = ((0, 0), (run, rise), (run, size[2]), (0, depth))
    corners = [(x, y, z) for y in (0, size[1]) for x, z in section]
    for name, columns in ORIENTATIONS:
        rotate = lambda p: tuple(sum(columns[i][row] * p[i] for i in range(3)) for row in range(3))
        # PoseOf: the origin keeps the rotated box's lowest corner on the block's cell.
        origin = tuple(lowest[row] - min(rotate(size)[row], 0) for row in range(3))
        if {tuple(o + r for o, r in zip(origin, rotate(corner))) for corner in corners} == target:
            return block(primitive, lowest, size, None if name == "FacePosXUpPosZ" else name, rise)
    raise ValueError("no turn of the panel fits the wall")


# ----- Halls with slanted corners ------------------------------------------------------------

def corner_uv(name, outer, cell_x, cell_y):
    """Distances of a cell's middle (doubled) from the box corner `name` along x and y."""
    x0, y0, x1, y1 = outer
    u = (2 * cell_x + 1 - 2 * x0) if name[1] == "W" else (2 * x1 - 2 * cell_x - 1)
    v = (2 * cell_y + 1 - 2 * y0) if name[0] == "N" else (2 * y1 - 2 * cell_y - 1)
    return u, v


def fill_corners(deck, corners):
    """Marks the characters a hall's filled corner triangles reach as solid fill. `corners`: (name, the
    hall's outer box in cells, steps) as in corner_blocks."""
    for name, outer, steps in corners:
        across, down = sum(step[0] for step in steps), sum(step[1] for step in steps)
        for cy in range(outer[1] // CELLS_PER_M, outer[3] // CELLS_PER_M + 1):
            for cx in range(outer[0] // CELLS_PER_M, outer[2] // CELLS_PER_M + 1):
                if deck.grid[cy][cx] != " ":
                    continue
                for dy, dx in itertools.product(range(CELLS_PER_M), repeat=2):
                    u, v = corner_uv(name, outer, cx * CELLS_PER_M + dx, cy * CELLS_PER_M + dy)
                    if u > 0 and v > 0 and u * down + v * across < 2 * across * down:
                        deck.grid[cy][cx] = MASS
                        break


def corner_blocks(name, outer, steps, z, wall_height, rows=WALL_BLOCK):
    """The filled triangle of one corner of a hall, as rows of `rows` blocks and one wedge prism per step. Each
    step is a pair of cells along x and y; they add up to the triangle's legs."""
    x0, y0, x1, y1 = outer
    across = sum(step[0] for step in steps)

    def at(u, v):
        return (x0 + u if name[1] == "W" else x1 - u, y0 + v if name[0] == "N" else y1 - v)

    blocks = []
    u_start, v_low = across, 0
    for step_x, step_y in steps:
        v_high = v_low + step_y
        width = u_start - step_x
        if width > 0:
            (xa, ya), (xb, yb) = at(0, v_low), at(width, v_high)
            for y in range(min(ya, yb), max(ya, yb)):
                blocks.append(block(rows, (min(xa, xb), y, z), (width, 1, wall_height)))
        triangle = [(width, v_low), (u_start, v_low), (width, v_high)]
        blocks.append(wedge_block([(*at(u, v), height) for u, v in triangle for height in (z, z + wall_height)]))
        u_start, v_low = width, v_high
    return blocks


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


def door_blocks(grid, doors, z, wall_height=WALL_HEIGHT_CELLS):
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
            blocks += line_blocks(WALL_BLOCK, axis, start, line, z, before, wall_height)
        if after:
            blocks += line_blocks(WALL_BLOCK, axis, door_start + DOOR_WIDTH_CELLS, line, z, after, wall_height)
        blocks += line_blocks(WALL_BLOCK, axis, door_start, line, z + DOOR_HEIGHT_CELLS, DOOR_WIDTH_CELLS,
                              wall_height - DOOR_HEIGHT_CELLS)
    return blocks


def wall_blocks(grid, z, skipped_doors=(), wall_height=WALL_HEIGHT_CELLS):
    """Every wall, window, counter and door of a deck plan."""
    doors = door_axes(grid, skipped_doors)
    blocks = []
    for kind, axis, (x, y), length in line_runs(line_cells(grid, doors)):
        if kind == DOOR_KIND:
            continue
        primitive, height = LINE_PRIMITIVES[kind]
        height = height or wall_height
        start, line = (x, y) if axis == 0 else (y, x)
        blocks += line_blocks(primitive, axis, start, line, z, length, height)
    return blocks + door_blocks(grid, doors, z, wall_height)


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


# ----- Lamps -------------------------------------------------------------------------------

LAMP = "Lamp"
LAMP_CELLS = 2
# One lamp per this many meters of a free area along each axis, at least one per area.
LAMP_SPACING_M = 6
AXES = {"X": 0, "Y": 1, "Z": 2}


def axis_vector(name):
    """'PosX', 'NegZ', ... as a unit vector."""
    vector = [0, 0, 0]
    vector[AXES[name[3]]] = 1 if name.startswith("Pos") else -1
    return vector


def world_axes(entry):
    """The world axis each of the block's own x, y and z turns into."""
    orientation = entry.get("orientation")
    if not orientation:
        return [0, 1, 2]
    face, up = orientation[len("Face"):].split("Up")
    x, z = axis_vector(face), axis_vector(up)
    y = [z[1] * x[2] - z[2] * x[1], z[2] * x[0] - z[0] * x[2], z[0] * x[1] - z[1] * x[0]]
    return [next(row for row in range(3) if column[row]) for column in (x, y, z)]


def occupied_box(entry):
    """The cells [min, max) a blueprint block occupies (primitives/placement.h)."""
    low = [entry["cell"][axis] for axis in "xyz"]
    high = list(low)
    for axis, world in zip("xyz", world_axes(entry)):
        high[world] += entry["size"][axis]
    return low, high


def overlaps(box, low, high):
    return all(box[0][i] < high[i] and low[i] < box[1][i] for i in range(3))


def free_areas(grid):
    """The deck's free areas at 1 m, walls and doors apart; the one around the hull is left out."""
    depth, width = len(grid), len(grid[0])
    seen = set()
    areas = []
    for y in range(depth):
        for x in range(width):
            if grid[y][x] in BLOCKED or (x, y) in seen:
                continue
            cells, stack, outside = set(), [(x, y)], False
            while stack:
                cx, cy = stack.pop()
                if (cx, cy) in seen or grid[cy][cx] in BLOCKED:
                    continue
                seen.add((cx, cy))
                cells.add((cx, cy))
                outside = outside or cx in (0, width - 1) or cy in (0, depth - 1)
                stack += [(nx, ny) for nx, ny in ((cx + 1, cy), (cx - 1, cy), (cx, cy + 1), (cx, cy - 1))
                          if 0 <= nx < width and 0 <= ny < depth]
            if not outside:
                areas.append(cells)
    return areas


def lamp_spots(area):
    """A lattice of LAMP_SPACING_M over the area's bounds, kept where it falls inside the area."""
    xs = [x for x, _ in area]
    ys = [y for _, y in area]
    x0, y0, x1, y1 = min(xs), min(ys), max(xs), max(ys)
    columns = max(1, round((x1 - x0 + 1) / LAMP_SPACING_M))
    rows = max(1, round((y1 - y0 + 1) / LAMP_SPACING_M))
    spots = [(x0 + (x1 - x0 + 1) * (2 * i + 1) // (2 * columns), y0 + (y1 - y0 + 1) * (2 * j + 1) // (2 * rows))
             for j in range(rows) for i in range(columns)]
    inside = [spot for spot in spots if spot in area]
    if inside:
        return inside
    centre = ((x0 + x1) / 2, (y0 + y1) / 2)
    return [min(sorted(area), key=lambda c: (c[0] - centre[0]) ** 2 + (c[1] - centre[1]) ** 2)]


def lamp_blocks(deck, z, blocks, wall_height=WALL_HEIGHT_CELLS):
    """Lamps under the ceiling of every free area of the deck, on a lattice; a lamp needs a block
    above it to hang from and none where it hangs. A spot under an open slab moves to the nearest
    cell of its area within half a lattice step that has a ceiling, such as a gallery's edge."""
    top = z + wall_height - 1
    near = [box for box in map(occupied_box, blocks) if box[0][2] < top + 2 and top < box[1][2]]
    reach = LAMP_SPACING_M // 2

    def fits(x, y):
        low = [x * CELLS_PER_M + 1, y * CELLS_PER_M + 1, top]
        high = [low[0] + LAMP_CELLS, low[1] + LAMP_CELLS, top + 1]
        hung = all(any(overlaps(box, [cx, cy, top + 1], [cx + 1, cy + 1, top + 2]) for box in near)
                   for cx in range(low[0], high[0]) for cy in range(low[1], high[1]))
        return low if hung and not any(overlaps(box, low, high) for box in near) else None

    lamps = []
    for area in free_areas(deck.grid):
        for x, y in lamp_spots(area):
            window = ((x + dx, y + dy) for dx in range(-reach, reach + 1) for dy in range(-reach, reach + 1))
            around = sorted((c for c in window if c in area), key=lambda c: ((c[0] - x) ** 2 + (c[1] - y) ** 2, c))
            low = next(filter(None, (fits(cx, cy) for cx, cy in around)), None)
            if low:
                lamp = block(LAMP, low, (LAMP_CELLS, LAMP_CELLS, 1))
                lamps.append(lamp)
                near.append(occupied_box(lamp))
    return lamps


def wall_opening(offset, width, height, kind="Door", sill=0):
    """An opening of a wall segment (BlueprintWallOpening): `offset` and `width` in the wall's steps (its plan
    offset over the gcd of its parts) from node a, `sill` and `height` in cells up the stick. The wall keeps at
    least a step at each end and a cell above the opening, so a wall of one step takes none."""
    return {"kind": kind, "offset": offset, "width": width, "sill": sill, "height": height}


def wall_segment(node_a, node_b, thickness, stick_length, stick_axis=2, openings=()):
    """A wall segment of a blueprint (BlueprintWall): the game derives its panel and stitches it along a seam to
    the segments that meet it at a node. Nodes at one cell are one node."""
    axis = "XYZ"[stick_axis]
    wall = {"node_a": dict(zip("xyz", node_a)), "node_b": dict(zip("xyz", node_b)), "axis_a": axis,
            "length_a": stick_length, "axis_b": axis, "length_b": stick_length, "thickness": thickness}
    if openings:
        wall["openings"] = list(openings)
    return wall


# building_tuning.fbs, max_block_cells: a longer block would reach past the sector next to its own.
MAX_BLOCK_CELLS = 64


def split_long(entry):
    """The block cut along each of its axes into pieces no longer than MAX_BLOCK_CELLS. Its box starts at its cell
    whatever the orientation (occupied_box), so a piece moves along the world axis its own axis turns into."""
    pieces = [entry]
    for axis, world in zip("xyz", world_axes(entry)):
        length = entry["size"][axis]
        cut = []
        for piece in pieces:
            for start in range(0, length, MAX_BLOCK_CELLS):
                cell = {**piece["cell"], "xyz"[world]: piece["cell"]["xyz"[world]] + start}
                cut.append({**piece, "cell": cell, "size": {**piece["size"], axis: min(MAX_BLOCK_CELLS, length - start)}})
        pieces = cut
    return pieces


def write_blueprint(path, blocks, walls=()):
    """One block per line, in FlatBuffers JSON (src/lib_z13/primitives/fbs/blueprint.fbs), then the wall segments;
    returns the blocks written, the long ones cut."""
    blocks = [piece for entry in blocks for piece in split_long(entry)]
    lines = [json.dumps(entry, separators=(", ", ": ")) for entry in blocks]
    text = "{\n  \"blocks\": [\n    " + ",\n    ".join(lines) + "\n  ]"
    if walls:
        wall_lines = [json.dumps(entry, separators=(", ", ": ")) for entry in walls]
        text += ",\n  \"walls\": [\n    " + ",\n    ".join(wall_lines) + "\n  ]"
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text + "\n}\n", encoding="ascii", newline="\n")
    return blocks


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
