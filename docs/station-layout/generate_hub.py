#!/usr/bin/env python3
"""Draws the hub station's pilot as text and writes its blueprint (docs/hub-station-plan.md).

The pilot is the atrium with the agora, the west spine, the bridge and the security wing opposite it, to check
the scale and the grid of the whole station. One character is 1 m (4 grid cells), as in generate_shard.py. The target atrium is
round and the spine bends; the atrium is a 16-gon of oblique panels (wall_panel_block) between four straight
flats, and the spine is straight until its bend is drawn.
"""

import functools
import math
from pathlib import Path
from typing import NamedTuple

from station_blueprint import (
    CELLS_PER_M, COUNTER, DOOR, FLOOR, MASS, SLAB_CELLS, SLOPE, TO_NEG_Y, TO_POS_Y, WALL, WINDOW, Deck, block,
    block_kinds, door_boxes, lamp_blocks, mask_blocks, render_plan, spawn_blocks, wall_blocks, wall_panel_block,
    write_blueprint)

# ----- Output -------------------------------------------------------------------------------

OUT = Path(__file__).parent / "hub"
DECK_FILE = "deck-{number}.txt"
DECK_FILE_GLOB = "deck-*.txt"
STATS_FILE = "stats.txt"
BLUEPRINT_FILE = Path(__file__).parent.parent.parent / "assets" / "station" / "blueprints" / "hub.json"

DECK_HEADER = "Hub pilot, deck {number}: {name}. Floor at {floor} m above the agora."
DECK_LEGEND = (
    "1 char = 1 m (4 cells). # wall, = window, D door, _ railing, @ solid fill of the atrium's slanted corner.",
    "The atrium is a 16-gon of oblique panels (drawn as @ outside it); the spine is straight (the target bends 22 degrees).",
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
    HANGAR = "DEPARTURE HANGAR"
    SPAWN = "SPAWN"


# ----- Geometry -----------------------------------------------------------------------------

SECURITY_LENGTH_M = 22
HANGAR_LENGTH_M = 30
# Room ahead of the atrium (smaller y) for the security wing, which faces the bridge across the agora, and the
# departure hangar behind it.
NORTH_M = 4 + SECURITY_LENGTH_M + HANGAR_LENGTH_M
WIDTH, DEPTH = 66, 70 + NORTH_M
DECK_PITCH_M = 3
DECK_PITCH_CELLS = DECK_PITCH_M * CELLS_PER_M
WALL_HEIGHT_CELLS = DECK_PITCH_CELLS - SLAB_CELLS
DECKS = 8
BRIDGE_DECK = 8  # the top deck is two decks high: the bridge looks out and down into the atrium
TOP_DECK_WALL_HEIGHT_CELLS = 2 * DECK_PITCH_CELLS - SLAB_CELLS
SPINE_DECKS = 4

# The atrium: a 16-gon 36 m across the flats, the target round hall. Its four straight flats (8 m, grid walls with
# the entrances) are joined by three oblique panels per quadrant. The outer box includes the east wall's cell.
ATRIUM = (28, 1 + NORTH_M, 64, 37 + NORTH_M)
FLAT_HALF_M = 4
ATRIUM_SPAN_X = ((ATRIUM[0] + ATRIUM[2]) // 2 - FLAT_HALF_M, (ATRIUM[0] + ATRIUM[2]) // 2 + FLAT_HALF_M)
ATRIUM_SPAN_Y = ((ATRIUM[1] + ATRIUM[3]) // 2 - FLAT_HALF_M, (ATRIUM[1] + ATRIUM[3]) // 2 + FLAT_HALF_M)
# One quadrant's panel nodes as offsets from the hall's middle, in half cells, from the +x flat round to the +y
# flat: the flats' inner faces are 71.5 cells away and end 16.5 from the axis, and two more nodes on the
# circle through those ends give three panels of equal turn.
QUADRANT_NODES_HALF_CELLS = ((143, 33), (121, 83), (83, 121), (33, 143))
PANEL_THICKNESS_CELLS = 2  # the least that holds air back on every one of the 22-45-degree panels
SLAB_OVERHANG_CELLS = 2.5  # the slab reaches under the panels

# The west spine: a corridor between two rows of rooms, open to the atrium.
SPINE = (0, 11 + NORTH_M, 28, 27 + NORTH_M)  # its rooms end a character short of the atrium's flat
SPINE_ROOM_X = (0, 9, 18)
SPINE_ROOM_LENGTH_M = 9
SPINE_ROW_Y = {"N": (11 + NORTH_M, 15 + NORTH_M), "S": (23 + NORTH_M, 27 + NORTH_M)}
SPINE_OPENING_Y = (16 + NORTH_M, 22 + NORTH_M)
SPINE_DOOR_ROWS = (19 + NORTH_M, 20 + NORTH_M)

# The bridge: a hall north of the atrium, glazed at its far end, entered through a gap in the atrium wall.
BRIDGE = (38, 38 + NORTH_M, 54, 68 + NORTH_M)  # a vestibule's width from the atrium

# Wings off the atrium's flat sides, 16 m wide: a lobby at the atrium and two rooms behind it on each deck,
# open to the galleries. Security faces the bridge across the agora; the command wing sits under the bridge.
WING_LOBBY_DEPTH_M = 8
WING_OPENING_X = (43, 49)
WING_JAMB_X = (42, 50)  # the vestibule's side walls between a wing and the atrium's flat
WING_ROOM_SPLIT_X = 46
COMMAND_LENGTH_M = 22


# The departure hangar behind the security wing: two decks tall as one hall, windows along its far wall.
DEPARTURE = (32, 4, 60, HANGAR_LENGTH_M + 4)
HANGAR_DECKS = range(1, 3)


class Wing(NamedTuple):
    box: tuple
    decks: range
    lobby: str
    rooms: dict  # deck -> (west room, east room)
    atrium_side: str  # which side of the box the atrium is on
    rear_door_decks: tuple = ()  # decks on which both rooms also have a door on the far side


WINGS = (
    Wing((38, HANGAR_LENGTH_M + 4, 54, ATRIUM[1] - 1), range(1, 4), Room.SECURITY_LOBBY,
         {1: (Room.ARMORY, Room.BRIG), 2: (Room.OFFICE.format(n=1), Room.OFFICE.format(n=2)),
          3: (Room.BARRACKS.format(n=1), Room.BARRACKS.format(n=2))}, "S", rear_door_decks=(1,)),
    Wing((38, ATRIUM[3] + 1, 54, ATRIUM[3] + 1 + COMMAND_LENGTH_M), range(6, 8), Room.COMMAND_LOBBY,
         {6: (Room.MEETING, Room.COMMS), 7: (Room.CAPTAIN, Room.HEADS)}, "N"),
)

# Galleries: a ring slab on every floor above the agora, 4 m wide, open in the middle.
RING_INNER_RADIUS_CELLS = 56
OCTAGON_DIAGONAL = 2 ** 0.5
RAIL_X = (40, 52)
RAIL_Y = (13 + NORTH_M, 25 + NORTH_M)
INNER_X = (32, 60)
INNER_Y = (5 + NORTH_M, 33 + NORTH_M)
FORECOURT = (43, 29 + NORTH_M, 49, 37 + NORTH_M)  # a balcony in the atrium before the bridge door, as wide as its flat

# Flights: one per floor, round the atrium clockwise on the four sides, 12 m long and 11 cells wide, a few
# cells off the flat so their ends clear the oblique panels. Each: (side, deck the flight starts on).
NORTH_CELLS = NORTH_M * CELLS_PER_M
FLIGHT_SIDES = ("E", "S", "W", "N")
FLIGHT_WEDGES = 3
FLIGHT_RUN_CELLS = 16
FLIGHT_WIDTH_CELLS = 11
FLIGHT_WALL_GAP_CELLS = 4
FLIGHT_RISE_CELLS = 4
WALL_LINE_CELLS = {side: cell for side, cell in
                   (("N", ATRIUM[1] * CELLS_PER_M), ("S", ATRIUM[3] * CELLS_PER_M),
                    ("W", ATRIUM[0] * CELLS_PER_M), ("E", ATRIUM[2] * CELLS_PER_M))}
FLIGHT_ALONG = {"E": (156, 204), "S": (161, 209), "W": (156, 204), "N": (161, 209)}  # centred on the hall

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


def atrium_centre2():
    """The hall's middle in half cells (x, y)."""
    x0, y0, x1, y1 = outer_box(ATRIUM)
    return x0 + x1, y0 + y1


def atrium_offsets():
    """The 16 nodes of the atrium's inner face as offsets from its middle in half cells, counter-clockwise from
    the +x flat (y grows towards the south on the plan, so this runs clockwise on the page)."""
    offsets = []
    quadrant = list(QUADRANT_NODES_HALF_CELLS)
    for _ in range(4):
        offsets += quadrant
        quadrant = [(-dy, dx) for dx, dy in quadrant]
    return offsets


def atrium_nodes():
    cx2, cy2 = atrium_centre2()
    return [((cx2 + dx) // 2, (cy2 + dy) // 2) for dx, dy in atrium_offsets()]


def atrium_panels(z, height):
    """The oblique panels of one deck: every side of the 16-gon but the four flats, thickened outwards."""
    offsets, nodes = atrium_offsets(), atrium_nodes()
    blocks = []
    for i, (a, b) in enumerate(zip(nodes, nodes[1:] + nodes[:1])):
        if i % len(QUADRANT_NODES_HALF_CELLS) == len(QUADRANT_NODES_HALF_CELLS) - 1:
            continue  # a flat, a grid wall
        major, minor = (0, 1) if abs(b[0] - a[0]) >= abs(b[1] - a[1]) else (1, 0)
        mid = (offsets[i][minor] + offsets[(i + 1) % len(offsets)][minor])
        blocks.append(wall_panel_block((*a, z), (*b, z), PANEL_THICKNESS_CELLS * (1 if mid > 0 else -1), height))
    return blocks


@functools.cache
def atrium_polygon(margin):
    """The 16-gon's corners in cells, grown outwards by about `margin` cells."""
    cx, cy = (c / 2 for c in atrium_centre2())
    return [(cx + px * (1 + margin / math.hypot(px, py)), cy + py * (1 + margin / math.hypot(px, py)))
            for px, py in ((dx / 2, dy / 2) for dx, dy in atrium_offsets())]


def inside_atrium(x, y, margin=0.0):
    """Whether the point (cells) is inside the 16-gon grown outwards by about `margin` cells."""
    polygon = atrium_polygon(margin)
    sides = [(bx - ax) * (y - ay) - (by - ay) * (x - ax) for (ax, ay), (bx, by) in zip(polygon, polygon[1:] + polygon[:1])]
    return all(side >= 0 for side in sides) or all(side <= 0 for side in sides)


# ----- Drawing ------------------------------------------------------------------------------

class HubDeck(Deck):
    def __init__(self, number, name):
        super().__init__(number, name, WIDTH, DEPTH)
        self.blocks = []

    def atrium_walls(self):
        x0, y0, x1, y1 = ATRIUM
        self.fill_outside()
        for x in range(ATRIUM_SPAN_X[0], ATRIUM_SPAN_X[1] + 1):
            self.put(x, y0, WALL)
            self.put(x, y1, WALL)
        for y in range(ATRIUM_SPAN_Y[0], ATRIUM_SPAN_Y[1] + 1):
            self.put(x0, y, WALL)
            self.put(x1, y, WALL)
        self.rooms.append(Room.AGORA)
        self.rects.append((*ATRIUM, Room.AGORA))
        self.label(*ATRIUM, Room.AGORA if self.number == 1 else f"ATRIUM {self.number}")

    def fill_outside(self):
        """Marks the characters of the atrium's box that are not wholly inside the hall as solid fill, so the
        hall's free area is closed for the lamps."""
        x0, y0, x1, y1 = ATRIUM
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                corners = [(cx, cy) for cx in (x * CELLS_PER_M, x * CELLS_PER_M + CELLS_PER_M)
                           for cy in (y * CELLS_PER_M, y * CELLS_PER_M + CELLS_PER_M)]
                if self.grid[y][x] == " " and not all(inside_atrium(cx, cy) for cx, cy in corners):
                    self.put(x, y, MASS)

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
        self.vestibule(y1 if toward_south else y0, ATRIUM[1] if toward_south else ATRIUM[3])
        if self.number in wing.rear_door_decks:
            rear = "N" if toward_south else "S"
            self.door_on(x0, rooms_y[0], WING_ROOM_SPLIT_X, rooms_y[1], rear)
            self.door_on(WING_ROOM_SPLIT_X, rooms_y[0], x1, rooms_y[1], rear)

    def vestibule(self, wing_row, atrium_row):
        """Joins an entrance in a wing's row to the opening in the atrium's flat, a character away."""
        for y in range(min(wing_row, atrium_row), max(wing_row, atrium_row) + 1):
            for x in WING_JAMB_X:
                self.put(x, y, WALL)
        for y in (wing_row, atrium_row):
            for x in range(WING_OPENING_X[0], WING_OPENING_X[1] + 1):
                self.put(x, y, " ")

    def hangar(self):
        x0, y0, x1, y1 = DEPARTURE
        self.room(x0, y0, x1, y1, Room.HANGAR)
        for x in range(x0 + 1, x1):
            self.put(x, y0, WINDOW)

    def bridge(self):
        x0, y0, x1, y1 = BRIDGE
        self.rect(*BRIDGE)
        for x in range(x0 + 1, x1):
            self.put(x, y1, WINDOW)
        self.vestibule(y0, ATRIUM[3])
        self.rooms.append(Room.BRIDGE)
        self.rects.append((*BRIDGE, Room.BRIDGE))
        self.label(*BRIDGE, Room.BRIDGE)


def build_decks():
    decks = []
    for number in range(1, DECKS + 1):
        deck = HubDeck(number, "Agora" if number == 1 else f"Atrium level {number}")
        deck.atrium_walls()
        if number > 1:
            deck.railing()
        if number <= SPINE_DECKS:
            deck.spine()
        if number in HANGAR_DECKS:
            deck.hangar()
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
            blocks.append(block(SLOPE, (wall - FLIGHT_WALL_GAP_CELLS - FLIGHT_WIDTH_CELLS, start + i * FLIGHT_RUN_CELLS, rise), size,
                                TO_POS_Y))
        elif side == "W":
            blocks.append(block(SLOPE, (wall + 1 + FLIGHT_WALL_GAP_CELLS, end - (i + 1) * FLIGHT_RUN_CELLS, rise), size,
                                TO_NEG_Y))
        elif side == "S":
            blocks.append(block(SLOPE, (end - (i + 1) * FLIGHT_RUN_CELLS, wall - FLIGHT_WALL_GAP_CELLS - FLIGHT_WIDTH_CELLS,
                                         rise), size, NEG_X))
        else:
            blocks.append(block(SLOPE, (start + i * FLIGHT_RUN_CELLS, wall + 1 + FLIGHT_WALL_GAP_CELLS, rise), size))
    along = (start, end)
    if side in "EW":
        low = wall - FLIGHT_WALL_GAP_CELLS - FLIGHT_WIDTH_CELLS if side == "E" else wall + 1 + FLIGHT_WALL_GAP_CELLS
        footprint = (low, along[0], low + FLIGHT_WIDTH_CELLS, along[1])
    else:
        low = wall - FLIGHT_WALL_GAP_CELLS - FLIGHT_WIDTH_CELLS if side == "S" else wall + 1 + FLIGHT_WALL_GAP_CELLS
        footprint = (along[0], low, along[1], low + FLIGHT_WIDTH_CELLS)
    return blocks, footprint


def flights():
    found = []
    for number in range(1, DECKS):
        side = FLIGHT_SIDES[(number - 1) % len(FLIGHT_SIDES)]
        blocks, footprint = flight(side, number)
        found.append((number, blocks, footprint))
    return found


def vestibule_box(atrium_side):
    """The characters between a wing's row and the atrium's flat, between the jambs."""
    row = (ATRIUM[1] - 1, ATRIUM[1]) if atrium_side == "S" else (ATRIUM[3], ATRIUM[3] + 1)
    return WING_JAMB_X[0], row[0], WING_JAMB_X[1], row[1]


def slab_mask(index):
    """The slab under deck `index + 1` (the roof for the top deck's): its footprint, open over flights."""
    x0, y0, x1, y1 = outer_box(ATRIUM)
    mask = [[False] * (WIDTH * CELLS_PER_M) for _ in range(DEPTH * CELLS_PER_M)]

    def fill(box):
        for y in range(box[1], box[3]):
            for x in range(box[0], box[2]):
                mask[y][x] = True

    for y in range(y0, y1):
        for x in range(x0, x1):
            mask[y][x] = inside_atrium(x + 0.5, y + 0.5, SLAB_OVERHANG_CELLS)
    if 1 <= index < DECKS:  # the roof has no opening over the agora
        for y in range(y0, y1):
            for x in range(x0, x1):
                if inside_ring(x, y):
                    mask[y][x] = False
    if index <= SPINE_DECKS:
        fill(outer_box(SPINE))
    if index in (HANGAR_DECKS.start - 1, HANGAR_DECKS.stop - 1):  # no slab inside the hall
        fill(outer_box(DEPARTURE))
    for wing in WINGS:
        if index in range(wing.decks.start - 1, wing.decks.stop):
            fill(outer_box(wing.box))
            fill(outer_box(vestibule_box(wing.atrium_side)))
    if index == BRIDGE_DECK - 1 or index == DECKS:
        fill(outer_box(BRIDGE))
        fill(outer_box(vestibule_box("N")))
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


def station_blocks(decks):
    blocks = slab_blocks() + podium_blocks()
    for _, flight_blocks, _ in flights():
        blocks += flight_blocks
    for deck in decks:
        z = deck_floor_z(deck.number)
        height = wall_height(deck.number)
        deck.blocks = wall_blocks(deck.grid, z, wall_height=height) + atrium_panels(z, height)
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
