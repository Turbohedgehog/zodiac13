#!/usr/bin/env python3
"""Draws the test station's decks as text, estimates its load and writes its blueprint.

One character is 1 m (4 grid cells). Writes the deck plans, the section and the stats next to
itself, and the blueprint the game builds the test station from into assets/station/blueprints/.
"""

from pathlib import Path

from station_blueprint import (
    CELLS_PER_M, COUNTER, DANCE, DECK_PITCH_CELLS, DECK_PITCH_M, DOORS, FLOOR, HIDDEN, LADDER, RAMP,
    ROOM_HEIGHT_CELLS, SEALED, SHAFT, TUNNEL, UNDER_LEDGE, WALL, WINDOW, Deck, block_kinds, clear_flights,
    deck_floor_z, door_boxes, flight_blocks, mask_blocks, render_plan, ruler, spawn_blocks, stairs_open,
    wall_blocks, write_blueprint)

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
CABIN_WIDTH_M = 3  # single cabins, 3 x 4 m



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
    deck = Deck(number, Name.SEALED.format(letter=letter), WIDTH, DEPTH, sealed=True)
    common_core(deck)
    bay = Room.UNKNOWN_BAY
    north_rooms(deck, [(16, 32, bay), (32, 46, bay), (50, 70, bay), (70, 90, bay)])
    south_rooms(deck, [(6, 30, bay), (30, 56, bay), (56, 80, bay)])
    deck.room(80, 26, 90, 36, "")
    deck.room(56, 3, 64, 8, Room.UNKNOWN)
    stairs_and_shaft(deck)
    return deck


def engineering(number):
    deck = Deck(number, Name.ENGINEERING, WIDTH, DEPTH)
    common_core(deck)
    north_rooms(deck, [(16, 46, Room.REACTOR), (50, 70, Room.POWER), (70, 90, Room.ATMOSPHERE.format(n=1))])
    south_rooms(deck, [(6, 30, Room.GRAVITY), (30, 50, Room.WATER), (50, 66, Room.WORKSHOP), (66, 80, Room.SPARES)])
    deck.room(80, 26, 90, 36, "")
    stairs_and_shaft(deck)
    return deck


def security_and_storage(number):
    deck = Deck(number, Name.SECURITY, WIDTH, DEPTH)
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
    deck = Deck(number, Name.HABITAT.format(letter=letter), WIDTH, DEPTH)
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
    deck = Deck(number, Name.CAMPUS, WIDTH, DEPTH)
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
    deck = Deck(number, Name.SCIENCE, WIDTH, DEPTH)
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
    deck = Deck(number, Name.COMMAND, WIDTH, DEPTH)
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


# ----- Blueprint ----------------------------------------------------------------------------

# Stairwells (their walls' x0, y0, x1, y1) and the way their flight climbs along y: away from
# the door, up a 2 m wide flight over the first ramp column, to a landing by the far wall.
STAIRWELLS = (((6, 12, 16, 22), -1), ((80, 26, 90, 36), 1))


def slab_blocks(decks, index):
    """The floor of deck `index` (the roof when index == len(decks)), open over the flights below."""
    width, depth = (WIDTH - 1) * CELLS_PER_M + 1, (DEPTH - 1) * CELLS_PER_M + 1
    mask = [[True] * width for _ in range(depth)]
    if stairs_open(decks, index):
        clear_flights(mask, STAIRWELLS)
    return mask_blocks(mask, index * DECK_PITCH_CELLS, FLOOR)


def station_blocks(decks):
    blocks = []
    for index in range(len(decks) + 1):
        blocks += slab_blocks(decks, index)
    for index, deck in enumerate(decks):
        z = deck_floor_z(deck)
        blocks += wall_blocks(deck.grid, z, LADDER_HATCHES)
        if stairs_open(decks, index + 1):
            blocks += flight_blocks(STAIRWELLS, z)
        blocks += spawn_blocks(deck, z)
    return blocks


def interior_m2(deck):
    return sum(1 for row in deck.grid[1:-1] for c in row[1:-1] if c not in (WALL, WINDOW, *DOORS))


def render(deck):
    header = [DECK_HEADER.format(number=deck.number, name=deck.name, floor=(deck.number - 1) * DECK_PITCH_M),
              *DECK_LEGEND]
    if deck.sealed:
        header.append(SEALED_NOTE)
    return render_plan(deck, header)


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
    return "\n".join([*SECTION_LEGEND, ""] + body + [ruler(WIDTH)]) + "\n"


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
    lines.append(STATS_BLOCKS.format(file=BLUEPRINT_FILE.name, total=len(blocks), kinds=block_kinds(blocks)))
    (OUT / STATS_FILE).write_text("\n".join(lines) + "\n", encoding="ascii")
    (OUT / SECTION_FILE).write_text(render_section(decks), encoding="ascii")
    print("\n".join(lines))


if __name__ == "__main__":
    main()
