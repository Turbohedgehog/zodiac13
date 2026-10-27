#!/usr/bin/env python3
"""Draws the hub station's pilot as text and writes its blueprint (docs/hub-station-plan.md).

The pilot is the atrium with the agora, the west spine, the bridge and the security wing opposite it, to check
the scale and the grid of the whole station. One character is 1 m (4 grid cells), as in generate_shard.py. The target atrium is
round and the spine bends; until station_blueprint.py can place oblique panels the atrium is an octagon
whose 45-degree corners are solid fill under wedge prisms (as hall A of the shard), and the spine is straight.
"""

from pathlib import Path
from typing import NamedTuple

from station_blueprint import (
    CELLS_PER_M, COUNTER, DOOR, FLOOR, MASS, SLAB_CELLS, SLOPE, TO_NEG_Y, TO_POS_Y, WALL, WINDOW, Deck, block,
    block_kinds, corner_blocks, door_boxes, fill_corners, lamp_blocks, mask_blocks, render_plan, spawn_blocks,
    wall_blocks, write_blueprint)

# ----- Output -------------------------------------------------------------------------------

OUT = Path(__file__).parent / "hub"
DECK_FILE = "deck-{number}.txt"
DECK_FILE_GLOB = "deck-*.txt"
STATS_FILE = "stats.txt"
BLUEPRINT_FILE = Path(__file__).parent.parent.parent / "assets" / "station" / "blueprints" / "hub.json"

DECK_HEADER = "Hub pilot, deck {number}: {name}. Floor at {floor} m above the agora."
DECK_LEGEND = (
    "1 char = 1 m (4 cells). # wall, = window, D door, _ railing, @ solid fill of the atrium's slanted corner.",
    "The atrium is an octagon here (the target is round); the spine is straight (the target bends 22 degrees).",
)
STATS_TITLE = "Hub pilot estimate (written by generate_hub.py)."
STATS_DECK = "Deck {number} ({name}): rooms {rooms}, doors {doors}, blocks {blocks}"
STATS_BLOCKS = "Blueprint blocks ({file}): {total} - {kinds}."
NEG_X = "FaceNegXUpPosZ"


class Room:
    AGORA = "AGORA"
    SPINE = "SPINE {side}{n}"
    CORRIDOR = "CORRIDOR"
    BRIDGE = "BRIDGE"
    SECURITY_LOBBY = "SECURITY LOBBY"
    ARMORY = "ARMORY"
    BRIG = "BRIG"
    OFFICE = "OFFICE {n}"
    BARRACKS = "BARRACKS {n}"
    COMMAND_LOBBY = "COMMAND LOBBY"
    MEETING = "MEETING ROOM"
    COMMS = "COMMS"
    CAPTAIN = "CAPTAIN"
    HEADS = "HEADS' ROOM"
    SPAWN = "SPAWN"


# ----- Geometry -----------------------------------------------------------------------------

NORTH_M = 26  # room ahead of the atrium (smaller y) for the security wing, which faces the bridge across the agora
WIDTH, DEPTH = 66, 70 + NORTH_M
DECK_PITCH_M = 3
DECK_PITCH_CELLS = DECK_PITCH_M * CELLS_PER_M
WALL_HEIGHT_CELLS = DECK_PITCH_CELLS - SLAB_CELLS
DECKS = 8
BRIDGE_DECK = 8  # the top deck is two decks high: the bridge looks out and down into the atrium
TOP_DECK_WALL_HEIGHT_CELLS = 2 * DECK_PITCH_CELLS - SLAB_CELLS
SPINE_DECKS = 4

# The atrium: an octagon 36 m across the flats, its corners cut by 10 m legs (steps of 4 cells). The
# outer box includes the east wall's cell, so all four corners are as long as the straight walls leave them.
ATRIUM = (28, 1 + NORTH_M, 64, 37 + NORTH_M)
LEG_M = 10
CORNER_STEPS = [(4, 4)] * LEG_M
ATRIUM_SPAN_X = (ATRIUM[0] + LEG_M, ATRIUM[2] - LEG_M)
ATRIUM_SPAN_Y = (ATRIUM[1] + LEG_M, ATRIUM[3] - LEG_M)
CORNER_NAMES = ("NW", "NE", "SW", "SE")

# The west spine: a corridor between two rows of rooms, open to the atrium.
SPINE = (1, 11 + NORTH_M, 28, 27 + NORTH_M)
SPINE_ROOM_X = (1, 10, 19)
SPINE_ROOM_LENGTH_M = 9
SPINE_ROW_Y = {"N": (11 + NORTH_M, 15 + NORTH_M), "S": (23 + NORTH_M, 27 + NORTH_M)}
SPINE_OPENING_Y = (16 + NORTH_M, 22 + NORTH_M)
SPINE_DOOR_ROWS = (19 + NORTH_M, 20 + NORTH_M)

# The bridge: a hall north of the atrium, glazed at its far end, entered through a gap in the atrium wall.
BRIDGE = (38, 37 + NORTH_M, 54, 67 + NORTH_M)
BRIDGE_OPENING_X = (42, 50)

# Wings off the atrium's flat sides, 16 m wide: a lobby at the atrium and two rooms behind it on each deck,
# open to the galleries. Security faces the bridge across the agora; the command wing sits under the bridge.
WING_LOBBY_DEPTH_M = 8
WING_OPENING_X = (42, 50)
WING_ROOM_SPLIT_X = 46
COMMAND_LENGTH_M = 22


class Wing(NamedTuple):
    box: tuple
    decks: range
    lobby: str
    rooms: dict  # deck -> (west room, east room)
    atrium_side: str  # which side of the box the atrium is on


WINGS = (
    Wing((38, 5, 54, ATRIUM[1]), range(1, 4), Room.SECURITY_LOBBY,
         {1: (Room.ARMORY, Room.BRIG), 2: (Room.OFFICE.format(n=1), Room.OFFICE.format(n=2)),
          3: (Room.BARRACKS.format(n=1), Room.BARRACKS.format(n=2))}, "S"),
    Wing((38, ATRIUM[3], 54, ATRIUM[3] + COMMAND_LENGTH_M), range(6, 8), Room.COMMAND_LOBBY,
         {6: (Room.MEETING, Room.COMMS), 7: (Room.CAPTAIN, Room.HEADS)}, "N"),
)

# Galleries: a ring slab on every floor above the agora, 4 m wide, open in the middle.
RING_INNER_RADIUS_CELLS = 56
OCTAGON_DIAGONAL = 2 ** 0.5
RAIL_X = (40, 52)
RAIL_Y = (13 + NORTH_M, 25 + NORTH_M)
INNER_X = (32, 60)
INNER_Y = (5 + NORTH_M, 33 + NORTH_M)
FORECOURT = (40, 29 + NORTH_M, 52, 37 + NORTH_M)  # a balcony in the atrium before the bridge door

# Flights: one per floor, round the atrium clockwise on the four straight sides, 12 m long and 15 cells
# wide against the wall. Each: (side, deck the flight starts on).
NORTH_CELLS = NORTH_M * CELLS_PER_M
FLIGHT_SIDES = ("E", "S", "W", "N")
FLIGHT_WEDGES = 3
FLIGHT_RUN_CELLS = 16
FLIGHT_WIDTH_CELLS = 15
FLIGHT_RISE_CELLS = 4
WALL_LINE_CELLS = {side: cell for side, cell in
                   (("N", ATRIUM[1] * CELLS_PER_M), ("S", ATRIUM[3] * CELLS_PER_M),
                    ("W", ATRIUM[0] * CELLS_PER_M), ("E", ATRIUM[2] * CELLS_PER_M))}
FLIGHT_ALONG = {"E": (48 + NORTH_CELLS, 96 + NORTH_CELLS), "S": (152, 200), "W": (48 + NORTH_CELLS, 96 + NORTH_CELLS),
                "N": (152, 200)}

PODIUM = (172, 64 + NORTH_CELLS, 24)  # x, y, size in cells
PODIUM_LAYERS = 4
SPAWN_ROOM = (34, 17 + NORTH_M, 38, 21 + NORTH_M)


def outer_box(box):
    """The box in cells with its far end exclusive: the outer faces of its wall lines."""
    x0, y0, x1, y1 = (c * CELLS_PER_M for c in box)
    return x0, y0, x1 + 1, y1 + 1


def deck_floor_z(deck_number):
    return (deck_number - 1) * DECK_PITCH_CELLS + SLAB_CELLS


def wall_height(deck_number):
    return TOP_DECK_WALL_HEIGHT_CELLS if deck_number == BRIDGE_DECK else WALL_HEIGHT_CELLS


CORNERS = [(name, outer_box(ATRIUM), CORNER_STEPS) for name in CORNER_NAMES]


# ----- Drawing ------------------------------------------------------------------------------

class HubDeck(Deck):
    def __init__(self, number, name):
        super().__init__(number, name, WIDTH, DEPTH)
        self.blocks = []

    def atrium_walls(self):
        x0, y0, x1, y1 = ATRIUM
        for x in range(ATRIUM_SPAN_X[0], ATRIUM_SPAN_X[1] + 1):
            self.put(x, y0, WALL)
            self.put(x, y1, WALL)
        for y in range(ATRIUM_SPAN_Y[0], ATRIUM_SPAN_Y[1] + 1):
            self.put(x0, y, WALL)
            self.put(x1, y, WALL)
        self.rooms.append(Room.AGORA)
        self.rects.append((*ATRIUM, Room.AGORA))
        self.label(*ATRIUM, Room.AGORA if self.number == 1 else f"ATRIUM {self.number}")

    def railing(self):
        """The ring's inner edge on the four straight sides; on the bridge deck the south side follows
        the forecourt's edge instead."""
        forecourt = self.number == BRIDGE_DECK
        for x in range(RAIL_X[0], RAIL_X[1] + 1):
            self.put(x, INNER_Y[0], COUNTER)
            self.put(x, FORECOURT[1] if forecourt else INNER_Y[1], COUNTER)
        for y in range(RAIL_Y[0], RAIL_Y[1] + 1):
            self.put(INNER_X[0], y, COUNTER)
            self.put(INNER_X[1], y, COUNTER)
        if forecourt:
            for y in range(FORECOURT[1] + 1, INNER_Y[1]):
                for x in (FORECOURT[0], FORECOURT[2]):
                    self.put(x, y, COUNTER)

    def spine(self):
        x0, _, x1, _ = SPINE
        for side, (y0, y1) in SPINE_ROW_Y.items():
            for n, x in enumerate(SPINE_ROOM_X, 1):
                self.room(x, y0, x + SPINE_ROOM_LENGTH_M, y1, Room.SPINE.format(side=side, n=n),
                          ("S" if side == "N" else "N", None))
        for y in range(SPINE_OPENING_Y[0], SPINE_OPENING_Y[1] + 1):
            self.put(x0, y, WALL)
            self.put(x1, y, " ")
        for y in SPINE_DOOR_ROWS:
            self.put(x0, y, DOOR)
        self.label(x0, SPINE_ROW_Y["N"][1], x1, SPINE_ROW_Y["S"][0], Room.CORRIDOR)

    def wing(self, wing):
        x0, y0, x1, y1 = wing.box
        toward_south = wing.atrium_side == "S"
        lobby_edge = y1 - WING_LOBBY_DEPTH_M if toward_south else y0 + WING_LOBBY_DEPTH_M
        lobby = (x0, lobby_edge, x1, y1) if toward_south else (x0, y0, x1, lobby_edge)
        rooms_y = (y0, lobby_edge) if toward_south else (lobby_edge, y1)
        self.room(*lobby, wing.lobby)
        west, east = wing.rooms[self.number]
        door = (wing.atrium_side, None)
        self.room(x0, rooms_y[0], WING_ROOM_SPLIT_X, rooms_y[1], west, door)
        self.room(WING_ROOM_SPLIT_X, rooms_y[0], x1, rooms_y[1], east, door)
        for x in range(WING_OPENING_X[0], WING_OPENING_X[1] + 1):
            self.put(x, y1 if toward_south else y0, " ")

    def bridge(self):
        x0, y0, x1, y1 = BRIDGE
        self.rect(*BRIDGE)
        for x in range(x0 + 1, x1):
            self.put(x, y1, WINDOW)
        for x in range(BRIDGE_OPENING_X[0], BRIDGE_OPENING_X[1] + 1):
            self.put(x, y0, " ")
        self.rooms.append(Room.BRIDGE)
        self.rects.append((*BRIDGE, Room.BRIDGE))
        self.label(*BRIDGE, Room.BRIDGE)


def build_decks():
    decks = []
    for number in range(1, DECKS + 1):
        deck = HubDeck(number, "Agora" if number == 1 else f"Atrium level {number}")
        fill_corners(deck, CORNERS)
        deck.atrium_walls()
        if number > 1:
            deck.railing()
        if number <= SPINE_DECKS:
            deck.spine()
        for wing in WINGS:
            if number in wing.decks:
                deck.wing(wing)
        if number == BRIDGE_DECK:
            deck.bridge()
            deck.name = "Atrium top and bridge"
        decks.append(deck)
    decks[0].rects.append((*SPAWN_ROOM, Room.SPAWN))
    decks[0].spawn_rooms = (Room.SPAWN,)
    return decks


# ----- Blueprint ----------------------------------------------------------------------------

def inside_ring(cell_x, cell_y):
    """Whether a cell is in the atrium's inner octagon, the open middle over the agora."""
    centre_x = (outer_box(ATRIUM)[0] + outer_box(ATRIUM)[2]) / 2
    centre_y = (outer_box(ATRIUM)[1] + outer_box(ATRIUM)[3]) / 2
    dx, dy = abs(cell_x + 0.5 - centre_x), abs(cell_y + 0.5 - centre_y)
    return (dx <= RING_INNER_RADIUS_CELLS and dy <= RING_INNER_RADIUS_CELLS
            and dx + dy <= RING_INNER_RADIUS_CELLS * OCTAGON_DIAGONAL)


def flight(side, number):
    """The blocks of flight `number` (from deck `number` to the next): its wedges, and its footprint box."""
    z0 = deck_floor_z(number)
    start, end = FLIGHT_ALONG[side]
    wall = WALL_LINE_CELLS[side]
    blocks = []
    for i in range(FLIGHT_WEDGES):
        rise = z0 + i * FLIGHT_RISE_CELLS
        size = (FLIGHT_RUN_CELLS, FLIGHT_WIDTH_CELLS, FLIGHT_RISE_CELLS)
        if side == "E":
            blocks.append(block(SLOPE, (wall - FLIGHT_WIDTH_CELLS, start + i * FLIGHT_RUN_CELLS, rise), size, TO_POS_Y))
        elif side == "W":
            blocks.append(block(SLOPE, (wall + 1, end - (i + 1) * FLIGHT_RUN_CELLS, rise), size, TO_NEG_Y))
        elif side == "S":
            blocks.append(block(SLOPE, (end - (i + 1) * FLIGHT_RUN_CELLS, wall - FLIGHT_WIDTH_CELLS, rise), size, NEG_X))
        else:
            blocks.append(block(SLOPE, (start + i * FLIGHT_RUN_CELLS, wall + 1, rise), size))
    along = (start, end)
    if side in "EW":
        footprint = ((wall - FLIGHT_WIDTH_CELLS if side == "E" else wall + 1), along[0],
                     (wall if side == "E" else wall + FLIGHT_WIDTH_CELLS + 1), along[1])
    else:
        footprint = (along[0], (wall - FLIGHT_WIDTH_CELLS if side == "S" else wall + 1),
                     along[1], (wall if side == "S" else wall + FLIGHT_WIDTH_CELLS + 1))
    return blocks, footprint


def flights():
    found = []
    for number in range(1, DECKS):
        side = FLIGHT_SIDES[(number - 1) % len(FLIGHT_SIDES)]
        blocks, footprint = flight(side, number)
        found.append((number, blocks, footprint))
    return found


def slab_mask(index):
    """The slab under deck `index + 1` (the roof for the top deck's): its footprint, open over flights."""
    x0, y0, x1, y1 = outer_box(ATRIUM)
    mask = [[False] * (WIDTH * CELLS_PER_M) for _ in range(DEPTH * CELLS_PER_M)]

    def fill(box):
        for y in range(box[1], box[3]):
            for x in range(box[0], box[2]):
                mask[y][x] = True

    fill(outer_box(ATRIUM))
    if 1 <= index < DECKS:  # the roof has no opening over the agora
        for y in range(y0, y1):
            for x in range(x0, x1):
                if inside_ring(x, y):
                    mask[y][x] = False
    if index <= SPINE_DECKS:
        fill(outer_box(SPINE))
    for wing in WINGS:
        if index in range(wing.decks.start - 1, wing.decks.stop):
            fill(outer_box(wing.box))
    if index == BRIDGE_DECK - 1 or index == DECKS:
        fill(outer_box(BRIDGE))
    if index == BRIDGE_DECK - 1:
        fill(outer_box(FORECOURT))
    for number, _, (fx0, fy0, fx1, fy1) in flights():
        if number == index:
            for y in range(fy0, fy1):
                for x in range(fx0, fx1):
                    mask[y][x] = False
    return mask


def slab_blocks():
    blocks = []
    for index in range(DECKS + 1):
        blocks += mask_blocks(slab_mask(index), index * DECK_PITCH_CELLS if index < DECKS else
                              (DECKS - 1) * DECK_PITCH_CELLS + SLAB_CELLS + TOP_DECK_WALL_HEIGHT_CELLS, FLOOR)
    return blocks


def podium_blocks():
    x, y, size = PODIUM
    return [block(FLOOR, (x, y, deck_floor_z(1) + layer), (size, size, SLAB_CELLS)) for layer in range(PODIUM_LAYERS)]


def corner_layers(z, height):
    """The corners' wedge prisms and wall rows, in layers no taller than the wedge limit."""
    blocks = []
    layer_height = height
    while layer_height > 16:
        layer_height = (height + 1) // 2
    for layer in range(0, height, layer_height):
        top = min(layer_height, height - layer)
        blocks += [b for name, outer, steps in CORNERS for b in corner_blocks(name, outer, steps, z + layer, top)]
    return blocks


def station_blocks(decks):
    blocks = slab_blocks() + podium_blocks()
    for _, flight_blocks, _ in flights():
        blocks += flight_blocks
    for deck in decks:
        z = deck_floor_z(deck.number)
        height = wall_height(deck.number)
        deck.blocks = wall_blocks(deck.grid, z, wall_height=height) + corner_layers(z, height)
        deck.blocks += spawn_blocks(deck, z)
        blocks += deck.blocks
    for deck in decks:
        lamps = lamp_blocks(deck, deck_floor_z(deck.number), blocks, wall_height(deck.number))
        deck.blocks += lamps
        blocks += lamps
    return blocks


# ----- Text ---------------------------------------------------------------------------------

def render(deck):
    header = [DECK_HEADER.format(number=deck.number, name=deck.name, floor=(deck.number - 1) * DECK_PITCH_M),
              *DECK_LEGEND]
    return render_plan(deck, header)


def main():
    decks = build_decks()
    blocks = station_blocks(decks)
    OUT.mkdir(exist_ok=True)
    for old in OUT.glob(DECK_FILE_GLOB):
        old.unlink()
    lines = [STATS_TITLE, ""]
    for deck in decks:
        (OUT / DECK_FILE.format(number=deck.number)).write_text(render(deck), encoding="ascii", newline="\n")
        lines.append(STATS_DECK.format(number=deck.number, name=deck.name, rooms=len(deck.rooms),
                                       doors=len(door_boxes(deck.grid)), blocks=len(deck.blocks)))
    write_blueprint(BLUEPRINT_FILE, blocks)
    lines.append("")
    lines.append(STATS_BLOCKS.format(file=BLUEPRINT_FILE.name, total=len(blocks), kinds=block_kinds(blocks)))
    (OUT / STATS_FILE).write_text("\n".join(lines) + "\n", encoding="ascii", newline="\n")
    print("\n".join(lines))


if __name__ == "__main__":
    main()
