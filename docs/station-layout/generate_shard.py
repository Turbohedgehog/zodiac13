#!/usr/bin/env python3
"""Draws the shard station's decks as text and writes its blueprint.

A small fragment of the future big station (docs/skew-station-plan.md): a cabin section on the
grid, an octagonal hall with 45-degree walls, a second hall whose walls run 2:3, ramps at 1:4
between three decks and a sloped ceiling. One character is 1 m (4 grid cells), as in
generate_spire.py. The halls' corners are solid fill under wedge prisms: exact slanted faces
until the thin oblique walls of f/wall-segments exist.
"""

from pathlib import Path

from station_blueprint import (
    CELLS_PER_M, COUNTER, DOOR, FLOOR, MASS, SLAB_CELLS, SLOPE, WALL, WALL_BLOCK, WINDOW, Deck, block,
    block_kinds, corner_blocks, door_boxes, fill_corners, lamp_blocks, mask_blocks, render_plan, spawn_blocks,
    wall_blocks, wedge_block, write_blueprint)

# ----- Output -------------------------------------------------------------------------------

OUT = Path(__file__).parent / "shard"
DECK_FILE = "deck-{number}.txt"
DECK_FILE_GLOB = "deck-*.txt"
STATS_FILE = "stats.txt"
BLUEPRINT_FILE = Path(__file__).parent.parent.parent / "assets" / "station" / "blueprints" / "shard.json"

DECK_HEADER = "Shard, deck {number}: {name}. Floor at {floor} m."
DECK_LEGEND = (
    "1 char = 1 m (4 cells). # wall, = window, D door, _ railing, @ solid fill of a hall's slanted corner.",
    "Halls A and B are cut from the deck boxes 20-44 and 44-68 m; their corners are filled up to a slanted face.",
)
STATS_TITLE = "Shard station estimate (written by generate_shard.py)."
STATS_DECK = "Deck {number} ({name}): rooms {rooms}, doors {doors}, blocks {blocks}"
STATS_BLOCKS = "Blueprint blocks ({file}): {total} - {kinds}."


class Room:
    CABIN = "CABIN-{n}"
    CORRIDOR = "CORRIDOR"
    CARGO_A = "CARGO A"
    CARGO_B = "CARGO B"
    HALL_A = "HALL A"
    HALL_B = "HALL B"
    LOFT = "LOFT"
    OBSERVATORY = "OBSERVATORY"


# ----- Geometry -----------------------------------------------------------------------------

WIDTH, DEPTH = 69, 41
DECK_PITCH_M = 3
DECK_PITCH_CELLS = DECK_PITCH_M * CELLS_PER_M
WALL_HEIGHT_CELLS = DECK_PITCH_CELLS - SLAB_CELLS
RAMP_RISE_CELLS = 4
RAMP_RUN_CELLS = 16  # 1:4
RAMP_WEDGES = DECK_PITCH_CELLS // RAMP_RISE_CELLS

# Boxes in meters (x0, y0, x1, y1).
WEST = (0, 12, 20, 28)
BOX_A = (20, 4, 44, 36)
BOX_B = (44, 4, 68, 36)
CABIN_WIDTH_M = 5
CORRIDOR = (18, 22)
DOOR_ROWS = (19, 20)

# A hall's corner is a triangle filled by steps (along x, along y) in cells, each a wedge prism on
# the slanted face over a box; the steps add up to the triangle's legs. A wall is one cell thick on
# its line, so a hall's east corners are one cell shorter than its west ones: the last step is 3.
HALL_A_WEST_STEPS = [(4, 4)] * 7
HALL_A_EAST_STEPS = [(4, 4)] * 6 + [(3, 3)]
HALL_B_STEPS = [(2, 3)] * 12
HALL_A_SPAN = (27, 37)  # the straight north and south walls, x
HALL_B_SPAN = (50, 62)
HALL_A_WEST = (11, 29)  # the straight west and east walls, y
HALL_A_EAST = (13, 27)  # the column shared with hall B belongs to B's filled corners above and below the door wall
HALL_B_SIDE = (13, 27)
HALL_B_WINDOWS = (16, 24)

# A ramp climbs along +x over three wedges and ends where the deck above begins.
RAMP_A = {"x": 96, "y": 49, "width": 15, "deck": 1}
RAMP_B = {"x": 208, "y": 49, "width": 15, "deck": 2}
RAMPS = (RAMP_A, RAMP_B)
RAIL_Y = (12, 16)  # the ramp opening's railing lines, m

LOFT_RISE_CELLS = 4
LOFT_WIDTH_CELLS = 16  # the sloped ceiling's run from each side wall, 1:4


def outer_box(box):
    """The box in cells with its far end exclusive: the outer faces of its wall lines."""
    x0, y0, x1, y1 = (c * CELLS_PER_M for c in box)
    return x0, y0, x1 + 1, y1 + 1


# The wall column at x = 44 m is shared: hall A's fill stops before it, hall B's starts on it.
HALL_A_OUTER = (*outer_box(BOX_A)[:2], BOX_A[2] * CELLS_PER_M, outer_box(BOX_A)[3])
HALL_B_OUTER = outer_box(BOX_B)
CORNER_NAMES = ("NW", "NE", "SW", "SE")
# Every filled corner: (name, the hall's outer box, steps).
CORNERS = (
    [(name, HALL_A_OUTER, HALL_A_WEST_STEPS if name[1] == "W" else HALL_A_EAST_STEPS) for name in CORNER_NAMES] +
    [(name, HALL_B_OUTER, HALL_B_STEPS) for name in CORNER_NAMES])


# ----- Drawing ------------------------------------------------------------------------------

class ShardDeck(Deck):
    def __init__(self, number, name):
        super().__init__(number, name, WIDTH, DEPTH)
        self.blocks = []

    def hall(self, box, label, span, west, east):
        """A hall: straight walls only where the corners don't cut them."""
        x0, y0, x1, y1 = box
        for x in range(span[0], span[1] + 1):
            self.put(x, y0, WALL)
            self.put(x, y1, WALL)
        for y in range(west[0], west[1] + 1):
            self.put(x0, y, WALL)
        for y in range(east[0], east[1] + 1):
            self.put(x1, y, WALL)
        self.rooms.append(label)
        self.rects.append((*box, label))
        self.label(*box, label)

    def mark_corners(self):
        fill_corners(self, CORNERS)

    def corridor_door(self, x):
        for y in DOOR_ROWS:
            self.put(x, y, DOOR)


def west_section(deck):
    """Eight cabins round an east-west corridor that ends in the hall wall at x = 20."""
    x0, y0, x1, y1 = WEST
    for x in range(x0, x1, CABIN_WIDTH_M):
        deck.room(x, y0, x + CABIN_WIDTH_M, CORRIDOR[0], Room.CABIN.format(n=deck.numbered("N")), ("S", None))
        deck.room(x, CORRIDOR[1], x + CABIN_WIDTH_M, y1, Room.CABIN.format(n=deck.numbered("S")), ("N", None))
    for y in range(CORRIDOR[0] + 1, CORRIDOR[1]):
        deck.put(x0, y, WALL)
    deck.label(x0, CORRIDOR[0], x1, CORRIDOR[1], Room.CORRIDOR)


def railing(deck, ramp):
    x = ramp["x"] // CELLS_PER_M
    for rail_y in RAIL_Y:
        for rail_x in range(x, x + RAMP_WEDGES * RAMP_RUN_CELLS // CELLS_PER_M + 1):
            deck.put(rail_x, rail_y, COUNTER)
    for rail_y in range(RAIL_Y[0], RAIL_Y[1] + 1):
        deck.put(x, rail_y, COUNTER)


def build_decks():
    """The three decks, bottom up."""
    cargo = ShardDeck(1, "Cargo and berths")
    hall = ShardDeck(2, "Halls")
    loft = ShardDeck(3, "Loft and observatory")

    for deck in (cargo, hall, loft):
        west_section(deck)
    cargo.room(*BOX_A, Room.CARGO_A)
    cargo.room(*BOX_B, Room.CARGO_B)
    loft.room(*BOX_A, Room.LOFT)
    loft.room(*BOX_B, Room.OBSERVATORY)

    hall.mark_corners()
    hall.hall(BOX_A, Room.HALL_A, HALL_A_SPAN, HALL_A_WEST, HALL_A_EAST)
    hall.hall(BOX_B, Room.HALL_B, HALL_B_SPAN, HALL_B_SIDE, HALL_B_SIDE)
    for y in range(HALL_B_WINDOWS[0], HALL_B_WINDOWS[1] + 1):
        hall.put(BOX_B[2], y, WINDOW)
    hall.spawn_rooms = (Room.HALL_A,)

    for deck in (cargo, hall, loft):
        deck.corridor_door(BOX_A[0])
        deck.corridor_door(BOX_A[2])
    railing(hall, RAMP_A)
    railing(loft, RAMP_B)
    return [cargo, hall, loft]


# ----- Blueprint ----------------------------------------------------------------------------

def deck_floor_z(deck):
    return (deck.number - 1) * DECK_PITCH_CELLS + SLAB_CELLS


def hall_corners(deck):
    z = deck_floor_z(deck)
    return [b for name, outer, steps in CORNERS for b in corner_blocks(name, outer, steps, z, WALL_HEIGHT_CELLS)]


def ramp_blocks(ramp):
    z = (ramp["deck"] - 1) * DECK_PITCH_CELLS + SLAB_CELLS
    return [block(SLOPE, (ramp["x"] + i * RAMP_RUN_CELLS, ramp["y"], z + i * RAMP_RISE_CELLS),
                  (RAMP_RUN_CELLS, ramp["width"], RAMP_RISE_CELLS)) for i in range(RAMP_WEDGES)]


def loft_ceiling_blocks(z):
    """Wedges hung under the roof along both long walls: the ceiling falls toward the wall, 1:4."""
    x0, y0, x1, y1 = outer_box(BOX_A)
    top = z + WALL_HEIGHT_CELLS
    blocks = []
    for x in range(x0 + 1, x1 - 1, LOFT_WIDTH_CELLS):
        x_end = min(x + LOFT_WIDTH_CELLS, x1 - 1)
        for wall, inner in ((y0 + 1, y0 + 1 + LOFT_WIDTH_CELLS), (y1 - 1, y1 - 1 - LOFT_WIDTH_CELLS)):
            vertices = [(px, py, pz) for px in (x, x_end)
                        for py, pz in ((wall, top - LOFT_RISE_CELLS), (wall, top), (inner, top))]
            blocks.append(wedge_block(vertices))
    return blocks


def slab_blocks(index):
    """The slab under deck `index + 1` (the roof for the last index): the whole footprint, open over the ramps."""
    boxes = [outer_box(box) for box in (WEST, BOX_A, BOX_B)]
    width, depth = max(b[2] for b in boxes), max(b[3] for b in boxes)
    mask = [[any(b[0] <= x < b[2] and b[1] <= y < b[3] for b in boxes) for x in range(width)] for y in range(depth)]
    for ramp in RAMPS:
        if ramp["deck"] == index:
            for y in range(ramp["y"], ramp["y"] + ramp["width"]):
                for x in range(ramp["x"] + 1, ramp["x"] + RAMP_WEDGES * RAMP_RUN_CELLS):
                    mask[y][x] = False
    return mask_blocks(mask, index * DECK_PITCH_CELLS, FLOOR)


def station_blocks(decks):
    blocks = []
    for index in range(len(decks) + 1):
        blocks += slab_blocks(index)
    for ramp in RAMPS:
        blocks += ramp_blocks(ramp)
    for deck in decks:
        z = deck_floor_z(deck)
        deck.blocks = wall_blocks(deck.grid, z, wall_height=WALL_HEIGHT_CELLS)
        if deck.number == 2:
            deck.blocks += hall_corners(deck)
        if deck.number == 3:
            deck.blocks += loft_ceiling_blocks(z)
        deck.blocks += spawn_blocks(deck, z)
        blocks += deck.blocks
    for deck in decks:
        lamps = lamp_blocks(deck, deck_floor_z(deck), blocks, WALL_HEIGHT_CELLS)
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
        (OUT / DECK_FILE.format(number=deck.number)).write_text(render(deck), encoding="ascii")
        lines.append(STATS_DECK.format(number=deck.number, name=deck.name, rooms=len(deck.rooms),
                                       doors=len(door_boxes(deck.grid)), blocks=len(deck.blocks)))
    write_blueprint(BLUEPRINT_FILE, blocks)
    lines.append("")
    lines.append(STATS_BLOCKS.format(file=BLUEPRINT_FILE.name, total=len(blocks), kinds=block_kinds(blocks)))
    (OUT / STATS_FILE).write_text("\n".join(lines) + "\n", encoding="ascii")
    print("\n".join(lines))


if __name__ == "__main__":
    main()
