#!/usr/bin/env python3
"""Draws the test station's decks as text, estimates its load and writes its blueprint.

One character is 1 m (4 grid cells). Writes the deck plans, the section and the stats next to
itself, and the blueprint the game builds the test station from into assets/station/blueprints/.
"""

import json
from pathlib import Path

# ----- Output -------------------------------------------------------------------------------
# ASCII only: the files are read on systems whose consoles may not show anything else.

OUT = Path(__file__).parent
DECK_FILE = "deck-{number}.txt"
DECK_FILE_GLOB = "deck-*.txt"
SECTION_FILE = "section.txt"
STATS_FILE = "stats.txt"
BLUEPRINT_FILE = OUT.parent.parent / "assets" / "station" / "blueprints" / "test.json"

DECK_HEADER = "Deck {number}: {name}. Floor at {floor} m."
DECK_LEGEND = (
    "1 char = 1 m (4 cells). # wall, = window, D door, X welded, / stairs, L lift, : dance floor, _ counter.",
    ". hidden tech tunnel, ~ tunnel under a window ledge, S hidden door, H ladder between decks.",
    "AL - airlocks at the spine's ends, AIRLOCK-N/S - EVA airlocks by the stairs, SW - switchboard, TECH - tech rooms.",
)
SEALED_NOTE = "Sealed deck: its entrances are welded (X); the plan is from the original blueprints, the bays' purpose unknown to the crew."
SECTION_LEGEND = (
    "Cut along the spine corridor (y = 24 m), seen from the south. 1 char = 1 m across, 1 row = 1 m of height.",
    "/ \\ - stair flights (x 6-16 and 80-90), L - lift shaft (x 46-50), H - tunnel ladders (x 2-3 and 93-94),",
    "= - glass dome over the bridge, X - hatches welded over the sealed decks and the lift doors on them.",
)
STATS_TITLE = "Test station load estimate (written by generate_layout.py)."
STATS_DECK = "Deck {number} ({name}): berths {berths}, rooms {rooms}, doors {doors}, area {area} m2, air cells {air}"
STATS_TOTAL = "Single cabins: {berths}. Rooms {rooms}, doors {doors}."
STATS_AIR = "Air cells (0.25 m grid): {air}."
STATS_BLOCKS = "Blueprint blocks ({file}): {total} - {kinds}."


class Name:
    """Deck names."""
    SEALED = "Sealed deck {letter}"
    ENGINEERING = "Engineering deck"
    SECURITY = "Security & storage deck"
    HABITAT = "Habitat deck {letter}"
    CAMPUS = "Campus deck"
    SCIENCE = "Science deck"
    COMMAND = "Command deck"


class Room:
    """Labels drawn on the plans; a label longer than its room is cut."""
    TECH = "TECH"
    SPINE = "SPINE"
    END_AIRLOCK = "AL"
    SWITCHBOARD = "SW"
    NORTH_AIRLOCK = "AIRLOCK-N"
    SOUTH_AIRLOCK = "AIRLOCK-S"
    STAIRS = "STAIRS"
    LIFT = "LIFT"
    TUNNEL = "TUNNEL"
    CABINS = "CABINS"
    UNKNOWN_BAY = "BAY ?"
    UNKNOWN = "?"
    REACTOR = "REACTOR"
    POWER = "POWER"
    ATMOSPHERE = "ATMOS-{n}"
    GRAVITY = "GRAVGEN"
    WATER = "WATER"
    WORKSHOP = "WORKSHOP"
    SPARES = "SPARES"
    SECURITY = "SECURITY"
    ARMORY = "ARMORY"
    BARRACKS = "GUARDS"
    BRIG = "BRIG"
    CELL = "C"
    CARGO_AIRLOCK = "CARGO AIRLOCK"
    STORAGE = "STORE-{n}"
    WASHROOM = "WC"
    SHOWERS = "SHOWER-{n}"
    LAUNDRY = "LAUNDRY"
    LOUNGE = "LOUNGE"
    GAMES = "GAMES"
    KITCHEN = "KITCHEN"
    MESS = "MESS HALL"
    DANCE_FLOOR = " DANCE FLOOR "
    BAR = "BAR"
    MED_INTAKE = "MED-INTAKE"
    MED_SURGERY = "MED-SURGERY"
    MED_ISOLATION = "MED-ISOLATE"
    GYM = "GYM"
    LAB_BIO = "LAB-BIO"
    LAB_CHEM = "LAB-CHEM"
    LAB_PHYS = "LAB-PHYS"
    LAB_MED = "LAB-MED"
    OBSERVATORY = "OBSERVATORY"
    HYDROPONICS = "HYDROPONICS-{n}"
    BRIDGE = "BRIDGE"
    NAVIGATION = "NAVIGATION"
    COMMS = "COMMS"
    BRIEFING = "BRIEFING"
    CAPTAIN = "CAPTAIN"
    BRIDGE_GUARD = "BR.GUARD"


# Rooms staff never need but maintenance does: the hidden tunnels open into them.
SERVICE_ROOMS = ("SW", Room.TECH, "ATMOS", Room.REACTOR, Room.POWER, Room.GRAVITY, Room.WATER, Room.COMMS, "BAY")

# ----- Geometry -----------------------------------------------------------------------------

WIDTH, DEPTH = 97, 49  # hull 96 x 48 m
CELLS_PER_M = 4
DECK_PITCH_M = 4  # floor slab + 3.5 m room + ceiling slab
ROOM_HEIGHT_CELLS = 14
DECK_PITCH_CELLS = DECK_PITCH_M * CELLS_PER_M
SLAB_CELLS = 1
CABIN_WIDTH_M = 3  # single cabins, 3 x 4 m

WALL, WINDOW, DOOR, SEALED, RAMP, SHAFT, DANCE, COUNTER = "#", "=", "D", "X", "/", "L", ":", "_"
HIDDEN, TUNNEL, UNDER_LEDGE, LADDER = "S", ".", "~", "H"
DOORS = DOOR + SEALED + HIDDEN
HEADER_RULER = ("    " + "".join(f"{x:<10}" for x in range(0, WIDTH, 10))).rstrip()


class Deck:
    def __init__(self, number, name, sealed=False):
        self.number = number
        self.name = name
        self.sealed = sealed
        self.grid = [[" "] * WIDTH for _ in range(DEPTH)]
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


def common_core(deck):
    """Hull, side service rooms, spine corridor and its end airlocks, shared by every deck."""
    deck.rect(0, 0, WIDTH - 1, DEPTH - 1)
    for x0, x1 in ((0, 6), (90, 96)):
        deck.room(x0, 0, x1, 20, Room.TECH)
        deck.room(x0, 28, x1, 48, Room.TECH)
    deck.rect(6, 22, 90, 26)
    deck.rooms.append(Room.SPINE)
    for x0, x1, outer, inner in ((0, 6, "W", "E"), (90, 96, "E", "W")):
        deck.room(x0, 20, x1, 28, Room.END_AIRLOCK, door=(outer, None, 3, deck.entry))
        deck.door_on(x0, 20, x1, 28, inner, None, 3)


def stairs_and_shaft(deck):
    """Drawn last, over the rooms: they cut through every deck."""
    deck.rect(6, 12, 16, 22)
    deck.fill(7, 13, 8, 21, RAMP)
    deck.fill(14, 13, 15, 21, RAMP)
    deck.door_on(6, 12, 16, 22, "S", 4, ch=deck.entry)
    deck.rect(80, 26, 90, 36)
    deck.fill(81, 27, 82, 35, RAMP)
    deck.fill(88, 27, 89, 35, RAMP)
    deck.door_on(80, 26, 90, 36, "N", 4, ch=deck.entry)
    deck.rect(46, 17, 50, 22)
    deck.fill(47, 18, 49, 21, SHAFT)
    deck.door_on(46, 17, 50, 22, "S", 1, 3, deck.entry)
    deck.room(46, 0, 50, 17, Room.SWITCHBOARD, door=("W", None))
    deck.room(6, 0, 16, 12, Room.NORTH_AIRLOCK, door=("N", 4, 2, deck.entry))
    deck.door_on(6, 0, 16, 12, "S", 4)
    deck.room(80, 36, 90, 48, Room.SOUTH_AIRLOCK, door=("S", 4, 2, deck.entry))
    deck.door_on(80, 36, 90, 48, "N", 4)
    deck.stairwells = 2
    deck.rooms += [Room.STAIRS, Room.STAIRS, Room.LIFT, Room.TUNNEL, Room.TUNNEL]
    service_tunnels(deck)


# Cabin rows and the corridors between them, per band: (y0, y1, door side) and corridor (y0, y1).
CABIN_ROWS = {
    "N": ([(2, 6, "S"), (8, 12, "N"), (12, 16, "S"), (18, 22, "N")], [(6, 8), (16, 18)]),
    "S": ([(26, 30, "S"), (32, 36, "N"), (36, 40, "S"), (42, 46, "N")], [(30, 32), (40, 42)]),
}


# Officers' cabins, 5 x 8 and 5 x 9 m, on one corridor south of the spine.
OFFICER_ROWS = ([(26, 34, "S"), (37, 46, "N")], [(34, 37)])


def cabin_wing(deck, x0, x1, band, passages, shared=(), layout=None, width=CABIN_WIDTH_M):
    """Single cabins along corridors, joined to the spine by 2 m passages at `passages` (x of the west wall).

    `shared`: (x0, x1, row index, label) rooms that replace cabins in one row.
    """
    rows, corridors = layout or CABIN_ROWS[band]
    door_width = 1 if width <= CABIN_WIDTH_M else 2
    for cy0, cy1 in corridors:
        deck.rect(x0, cy0, x1, cy1)
    blocked = [(px, px + 3) for px in passages]
    for index, (y0, y1, side) in enumerate(rows):
        taken = blocked + [(sx0, sx1) for sx0, sx1, row, _ in shared if row == index]
        for sx0, sx1, row, label in shared:
            if row == index:
                deck.room(sx0, y0, sx1, y1, label, door=(side, None))
        cx = x0
        while cx + width <= x1:
            clash = next((end for start, end in taken if cx < end and cx + width > start), None)
            if clash is not None:
                cx = clash
                continue
            deck.room(cx, y0, cx + width, y1, "", door=(side, 1, door_width))
            deck.berths += 1
            cx += width
    spine_wall = 22 if band == "N" else 26
    far = min(cy0 for cy0, _ in corridors) + 1 if band == "N" else max(cy1 for _, cy1 in corridors) - 1
    for px in passages:
        lo, hi = sorted((far, spine_wall))
        deck.fill(px + 1, lo, px + 2, hi, " ")
        for y in range(lo, hi + 1):
            inside_corridor = any(cy0 < y < cy1 for cy0, cy1 in corridors)
            for x in (px, px + 3):
                deck.put(x, y, " " if inside_corridor else WALL)
        deck.door_on(px, spine_wall, px + 3, spine_wall, "N", 1, 2)
    deck.rooms.append(Room.CABINS)


# Ladders between decks, in the tech rooms at both ends of the station.
LADDER_HATCHES = ((2, 3, 3, 4), (93, 3, 94, 4), (2, 44, 3, 45), (93, 44, 94, 45))


def service_tunnels(deck):
    """1 m tunnels along the north and south hull, behind a wall, with hidden doors into service rooms.

    Under a hull window or gate the tunnel runs below a 1 m ledge instead, so the view stays open.
    """
    for hull, inner, x0, x1 in ((0, 2, 16, 90), (DEPTH - 1, DEPTH - 3, 6, 80)):
        tunnel = (hull + inner) // 2
        for x in range(x0 + 1, x1):
            if deck.grid[hull][x] in WINDOW + DOORS:
                deck.put(x, tunnel, UNDER_LEDGE)
                deck.put(x, inner, " ")
            else:
                deck.put(x, tunnel, TUNNEL)
                deck.put(x, inner, WALL)
        deck.put(x0, tunnel, HIDDEN)
        deck.put(x1, tunnel, HIDDEN)
        for rx0, ry0, rx1, ry1, label in deck.rects:
            touches = ry0 == 0 if hull == 0 else ry1 == DEPTH - 1
            if touches and label.startswith(SERVICE_ROOMS) and x0 <= rx0 < x1:
                deck.put((rx0 + rx1) // 2, inner, HIDDEN)
    for x0, y_wall in ((0, 20), (0, 28), (90, 20), (90, 28)):  # tech rooms into the end airlocks
        deck.put(x0 + 3, y_wall, HIDDEN)
    for x0, y0, x1, y1 in LADDER_HATCHES:
        deck.fill(x0, y0, x1, y1, SEALED if deck.sealed else LADDER)


def north_rooms(deck, rooms):
    for x0, x1, label in rooms:
        deck.room(x0, 0, x1, 22, label, door=("S", None))


def south_rooms(deck, rooms):
    for x0, x1, label in rooms:
        deck.room(x0, 26, x1, 48, label, door=("N", None))


def sealed(number, letter):
    """Known only from the original blueprints; every way in is welded shut."""
    deck = Deck(number, Name.SEALED.format(letter=letter), sealed=True)
    common_core(deck)
    bay = Room.UNKNOWN_BAY
    north_rooms(deck, [(16, 32, bay), (32, 46, bay), (50, 70, bay), (70, 90, bay)])
    south_rooms(deck, [(6, 30, bay), (30, 56, bay), (56, 80, bay)])
    deck.room(80, 26, 90, 36, "")
    deck.room(56, 3, 64, 8, Room.UNKNOWN)
    stairs_and_shaft(deck)
    return deck


def engineering(number):
    deck = Deck(number, Name.ENGINEERING)
    common_core(deck)
    north_rooms(deck, [(16, 46, Room.REACTOR), (50, 70, Room.POWER), (70, 90, Room.ATMOSPHERE.format(n=1))])
    south_rooms(deck, [(6, 30, Room.GRAVITY), (30, 50, Room.WATER), (50, 66, Room.WORKSHOP), (66, 80, Room.SPARES)])
    deck.room(80, 26, 90, 36, "")
    stairs_and_shaft(deck)
    return deck


def security_and_storage(number):
    deck = Deck(number, Name.SECURITY)
    common_core(deck)
    north_rooms(deck, [(16, 30, Room.SECURITY), (30, 46, Room.ARMORY)])
    cabin_wing(deck, 70, 90, "N", [79], shared=[(70, 78, 3, Room.BARRACKS)])
    deck.room(50, 0, 70, 22, "", door=("S", None))
    for x0 in range(50, 70, 4):
        deck.room(x0, 0, x0 + 4, 7, deck.numbered(Room.CELL), door=("S", 1))
    deck.label(50, 7, 70, 22, Room.BRIG)
    deck.rooms.append(Room.BRIG)
    south_rooms(deck, [(6, 30, Room.CARGO_AIRLOCK), (30, 46, Room.STORAGE.format(n=1)),
                       (46, 62, Room.STORAGE.format(n=2)), (62, 80, Room.STORAGE.format(n=3))])
    deck.door_on(6, 26, 30, 48, "S", None, 8)
    deck.room(80, 26, 90, 36, "")
    stairs_and_shaft(deck)
    return deck


def habitat(number, letter, lounge, extra):
    deck = Deck(number, Name.HABITAT.format(letter=letter))
    common_core(deck)
    # A washroom on every corridor: the cabins of one corridor never walk past another's.
    cabin_wing(deck, 16, 46, "N", [28],
               shared=[(16, 22, 1, Room.WASHROOM), (16, 28, 3, Room.SHOWERS.format(n=1))])
    cabin_wing(deck, 50, 90, "N", [68],
               shared=[(50, 56, 1, Room.WASHROOM), (50, 56, 2, Room.WASHROOM), (76, 90, 3, Room.LAUNDRY)])
    cabin_wing(deck, 6, 50, "S", [24],
               shared=[(44, 50, 1, Room.WASHROOM), (6, 18, 3, Room.SHOWERS.format(n=2))])
    south_rooms(deck, [(50, 66, lounge), (66, 80, extra)])
    deck.spawn_rooms = (lounge,)
    deck.room(80, 26, 90, 36, "")
    stairs_and_shaft(deck)
    return deck


def campus(number):
    deck = Deck(number, Name.CAMPUS)
    common_core(deck)
    north_rooms(deck, [(16, 28, Room.KITCHEN), (28, 46, Room.MESS)])
    deck.door_on(16, 0, 28, 22, "E", 6)
    deck.window(29, 0, 45, 0)
    deck.room(50, 0, 90, 22, "", door=("S", None, 4))
    deck.fill(60, 4, 80, 14, DANCE)
    deck.label(60, 4, 80, 14, Room.DANCE_FLOOR)
    deck.fill(52, 18, 62, 18, COUNTER)
    deck.label(50, 18, 70, 22, Room.BAR)
    deck.window(51, 0, 89, 0)
    deck.rooms.append(Room.BAR)
    south_rooms(deck, [(6, 24, Room.MED_INTAKE), (24, 40, Room.MED_SURGERY), (40, 54, Room.MED_ISOLATION),
                       (54, 66, Room.GYM), (66, 80, Room.ATMOSPHERE.format(n=3))])
    deck.door_on(24, 26, 40, 48, "W", 10)
    deck.room(80, 26, 90, 36, "")
    stairs_and_shaft(deck)
    return deck


def science(number):
    deck = Deck(number, Name.SCIENCE)
    common_core(deck)
    north_rooms(deck, [(16, 30, Room.LAB_BIO), (30, 46, Room.LAB_CHEM), (50, 66, Room.LAB_PHYS),
                       (66, 90, Room.OBSERVATORY)])
    deck.window(67, 0, 89, 0)
    south_rooms(deck, [(6, 30, Room.HYDROPONICS.format(n=1)), (30, 54, Room.HYDROPONICS.format(n=2)),
                       (54, 66, Room.LAB_MED), (66, 80, Room.ATMOSPHERE.format(n=4))])
    deck.door_on(6, 26, 30, 48, "E", 10)
    deck.window(8, 48, 52, 48)
    deck.room(80, 26, 90, 36, "")
    stairs_and_shaft(deck)
    return deck


def command(number):
    deck = Deck(number, Name.COMMAND)
    common_core(deck)
    north_rooms(deck, [(16, 46, Room.BRIDGE), (50, 62, Room.NAVIGATION), (62, 74, Room.COMMS),
                       (74, 90, Room.BRIEFING)])
    deck.window(17, 0, 45, 0)
    south_rooms(deck, [(6, 26, Room.CAPTAIN), (64, 80, Room.ATMOSPHERE.format(n=5))])
    deck.berths += 1
    cabin_wing(deck, 26, 64, "S", [46], shared=[(26, 36, 0, Room.BRIDGE_GUARD)], layout=OFFICER_ROWS, width=5)
    deck.room(80, 26, 90, 36, "")
    stairs_and_shaft(deck)
    return deck


def door_boxes(grid):
    """Doors are connected groups of door cells, whatever their width: one box each."""
    seen = set()
    boxes = []
    for y in range(DEPTH):
        for x in range(WIDTH):
            if grid[y][x] not in DOORS or (x, y) in seen:
                continue
            cells = []
            stack = [(x, y)]
            while stack:
                cx, cy = stack.pop()
                if (cx, cy) in seen or not (0 <= cx < WIDTH and 0 <= cy < DEPTH) or grid[cy][cx] not in DOORS:
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
# A wall of the plan becomes blocks one cell thick on the grid line through its characters'
# first cells: x m on the plan is cell 4x.

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

# Stairwells (their walls' x0, y0, x1, y1) and the way their flight climbs along y: away from
# the door, up a 2 m wide flight over the first ramp column, to a landing by the far wall.
STAIRWELLS = (((6, 12, 16, 22), -1), ((80, 26, 90, 36), 1))


def wall_like(grid, x, y):
    return 0 <= x < WIDTH and 0 <= y < DEPTH and grid[y][x] in WALL_LIKE


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


def door_axes(grid):
    """Every door group in a wall with the axis the wall runs along; a door stands in a straight wall."""
    doors = []
    for x0, y0, x1, y1 in door_boxes(grid):
        if (x0, y0, x1, y1) in LADDER_HATCHES:  # welded hatches in the floor: the slab stays closed
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


def deck_floor_z(deck):
    return (deck.number - 1) * DECK_PITCH_CELLS + SLAB_CELLS


def stairs_open(decks, index):
    """Whether the slab under deck `index` (counted from 0) opens over the flights below it."""
    return 0 < index < len(decks) and not (decks[index - 1].sealed and not decks[index].sealed)


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


def flight_blocks(z):
    blocks = []
    for stairwell in STAIRWELLS:
        x, bottom, climb = flight(stairwell)
        for step in range(FLIGHT_WEDGES):
            y = bottom + step * WEDGE_CELLS if climb > 0 else bottom - (step + 1) * WEDGE_CELLS
            for column in range(FLIGHT_WIDTH_WEDGES):
                blocks.append(block(SLOPE, (x + column * WEDGE_CELLS, y, z + step * WEDGE_CELLS),
                                    (WEDGE_CELLS,) * 3, TO_POS_Y if climb > 0 else TO_NEG_Y))
    return blocks


def slab_blocks(decks, index):
    """The floor of deck `index` (the roof when index == len(decks)), open over the flights below."""
    width, depth = (WIDTH - 1) * CELLS_PER_M + 1, (DEPTH - 1) * CELLS_PER_M + 1
    mask = [[True] * width for _ in range(depth)]
    if stairs_open(decks, index):
        for stairwell in STAIRWELLS:
            fx0, fy0, fx1, fy1 = flight_footprint(stairwell)
            for y in range(fy0, fy1 + 1):
                for x in range(fx0, fx1 + 1):
                    mask[y][x] = False
    z = index * DECK_PITCH_CELLS
    blocks = []
    for x0, y0, x1, y1 in rectangles(mask):
        for px in range(x0, x1 + 1, MAX_SIZE_CELLS):
            for py in range(y0, y1 + 1, MAX_SIZE_CELLS):
                size = (min(MAX_SIZE_CELLS, x1 + 1 - px), min(MAX_SIZE_CELLS, y1 + 1 - py), SLAB_CELLS)
                blocks.append(block(FLOOR, (px, py, z), size))
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


def station_blocks(decks):
    blocks = []
    for index in range(len(decks) + 1):
        blocks += slab_blocks(decks, index)
    for index, deck in enumerate(decks):
        z = deck_floor_z(deck)
        doors = door_axes(deck.grid)
        for kind, axis, (x, y), length in line_runs(line_cells(deck.grid, doors)):
            if kind == DOOR_KIND:
                continue
            primitive, height = LINE_PRIMITIVES[kind]
            start, line = (x, y) if axis == 0 else (y, x)
            blocks += line_blocks(primitive, axis, start, line, z, length, height)
        blocks += door_blocks(deck.grid, doors, z)
        if stairs_open(decks, index + 1):
            blocks += flight_blocks(z)
        blocks += spawn_blocks(deck, z)
    return blocks


def write_blueprint(path, blocks):
    """One block per line, in FlatBuffers JSON (src/lib_z13/schemas/fbs/blueprint.fbs)."""
    lines = [json.dumps(entry, separators=(", ", ": ")) for entry in blocks]
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("{\n  \"blocks\": [\n    " + ",\n    ".join(lines) + "\n  ]\n}\n", encoding="ascii")


def interior_m2(deck):
    return sum(1 for row in deck.grid[1:-1] for c in row[1:-1] if c not in (WALL, WINDOW, *DOORS))


def render(deck):
    header = [DECK_HEADER.format(number=deck.number, name=deck.name, floor=(deck.number - 1) * DECK_PITCH_M),
              *DECK_LEGEND]
    if deck.sealed:
        header.append(SEALED_NOTE)
    header += ["", HEADER_RULER]
    plan = [row[:] for row in deck.grid]
    for x, y, text in deck.labels:
        plan[y][x:x + len(text)] = list(text)
    body = [f"{y:3} " + "".join(row).rstrip() for y, row in enumerate(plan)]
    return "\n".join(header + body) + "\n"


def render_section(decks):
    """Cut along the spine (y = 24 m), seen from the south; one row is 1 m of height."""
    top = len(decks) * DECK_PITCH_M
    dome = 3
    rows = [[" "] * WIDTH for _ in range(top + dome + 1)]
    for deck in decks:
        base = (deck.number - 1) * DECK_PITCH_M
        rows[base] = [WALL] * WIDTH
        for z in range(base + 1, base + DECK_PITCH_M):
            rows[z][0] = rows[z][WIDTH - 1] = WALL
            for x0 in (7, 81):  # stairwell flights, as on the decks
                step = z - base
                rows[z][x0 + step] = RAMP
                rows[z][x0 + 8 - step] = "\\"
        label = f"{deck.number}: {deck.name.upper()}"
        for i, ch in enumerate(label):
            rows[base + 2][20 + i] = ch
        # Stair openings, closed over a sealed deck.
        if deck.number > 1:
            below_sealed = decks[deck.number - 2].sealed
            for x in (*range(7, 16), *range(81, 90)):
                rows[base][x] = SEALED if below_sealed and not deck.sealed else " "
    rows[top] = [WALL] * WIDTH
    for z in range(top, top + dome + 1):
        rows[z][16] = rows[z][46] = WALL
    rows[top + dome][16:47] = [WINDOW] * 31
    for z in range(0, top):
        for x in range(47, 50):
            rows[z][x] = SHAFT
        for x in (2, 3, 93, 94):
            rows[z][x] = LADDER
    for z in range(DECK_PITCH_M, top, DECK_PITCH_M):  # ladder hatches over a sealed deck
        if decks[z // DECK_PITCH_M - 1].sealed and not decks[z // DECK_PITCH_M].sealed:
            for x in (2, 3, 93, 94):
                rows[z][x] = SEALED
        if decks[z // DECK_PITCH_M].sealed and z % DECK_PITCH_M == 2:
            rows[z][46] = SEALED
    body = [f"{z:3} " + "".join(row).rstrip() for z, row in reversed(list(enumerate(rows)))]
    return "\n".join([*SECTION_LEGEND, ""] + body + [HEADER_RULER]) + "\n"


def main():
    builders = [
        lambda n: sealed(n, "Sigma"), lambda n: sealed(n, "Omega"), engineering, security_and_storage,
        lambda n: habitat(n, "A", Room.LOUNGE, Room.ATMOSPHERE.format(n=2)),
        lambda n: habitat(n, "B", Room.GAMES, Room.ATMOSPHERE.format(n=6)),
        campus, science, command,
    ]
    decks = [build(number) for number, build in enumerate(builders, start=1)]
    lines = [STATS_TITLE, ""]
    for old in OUT.glob(DECK_FILE_GLOB):
        old.unlink()
    berths = rooms = doors = air_cells = 0
    for deck in decks:
        (OUT / DECK_FILE.format(number=deck.number)).write_text(render(deck), encoding="ascii")
        area = interior_m2(deck)
        deck_doors = len(door_boxes(deck.grid))
        deck_air = area * CELLS_PER_M * CELLS_PER_M * ROOM_HEIGHT_CELLS
        berths += deck.berths
        rooms += len(deck.rooms)
        doors += deck_doors
        air_cells += deck_air
        lines.append(STATS_DECK.format(number=deck.number, name=deck.name, berths=deck.berths, rooms=len(deck.rooms),
                                       doors=deck_doors, area=area, air=f"{deck_air:,}"))
    lines += ["", STATS_TOTAL.format(berths=berths, rooms=rooms, doors=doors), STATS_AIR.format(air=f"{air_cells:,}")]
    blocks = station_blocks(decks)
    write_blueprint(BLUEPRINT_FILE, blocks)
    kinds = {}
    for entry in blocks:
        kinds[entry["primitive"]] = kinds.get(entry["primitive"], 0) + 1
    lines.append(STATS_BLOCKS.format(file=BLUEPRINT_FILE.name, total=len(blocks),
                                     kinds=", ".join(f"{name} {count}" for name, count in kinds.items())))
    (OUT / STATS_FILE).write_text("\n".join(lines) + "\n", encoding="ascii")
    (OUT / SECTION_FILE).write_text(render_section(decks), encoding="ascii")
    print("\n".join(lines))


if __name__ == "__main__":
    main()
