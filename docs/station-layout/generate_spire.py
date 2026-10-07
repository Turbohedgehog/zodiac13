#!/usr/bin/env python3
"""Draws the spire station's decks as text, estimates its load and writes its blueprint.

The test station's rooms, crew and load on decks of different shapes around one core: octagonal
hulls whose corners are stepped on the 0.25 m grid, an atrium through decks 5-7 under a glass
roof, a machine hall through decks 3-4, docking pylons, a cantilevered observatory and a glass
roof over the bridge. One character is 1 m (4 grid cells), as in generate_layout.py.
"""

from pathlib import Path

from station_blueprint import (
    CELLS_PER_M, COUNTER, DANCE, DECK_PITCH_CELLS, DECK_PITCH_M, DOOR, DOORS, FLOOR, HIDDEN, LADDER, RAMP,
    ROOM_HEIGHT_CELLS, SEALED, SHAFT, TUNNEL, WALL, WALL_BLOCK, WALL_HEIGHT_CELLS, WALL_LIKE, WINDOW,
    TO_POS_Y, WINDOW_BLOCK, Deck, block, block_kinds, clear_flights, deck_floor_z, door_boxes, flight_blocks,
    mask_blocks, render_plan, ruler, spawn_blocks, stairs_open, wall_blocks, write_blueprint)

# ----- Output -------------------------------------------------------------------------------

OUT = Path(__file__).parent / "spire"
DECK_FILE = "deck-{number}.txt"
DECK_FILE_GLOB = "deck-*.txt"
SECTION_FILE = "section.txt"
STATS_FILE = "stats.txt"
BLUEPRINT_FILE = Path(__file__).parent.parent.parent / "assets" / "station" / "blueprints" / "spire.json"

DECK_HEADER = "Spire, deck {number}: {name}. Floor at {floor} m."
DECK_LEGEND = (
    "1 char = 1 m (4 cells). # wall, = window, D door, X welded, / stairs, L lift, : dance floor, _ railing.",
    "% hull corner stepped on the 0.25 m grid (window where it reads +). . hidden tunnel, S hidden door, H ladder.",
    "The core is the same on every deck: ring corridor round the atrium square (x 50-70, y 38-58), stairs, lift.",
)
SEALED_NOTE = "Sealed deck: its entrances are welded (X); the plan is from the original blueprints, the bays' purpose unknown to the crew."
SECTION_LEGEND = (
    "Cut through the core (y = 48 m), seen from the south. 1 char = 1 m across, 1 row = 1 m of height.",
    "= glass: atrium roof under deck 8, observatory floor, bridge roof. Gaps in a floor are the machine hall",
    "(decks 3-4) and the atrium (decks 5-7). Pylons with airlocks stick out of deck 4.",
)
STATS_TITLE = "Spire station load estimate (written by generate_spire.py)."
STATS_DECK = "Deck {number} ({name}): berths {berths}, rooms {rooms}, doors {doors}, area {area} m2, air cells {air}"
STATS_TOTAL = "Single cabins: {berths}. Rooms {rooms}, doors {doors}."
STATS_AIR = "Air cells (0.25 m grid): {air}."
STATS_BLOCKS = "Blueprint blocks ({file}): {total} - {kinds}."


class Name:
    SEALED = "Sealed deck {letter}"
    ENGINEERING = "Engineering deck"
    SECURITY = "Security & storage deck"
    HABITAT = "Habitat deck {letter}"
    CAMPUS = "Campus deck"
    SCIENCE = "Science deck"
    COMMAND = "Command deck"


class Room:
    TECH = "TECH"
    STORE = "STORE"
    SWITCHBOARD = "SW"
    AIRLOCK = "AL"
    STAIRS = "STAIRS"
    LIFT = "LIFT"
    RING = "RING"
    TUNNEL = "TUNNEL"
    CABINS = "CABINS"
    UNKNOWN_BAY = "BAY ?"
    REACTOR = "REACTOR HALL"
    POWER = "POWER"
    ATMOSPHERE = "ATMOS-{n}"
    GRAVITY = "GRAVGEN"
    WATER = "WATER"
    WORKSHOP = "WORKSHOP"
    SPARES = "SPARES"
    GALLERY = "HALL GALLERY"
    SECURITY = "SECURITY"
    GUARD_ROOM = "GUARD ROOM"
    ARMORY = "ARMORY"
    BRIG = "BRIG"
    CELL = "C"
    PYLON = "PYLON"
    CARGO_AIRLOCK = "CARGO AIRLOCK"
    STORAGE = "STORE-{n}"
    GARDEN = "ATRIUM GARDEN"
    ATRIUM = "ATRIUM"
    WASHROOM = "WC"
    SHOWERS = "SHOWER"
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
SERVICE_ROOMS = (Room.SWITCHBOARD, Room.TECH, "ATMOS", Room.POWER, Room.GRAVITY, Room.WATER, Room.COMMS, "BAY")

# ----- Geometry -----------------------------------------------------------------------------

WIDTH, DEPTH = 121, 97
CABIN_WIDTH_M = 3  # single cabins, 3 x 4 m
OFFICER_WIDTH_M = 5
DIAGONAL, DIAGONAL_WINDOW = "%", "+"

# The core, the same on every deck. The square is the atrium on decks 5-7, the machine hall on
# decks 3-4 and a room elsewhere; the ring corridor runs round it.
SQUARE = (50, 38, 70, 58)
RING = (46, 34, 74, 62)
STAIRWELLS = (((46, 24, 56, 34), -1), ((59, 62, 69, 72), 1))
LIFT = (59, 28, 63, 34)
LADDER_ROOMS = ((59, 24, 63, 28), (46, 62, 56, 68))
LADDER_HATCHES = ((60, 25, 61, 26), (48, 64, 49, 65))
RADIAL_X = (56, 59)  # the north and south corridors from the ring
NORTH_CORRIDOR, SOUTH_CORRIDOR = (21, 24), (72, 75)
SIDE_CORRIDOR = (46, 50)  # the west and east corridors to the airlocks
AIRLOCK_DEPTH = 6

# Slabs (counted from 0, under deck index + 1) open over the core square, or glazed over it.
OPEN_SQUARE_SLABS = (3, 5, 6)
GLASS_SQUARE_SLABS = (7, 9)


class Hull:
    """An octagon: a box with its corners cut at 45 degrees, `chamfer` m along each side, plus
    `extras` boxes (pylons, bays) that the slabs also cover."""

    def __init__(self, x0, y0, x1, y1, chamfer, extras=(), glass=()):
        self.x0, self.y0, self.x1, self.y1, self.chamfer = x0, y0, x1, y1, chamfer
        self.extras = extras
        # Boxes whose floor under this deck is glass.
        self.glass = glass

    def cut(self, x, y):
        """How far inside the corner cut (x, y) is: 0 on the diagonal, negative outside it."""
        c = self.chamfer
        return min(x - self.x0 + y - self.y0, self.x1 - x + y - self.y0,
                   x - self.x0 + self.y1 - y, self.x1 - x + self.y1 - y) - c

    def holds_cell(self, cx, cy):
        """Whether the slab under or over this deck covers grid cell (cx, cy)."""
        m = CELLS_PER_M
        inside = (self.x0 * m <= cx <= self.x1 * m and self.y0 * m <= cy <= self.y1 * m and
                  min(cx - self.x0 * m + cy - self.y0 * m, self.x1 * m - cx + cy - self.y0 * m,
                      cx - self.x0 * m + self.y1 * m - cy, self.x1 * m - cx + self.y1 * m - cy)
                  >= self.chamfer * m - 1)
        return inside or any(bx0 * m <= cx <= bx1 * m and by0 * m <= cy <= by1 * m
                             for bx0, by0, bx1, by1 in self.extras)

    def left(self, y):
        """The hull's character on row y, west side."""
        return self.x0 + max(0, self.chamfer - (y - self.y0), self.chamfer - (self.y1 - y))

    def right(self, y):
        return self.x1 - max(0, self.chamfer - (y - self.y0), self.chamfer - (self.y1 - y))

    def diagonal_row(self, y):
        return y - self.y0 < self.chamfer or self.y1 - y < self.chamfer

    def inner_left(self, ya, yb):
        """The westmost x a wall spanning rows ya-yb may stand on without touching a diagonal."""
        return max(self.left(y) + (2 if self.diagonal_row(y) else 0) for y in range(ya, yb + 1))

    def inner_right(self, ya, yb):
        return min(self.right(y) - (2 if self.diagonal_row(y) else 0) for y in range(ya, yb + 1))

    def diagonals(self):
        """Each cut corner as its two ends on the hull's straight walls."""
        x0, y0, x1, y1, c = self.x0, self.y0, self.x1, self.y1, self.chamfer
        return (((x0 + c, y0), (x0, y0 + c)), ((x1 - c, y0), (x1, y0 + c)),
                ((x0, y1 - c), (x0 + c, y1)), ((x1, y1 - c), (x1 - c, y1)))


class SpireDeck(Deck):
    def __init__(self, number, name, hull, sealed=False, glazed_corners=False):
        super().__init__(number, name, WIDTH, DEPTH, sealed)
        self.hull = hull
        self.glazed_corners = glazed_corners and not sealed
        self.hatches = LADDER_HATCHES


# ----- Drawing ------------------------------------------------------------------------------

def draw_hull(deck):
    h = deck.hull
    for x in range(h.x0 + h.chamfer, h.x1 - h.chamfer + 1):
        deck.put(x, h.y0, WALL)
        deck.put(x, h.y1, WALL)
    for y in range(h.y0 + h.chamfer, h.y1 - h.chamfer + 1):
        deck.put(h.x0, y, WALL)
        deck.put(h.x1, y, WALL)
    corner = DIAGONAL_WINDOW if deck.glazed_corners else DIAGONAL
    for (ax, ay), (bx, by) in h.diagonals():
        sx, sy = (1 if bx > ax else -1), (1 if by > ay else -1)
        for i in range(1, abs(bx - ax)):
            deck.put(ax + sx * i, ay + sy * i, corner)


def corridor(deck, y0, y1, xa=None, xb=None):
    """Two walls along x from the hull (or xa) to the hull (or xb)."""
    h = deck.hull
    for y in (y0, y1):
        left = h.left(y) + (1 if h.diagonal_row(y) else 0) if xa is None else xa
        right = h.right(y) - (1 if h.diagonal_row(y) else 0) if xb is None else xb
        for x in range(left, right + 1):
            deck.put(x, y, WALL)


def radial(deck, y_from, y_to, corridors):
    """The north-south corridor between RADIAL_X: walls except inside crossed corridors, open
    through their walls."""
    xa, xb = RADIAL_X
    lo, hi = sorted((y_from, y_to))
    for y in range(lo, hi + 1):
        inside = any(c0 < y < c1 for c0, c1 in corridors)
        for x in (xa, xb):
            deck.put(x, y, " " if inside else WALL)
        for x in range(xa + 1, xb):
            deck.put(x, y, " ")


def tunnels(deck, skip=()):
    """1 m tunnels along the north and south hull, behind a wall, with hidden doors into service rooms."""
    h = deck.hull
    for hull_y, step in ((h.y0, 1), (h.y1, -1)):
        tunnel_y, inner_y = hull_y + step, hull_y + 2 * step
        for x in range(h.left(tunnel_y) + 1, h.right(tunnel_y)):
            if x not in skip:
                deck.put(x, tunnel_y, TUNNEL)
        for x in range(h.left(inner_y) + 1, h.right(inner_y)):
            deck.put(x, inner_y, WALL)
    deck.rooms += [Room.TUNNEL, Room.TUNNEL]


def hidden_doors(deck):
    h = deck.hull
    for rx0, ry0, rx1, ry1, label in deck.rects:
        if not label.startswith(SERVICE_ROOMS):
            continue
        if ry0 == h.y0 + 2:
            deck.put((rx0 + rx1) // 2, ry0, HIDDEN)
        if ry1 == h.y1 - 2:
            deck.put((rx0 + rx1) // 2, ry1, HIDDEN)


def core(deck, square):
    """The ring corridor, stairs, lift, ladders and service rooms round the square."""
    x0, y0, x1, y1 = RING
    deck.rect(x0, y0, x1, y1)
    deck.rooms.append(Room.RING)
    sx0, sy0, sx1, sy1 = SQUARE
    if square == "railing":
        for x in range(sx0, sx1 + 1):
            deck.put(x, sy0, COUNTER)
            deck.put(x, sy1, COUNTER)
        for y in range(sy0, sy1 + 1):
            deck.put(sx0, y, COUNTER)
            deck.put(sx1, y, COUNTER)
    for (wx0, wy0, wx1, wy1), climb in STAIRWELLS:
        deck.rect(wx0, wy0, wx1, wy1)
        deck.fill(wx0 + 1, wy0 + 1, wx0 + 2, wy1 - 1, RAMP)
        deck.fill(wx1 - 2, wy0 + 1, wx1 - 1, wy1 - 1, RAMP)
        deck.door_on(wx0, wy0, wx1, wy1, "S" if climb < 0 else "N", 4, ch=deck.entry)
    lx0, ly0, lx1, ly1 = LIFT
    deck.rect(lx0, ly0, lx1, ly1)
    deck.fill(lx0 + 1, ly0 + 1, lx1 - 1, ly1 - 1, SHAFT)
    deck.door_on(lx0, ly0, lx1, ly1, "S", 1, 3, deck.entry)
    deck.rooms += [Room.STAIRS, Room.STAIRS, Room.LIFT]
    north_tech, south_tech = LADDER_ROOMS
    deck.room(*north_tech, Room.TECH, door=("W", 1))
    deck.room(*south_tech, Room.TECH, door=("N", 4))
    for hx0, hy0, hx1, hy1 in deck.hatches:
        deck.fill(hx0, hy0, hx1, hy1, SEALED if deck.sealed else LADDER)
    deck.room(63, 24, 74, 34, Room.SWITCHBOARD, door=("S", None))
    deck.room(46, 68, 56, 72, Room.STORE, door=("S", None))
    deck.room(69, 62, 74, 72, Room.TECH, door=("N", None))
    radial(deck, NORTH_CORRIDOR[1], RING[1], [])
    radial(deck, RING[3], SOUTH_CORRIDOR[0], [])
    for ry in (RING[1], RING[3]):
        deck.door_on(RADIAL_X[0], ry, RADIAL_X[1], ry, "N", 1)


def side_corridors(deck, west_door=None, east_door=None):
    """West and east corridors from the ring to an airlock on the hull (or to `*_door` instead)."""
    h = deck.hull
    y0, y1 = SIDE_CORRIDOR
    for hull_x, ring_x, outer, inner, custom in ((h.x0, RING[0], "W", "E", west_door),
                                                 (h.x1, RING[2], "E", "W", east_door)):
        lo, hi = sorted((hull_x, ring_x))
        corridor(deck, y0, y1, lo, hi)
        deck.door_on(ring_x, y0, ring_x, y1, "W", None, 3)
        if custom:
            deck.door_on(hull_x, y0, hull_x, y1, "W", None, 3, custom)
            continue
        ax0, ax1 = (hull_x, hull_x + AIRLOCK_DEPTH) if outer == "W" else (hull_x - AIRLOCK_DEPTH, hull_x)
        deck.room(ax0, y0, ax1, y1, Room.AIRLOCK, door=(outer, None, 3, deck.entry))
        deck.door_on(ax0, y0, ax1, y1, inner, None, 3)
    deck.rooms += ["W", "E"]


def band(deck, ya, yb, labels, door_side, xa=None, xb=None):
    """Rooms side by side between rows ya and yb, as wide as the hull allows (or from xa to xb)."""
    h = deck.hull
    left = h.inner_left(ya, yb) if xa is None else xa
    right = h.inner_right(ya, yb) if xb is None else xb
    edges = [left + round((right - left) * i / len(labels)) for i in range(len(labels) + 1)]
    for i, label in enumerate(labels):
        deck.room(edges[i], ya, edges[i + 1], yb, label, door=(door_side, None))


def cabin_rows(deck, rows, xa=None, xb=None, shared=(), width=CABIN_WIDTH_M, blocked=RADIAL_X):
    """Single cabins along rows (y0, y1, door side) as far as the hull allows, round `blocked`.

    `shared`: (x0, x1, row index, label) rooms that replace cabins in one row.
    """
    h = deck.hull
    door_width = 1 if width <= CABIN_WIDTH_M else 2
    for index, (y0, y1, side) in enumerate(rows):
        left = max(h.inner_left(y0, y1), xa if xa is not None else 0)
        right = min(h.inner_right(y0, y1), xb if xb is not None else WIDTH)
        taken = [blocked] + [(sx0, sx1) for sx0, sx1, row, _ in shared if row == index]
        for sx0, sx1, row, label in shared:
            if row == index:
                deck.room(sx0, y0, sx1, y1, label, door=(side, None))
        cx = left
        while cx + width <= right:
            clash = next((end for start, end in taken if cx < end and cx + width > start), None)
            if clash is not None:
                cx = clash
                continue
            deck.room(cx, y0, cx + width, y1, "", door=(side, 1, door_width))
            deck.berths += 1
            cx += width
    deck.rooms.append(Room.CABINS)


def sides(deck, west, east):
    """Four rooms on each side, north to south: by the north corridor, by the ring above the side
    corridor, by the ring below it, by the south corridor."""
    h = deck.hull
    for (top, upper, lower, bottom), x_hull, x_ring, ring_side in (
            (west, h.x0, RING[0], "E"), (east, h.x1, RING[2], "W")):
        lo, hi = sorted((x_hull, x_ring))
        deck.room(lo, NORTH_CORRIDOR[1], hi, RING[1], top, door=("N", None))
        deck.room(lo, RING[1], hi, SIDE_CORRIDOR[0], upper, door=(ring_side, None))
        deck.room(lo, SIDE_CORRIDOR[1], hi, RING[3], lower, door=(ring_side, None))
        deck.room(lo, RING[3], hi, SOUTH_CORRIDOR[0], bottom, door=("S", None))


def skeleton(deck, square, west, east, side_doors=(None, None)):
    """What every deck has: hull, tunnels, north and south corridors, side rooms and corridors."""
    draw_hull(deck)
    tunnels(deck)
    corridor(deck, *NORTH_CORRIDOR)
    corridor(deck, *SOUTH_CORRIDOR)
    core(deck, square)
    sides(deck, west, east)
    side_corridors(deck, *side_doors)


def finish(deck, extend_north=None, extend_south=None):
    """Radial corridors on to the bands' corridors, hidden doors, and the plan's own check."""
    if extend_north is not None:
        radial(deck, extend_north[0], NORTH_CORRIDOR[0], extend_north[1])
    if extend_south is not None:
        radial(deck, SOUTH_CORRIDOR[1], extend_south[0], extend_south[1])
    hidden_doors(deck)
    check_inside(deck)


def check_inside(deck):
    """Nothing drawn outside the hull, and nothing walled onto a cut corner."""
    h = deck.hull
    for y, row in enumerate(deck.grid):
        for x, ch in enumerate(row):
            if ch == " " or any(bx0 <= x <= bx1 and by0 <= y <= by1 for bx0, by0, bx1, by1 in h.extras):
                continue
            if not (h.x0 <= x <= h.x1 and h.y0 <= y <= h.y1) or h.cut(x, y) < 0:
                raise ValueError(f"deck {deck.number}: '{ch}' at {x},{y} is outside the hull")


# ----- Decks --------------------------------------------------------------------------------

def sealed(number, letter, hull, bays):
    """Known only from the original blueprints; every way in is welded shut."""
    deck = SpireDeck(number, Name.SEALED.format(letter=letter), hull, sealed=True)
    bay = Room.UNKNOWN_BAY
    skeleton(deck, "room", (bay,) * 4, (bay,) * 4)
    deck.room(*SQUARE, bay, door=("N", None))
    band(deck, hull.y0 + 2, NORTH_CORRIDOR[0], [bay] * bays, "S")
    band(deck, SOUTH_CORRIDOR[1], hull.y1 - 2, [bay] * bays, "N")
    finish(deck)
    return deck


def engineering(number, hull):
    deck = SpireDeck(number, Name.ENGINEERING, hull)
    skeleton(deck, "room", (Room.TECH, Room.POWER, Room.ATMOSPHERE.format(n=1), Room.STORAGE.format(n=1)),
             (Room.TECH, Room.SPARES, Room.WORKSHOP, Room.STORAGE.format(n=2)))
    deck.room(*SQUARE, Room.REACTOR, door=("N", None, 4))
    deck.door_on(*SQUARE, "S", None, 4)
    deck.door_on(*SQUARE, "W", None, 3)
    deck.door_on(*SQUARE, "E", None, 3)
    band(deck, hull.y0 + 2, NORTH_CORRIDOR[0], [Room.GRAVITY, Room.POWER, Room.WATER], "S")
    band(deck, SOUTH_CORRIDOR[1], hull.y1 - 2, [Room.WORKSHOP, Room.STORAGE.format(n=3), Room.TECH], "N")
    finish(deck)
    return deck


PYLON = (SIDE_CORRIDOR[0], SIDE_CORRIDOR[1])


def security_and_storage(number, hull):
    deck = SpireDeck(number, Name.SECURITY, hull)
    skeleton(deck, "railing", (Room.TECH, Room.SECURITY, Room.ARMORY, Room.STORAGE.format(n=1)),
             (Room.TECH, Room.ATMOSPHERE.format(n=7), Room.STORAGE.format(n=5), Room.STORAGE.format(n=6)),
             side_doors=(DOOR, DOOR))
    deck.label(*SQUARE, Room.GALLERY)
    deck.rooms.append(Room.GALLERY)
    # Pylons: a windowed corridor to an airlock on the west, to the cargo airlock on the east.
    y0, y1 = PYLON
    west_end, east_end = hull.extras
    deck.rect(west_end[0], y0, hull.x0, y1)
    deck.window(west_end[0] + AIRLOCK_DEPTH + 1, y0, hull.x0 - 1, y1)
    deck.room(west_end[0], y0, west_end[0] + AIRLOCK_DEPTH, y1, Room.AIRLOCK, door=("W", None, 3, deck.entry))
    deck.door_on(west_end[0], y0, west_end[0] + AIRLOCK_DEPTH, y1, "E", None, 3)
    cx0, cy0, cx1, cy1 = east_end[0] + 12, east_end[1], east_end[2], east_end[3]
    deck.rect(hull.x1, y0, cx0, y1)
    deck.window(hull.x1 + 1, y0, cx0 - 1, y1)
    deck.room(cx0, cy0, cx1, cy1, Room.CARGO_AIRLOCK, door=("E", None, 8, deck.entry))
    deck.door_on(cx0, cy0, cx1, cy1, "W", None, 3)
    deck.rooms += [Room.PYLON, Room.PYLON]
    # Brig: cells along the north hull, then the guard room and a store.
    brig_x0 = hull.inner_left(hull.y0 + 2, NORTH_CORRIDOR[0])
    for x0 in range(brig_x0, brig_x0 + 16, 4):
        deck.room(x0, hull.y0 + 2, x0 + 4, hull.y0 + 7, deck.numbered(Room.CELL), door=("S", 1))
    deck.room(brig_x0, hull.y0 + 7, brig_x0 + 16, NORTH_CORRIDOR[0], Room.BRIG, door=("S", None))
    band(deck, hull.y0 + 2, NORTH_CORRIDOR[0], [Room.GUARD_ROOM, Room.STORAGE.format(n=2)], "S", xa=brig_x0 + 16)
    # Guards' cabins along the south corridor, stores behind them.
    cabin_rows(deck, [(SOUTH_CORRIDOR[1], SOUTH_CORRIDOR[1] + 4, "N")])
    corridor(deck, 79, 81)
    band(deck, 81, hull.y1 - 2, [Room.STORAGE.format(n=3), Room.STORAGE.format(n=4)], "N")
    finish(deck, extend_south=(81, [SOUTH_CORRIDOR]))
    return deck


# Cabin rows and their corridors north and south of the core, mirrored.
HABITAT_NORTH = ([(7, 11, "S"), (13, 17, "N"), (17, 21, "S")], [(11, 13)])
HABITAT_SOUTH = ([(75, 79, "N"), (79, 83, "S"), (85, 89, "N")], [(83, 85)])


def habitat(number, letter, hull, square, lounge, atmosphere):
    deck = SpireDeck(number, Name.HABITAT.format(letter=letter), hull, glazed_corners=True)
    skeleton(deck, square, (Room.WASHROOM, lounge, Room.LAUNDRY, Room.SHOWERS),
             (Room.WASHROOM, atmosphere, Room.TECH, Room.SHOWERS))
    deck.label(*SQUARE, Room.GARDEN if square == "open" else Room.ATRIUM)
    deck.rooms.append(Room.ATRIUM)
    deck.spawn_rooms = (lounge,)
    for rows, corridors in (HABITAT_NORTH, HABITAT_SOUTH):
        for c0, c1 in corridors:
            corridor(deck, c0, c1)
        cabin_rows(deck, rows, shared=washrooms(deck, rows))
    finish(deck, extend_north=(HABITAT_NORTH[1][0][1], HABITAT_NORTH[1]),
           extend_south=(HABITAT_SOUTH[1][0][0], HABITAT_SOUTH[1]))
    return deck


def washrooms(deck, rows):
    """Toilets at the west end of one row on each corridor, showers at the east end of the other."""
    h = deck.hull
    first, last = rows[0], rows[-1]
    return [(h.inner_left(first[0], first[1]), h.inner_left(first[0], first[1]) + 6, 0, Room.WASHROOM),
            (h.inner_right(last[0], last[1]) - 6, h.inner_right(last[0], last[1]), len(rows) - 1, Room.SHOWERS)]


def campus(number, hull):
    deck = SpireDeck(number, Name.CAMPUS, hull, glazed_corners=True)
    skeleton(deck, "railing", (Room.TECH, Room.MED_INTAKE, Room.MED_SURGERY, Room.WASHROOM),
             (Room.TECH, Room.GYM, Room.MED_ISOLATION, Room.ATMOSPHERE.format(n=3)))
    deck.label(*SQUARE, Room.ATRIUM)
    deck.rooms.append(Room.ATRIUM)
    band(deck, hull.y0 + 2, NORTH_CORRIDOR[0], [Room.KITCHEN, Room.MESS], "S")
    # The bar fills the south: no tunnel there, a panoramic window instead.
    y0 = SOUTH_CORRIDOR[1]
    left, right = hull.inner_left(y0, hull.y1), hull.inner_right(y0, hull.y1)
    for x in range(hull.left(hull.y1 - 1) + 1, hull.right(hull.y1 - 1)):
        deck.put(x, hull.y1 - 1, " ")
    for x in range(hull.left(hull.y1 - 2) + 1, hull.right(hull.y1 - 2)):
        deck.put(x, hull.y1 - 2, " ")
    deck.room(left, y0, right, hull.y1, "", door=("N", None, 4))
    deck.window(left + 1, hull.y1, right - 1, hull.y1)
    deck.fill(left + 12, y0 + 3, right - 12, y0 + 9, DANCE)
    deck.label(left + 12, y0 + 3, right - 12, y0 + 9, Room.DANCE_FLOOR)
    deck.fill(left + 2, y0 + 3, left + 8, y0 + 3, COUNTER)
    deck.label(left, y0 + 4, left + 10, y0 + 6, Room.BAR)
    deck.rooms.append(Room.BAR)
    finish(deck)
    return deck


def science(number, hull):
    deck = SpireDeck(number, Name.SCIENCE, hull)
    skeleton(deck, "room", (Room.TECH, Room.LAB_PHYS, Room.LAB_MED, Room.WASHROOM),
             (Room.TECH, Room.ATMOSPHERE.format(n=4), Room.HYDROPONICS.format(n=2), Room.STORE))
    deck.room(*SQUARE, Room.HYDROPONICS.format(n=1), door=("N", None, 3))
    deck.door_on(*SQUARE, "S", None, 3)
    band(deck, hull.y0 + 2, NORTH_CORRIDOR[0], [Room.LAB_BIO, "OBS GALLERY", Room.LAB_CHEM], "S")
    band(deck, SOUTH_CORRIDOR[1], hull.y1 - 2, [Room.HYDROPONICS.format(n=3), Room.STORE], "N")
    # The observatory hangs over the north hull: glass walls and floor, reached across the tunnel.
    bx0, by0, bx1, by1 = hull.extras[0]
    deck.rect(bx0, by0, bx1, by1)
    deck.window(bx0, by0, bx1, by1 - 1)
    deck.rooms.append(Room.OBSERVATORY)
    deck.label(bx0, by0, bx1, by1, Room.OBSERVATORY)
    door_x = (bx0 + bx1) // 2 - 1
    for y in (hull.y0 + 1,):
        deck.put(door_x - 1, y, WALL)
        deck.put(door_x + 2, y, WALL)
        deck.put(door_x, y, " ")
        deck.put(door_x + 1, y, " ")
    deck.door_on(door_x, hull.y0, door_x + 1, hull.y0, "N", 0)
    deck.door_on(door_x, hull.y0 + 2, door_x + 1, hull.y0 + 2, "N", 0)
    finish(deck)
    return deck


def command(number, hull):
    deck = SpireDeck(number, Name.COMMAND, hull, glazed_corners=True)
    skeleton(deck, "room", (Room.NAVIGATION, Room.CAPTAIN, Room.BRIDGE_GUARD, Room.WASHROOM),
             (Room.COMMS, Room.BRIEFING, Room.ATMOSPHERE.format(n=5), Room.TECH))
    deck.room(*SQUARE, Room.BRIDGE, door=("N", None, 4))
    deck.door_on(*SQUARE, "W", None, 3)
    deck.door_on(*SQUARE, "E", None, 3)
    deck.berths += 1  # the captain
    cabin_rows(deck, [(hull.y0 + 2, NORTH_CORRIDOR[0], "S")], width=OFFICER_WIDTH_M, blocked=(0, 0))
    cabin_rows(deck, [(SOUTH_CORRIDOR[1], hull.y1 - 2, "N")], width=OFFICER_WIDTH_M, blocked=(0, 0))
    finish(deck)
    return deck


def spire():
    """The decks, bottom up, with their hulls (x0, y0, x1, y1, corner cut)."""
    habitat_hull = Hull(18, 5, 102, 91, 14)
    return [
        sealed(1, "Sigma", Hull(38, 14, 82, 82, 6), 2),
        sealed(2, "Omega", Hull(34, 12, 86, 84, 8), 3),
        engineering(3, Hull(28, 10, 92, 86, 10)),
        security_and_storage(4, Hull(28, 10, 92, 86, 10, extras=((4, 46, 28, 50), (92, 42, 116, 54)))),
        habitat(5, "A", habitat_hull, "open", Room.LOUNGE, Room.ATMOSPHERE.format(n=2)),
        habitat(6, "B", habitat_hull, "railing", Room.GAMES, Room.ATMOSPHERE.format(n=6)),
        campus(7, Hull(22, 8, 98, 88, 12)),
        science(8, Hull(30, 10, 90, 86, 10, extras=((50, 0, 70, 10),), glass=((50, 0, 70, 10),))),
        command(9, Hull(36, 12, 84, 84, 8)),
    ]


# ----- Blueprint ----------------------------------------------------------------------------

def staircase(a, b):
    """Grid cells of a cut corner from char a to char b, one cell per row: a wall that reads as a
    diagonal, without the cells of a and b themselves."""
    (ax, ay), (bx, by) = (a[0] * CELLS_PER_M, a[1] * CELLS_PER_M), (b[0] * CELLS_PER_M, b[1] * CELLS_PER_M)
    sx, sy = (1 if bx > ax else -1), (1 if by > ay else -1)
    rows = []
    for k in range(abs(by - ay)):
        cells = [ax + sx * k, ax + sx * (k + 1)]
        if k == 0:
            cells = cells[1:]
        rows.append((min(cells), ay + sy * k, len(cells)))
    return rows


def corner_blocks(deck, z):
    """The cut corners, and short walls from an inner wall up to a corner it meets."""
    h = deck.hull
    primitive = WINDOW_BLOCK if deck.glazed_corners else WALL_BLOCK
    blocks = []
    taken = set()
    for a, b in h.diagonals():
        for x, y, length in staircase(a, b):
            blocks.append(block(primitive, (x, y, z), (length, 1, WALL_HEIGHT_CELLS)))
            taken.update((x + i, y) for i in range(length))
    corners = DIAGONAL + DIAGONAL_WINDOW
    for y, row in enumerate(deck.grid):
        for x, ch in enumerate(row):
            if ch not in WALL_LIKE:
                continue
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                nx, ny = x + dx, y + dy
                if not (0 <= ny < DEPTH and 0 <= nx < WIDTH) or deck.grid[ny][nx] not in corners:
                    continue
                cells = []
                for step in range(1, CELLS_PER_M):
                    cell = (x * CELLS_PER_M + dx * step, y * CELLS_PER_M + dy * step)
                    if cell in taken:
                        break
                    cells.append(cell)
                if not cells:
                    continue
                cx, cy = min(c[0] for c in cells), min(c[1] for c in cells)
                blocks.append(block(WALL_BLOCK, (cx, cy, z), (len(cells), 1, WALL_HEIGHT_CELLS),
                                    None if dy == 0 else TO_POS_Y))
                taken.update(cells)
    return blocks


FLAT = "FacePosXUpPosY"  # a wall-like primitive laid flat: its +Z along +Y, its thin Y downwards


def square_cells(square):
    sx0, sy0, sx1, sy1 = square
    return (sx0 * CELLS_PER_M + 1, sy0 * CELLS_PER_M + 1, sx1 * CELLS_PER_M - 1, sy1 * CELLS_PER_M - 1)


def box_cells(box):
    bx0, by0, bx1, by1 = box
    return (bx0 * CELLS_PER_M, by0 * CELLS_PER_M, bx1 * CELLS_PER_M, by1 * CELLS_PER_M)


def glass_blocks(mask, z):
    """Flat windows over the True cells of `mask`; a window lies with its thin side along z."""
    blocks = []
    for entry in mask_blocks(mask, z, WINDOW_BLOCK):
        size = entry["size"]
        entry["size"] = {"x": size["x"], "y": 1, "z": size["y"]}
        entry["orientation"] = FLAT
        entry["cell"]["z"] = z
        blocks.append(entry)
    return blocks


def slab_blocks(decks, index):
    """The floor of deck `index` (the roof when index == len(decks)): every cell under or over a hull,
    open over the flights, the machine hall and the atrium, glass where the decks see through."""
    width, depth = (WIDTH - 1) * CELLS_PER_M + 1, (DEPTH - 1) * CELLS_PER_M + 1
    hulls = [decks[i].hull for i in (index - 1, index) if 0 <= i < len(decks)]
    mask = [[any(h.holds_cell(x, y) for h in hulls) for x in range(width)] for y in range(depth)]
    glass = [[False] * width for _ in range(depth)]
    if stairs_open(decks, index):
        clear_flights(mask, STAIRWELLS)

    def clear(box, into=None):
        x0, y0, x1, y1 = box
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                if into is not None and mask[y][x]:
                    into[y][x] = True
                mask[y][x] = False

    if index in OPEN_SQUARE_SLABS:
        clear(square_cells(SQUARE))
    if index in GLASS_SQUARE_SLABS:
        clear(square_cells(SQUARE), glass)
    if index < len(decks):
        for box in decks[index].hull.glass:
            clear(box_cells(box), glass)
    z = index * DECK_PITCH_CELLS
    return mask_blocks(mask, z, FLOOR) + glass_blocks(glass, z)


def station_blocks(decks):
    blocks = []
    for index in range(len(decks) + 1):
        blocks += slab_blocks(decks, index)
    for index, deck in enumerate(decks):
        z = deck_floor_z(deck)
        blocks += wall_blocks(deck.grid, z, deck.hatches)
        blocks += corner_blocks(deck, z)
        if stairs_open(decks, index + 1):
            blocks += flight_blocks(STAIRWELLS, z)
        blocks += spawn_blocks(deck, z)
    return blocks


# ----- Text ---------------------------------------------------------------------------------

def interior_m2(deck):
    h = deck.hull
    open_chars = (WALL, WINDOW, COUNTER, DIAGONAL, DIAGONAL_WINDOW, *DOORS)
    return sum(1 for y, row in enumerate(deck.grid) for x, ch in enumerate(row)
               if ch not in open_chars and h.x0 < x < h.x1 and h.y0 < y < h.y1 and h.cut(x, y) > 0)


def render(deck):
    header = [DECK_HEADER.format(number=deck.number, name=deck.name, floor=(deck.number - 1) * DECK_PITCH_M),
              *DECK_LEGEND]
    if deck.sealed:
        header.append(SEALED_NOTE)
    return render_plan(deck, header)


def render_section(decks):
    """Cut through the core (y = 48 m), seen from the south; one row is 1 m of height."""
    cut_y = (SIDE_CORRIDOR[0] + SIDE_CORRIDOR[1]) // 2
    top = len(decks) * DECK_PITCH_M
    rows = [[" "] * WIDTH for _ in range(top + 1)]
    sx0, _, sx1, _ = SQUARE
    for index in range(len(decks) + 1):
        z = index * DECK_PITCH_M
        hulls = [decks[i].hull for i in (index - 1, index) if 0 <= i < len(decks)]
        for x in range(WIDTH):
            covered = any((h.x0 <= x <= h.x1 and h.cut(x, cut_y) >= 0) or
                          any(bx0 <= x <= bx1 and by0 <= cut_y <= by1 for bx0, by0, bx1, by1 in h.extras)
                          for h in hulls)
            if not covered:
                continue
            in_square = sx0 < x < sx1
            if in_square and index in OPEN_SQUARE_SLABS:
                continue
            rows[z][x] = WINDOW if in_square and index in GLASS_SQUARE_SLABS else WALL
    for deck in decks:
        base = (deck.number - 1) * DECK_PITCH_M
        h = deck.hull
        walls = [h.x0, h.x1, *[e for bx0, by0, bx1, by1 in h.extras if by0 <= cut_y <= by1 for e in (bx0, bx1)]]
        for z in range(base + 1, base + DECK_PITCH_M):
            for x in walls:
                rows[z][x] = WALL
            for x in range(59, 63):
                rows[z][x] = SHAFT
        label = f"{deck.number}: {deck.name.upper()}"
        start = max(h.x0 + 2, sx0 - len(label) - 1) if deck.number in (3, 4, 5, 6, 7) else h.x0 + 2
        for i, ch in enumerate(label):
            if 0 <= start + i < WIDTH and rows[base + 2][start + i] == " ":
                rows[base + 2][start + i] = ch
    body = [f"{z:3} " + "".join(row).rstrip() for z, row in reversed(list(enumerate(rows)))]
    return "\n".join([*SECTION_LEGEND, ""] + body + [ruler(WIDTH)]) + "\n"


def main():
    decks = spire()
    OUT.mkdir(exist_ok=True)
    for old in OUT.glob(DECK_FILE_GLOB):
        old.unlink()
    lines = [STATS_TITLE, ""]
    berths = rooms = doors = air_cells = 0
    for deck in decks:
        (OUT / DECK_FILE.format(number=deck.number)).write_text(render(deck), encoding="ascii")
        area = interior_m2(deck)
        deck_doors = len(door_boxes(deck.grid)) - len(deck.hatches)
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
    lines.append(STATS_BLOCKS.format(file=BLUEPRINT_FILE.name, total=len(blocks), kinds=block_kinds(blocks)))
    (OUT / STATS_FILE).write_text("\n".join(lines) + "\n", encoding="ascii")
    (OUT / SECTION_FILE).write_text(render_section(decks), encoding="ascii")
    print("\n".join(lines))


if __name__ == "__main__":
    main()
