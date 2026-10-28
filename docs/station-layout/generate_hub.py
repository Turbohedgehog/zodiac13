#!/usr/bin/env python3
"""Draws the hub station's pilot as text and writes its blueprint (docs/hub-station-plan.md).

The pilot is the atrium with the agora, the west spine, the bridge and the security wing opposite it, to check
the scale and the grid of the whole station. One character is 1 m (4 grid cells), as in generate_shard.py. The target atrium is
round and the spine bends; the atrium is a 16-gon of oblique panels (wall_panel_block) between four straight
flats, and the spine is straight until its bend is drawn.
"""

import functools
import math
from fractions import Fraction
from pathlib import Path
from typing import NamedTuple

from station_blueprint import (
    CELLS_PER_M, COUNTER, COUNTER_HEIGHT_CELLS, DOOR, DOOR_HEIGHT_CELLS, FLOOR, MASS, SLAB_CELLS, SLOPE, TO_NEG_Y, TO_POS_Y, WALL, WINDOW, Deck, block,
    block_kinds, door_boxes, LAMP, LAMP_CELLS, lamp_blocks, min_wall_thickness, mask_blocks, render_plan, spawn_blocks, wall_blocks, wall_panel_block,
    wall_opening, wall_segment, write_blueprint)

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
WIDTH, DEPTH = 90, 70 + NORTH_M  # the east spine reaches 85 m
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
RAIL_THICKNESS_CELLS = 2
FORECOURT = (43, 27 + NORTH_M, 49, 37 + NORTH_M)  # a balcony in the atrium before the bridge door, as wide as its flat

# Flights from each deck to the next, on the ring's flats: a small wedge that lifts to the plate's height and a
# Panel plate, rising 1:2.5 along the flat (33 cells), a few cells off the wall. Not on a flat whose entrance is on
# either deck (the spine's W up to deck 4, security's N up to 3, the command wing's and bridge's S from 6), and not
# on the flat of the flight below (it would start over that flight's opening).
NORTH_CELLS = NORTH_M * CELLS_PER_M
FLIGHT_SIDES = {1: ("E",), 2: ("S",), 3: ("E",), 4: ("S", "N"), 5: ("E", "W"), 6: ("N",), 7: ("E", "W")}
FLIGHT_LIP_RUN_CELLS = 5
FLIGHT_LIP_RISE_CELLS = 2
FLIGHT_PLATE_RUN_CELLS = 25
FLIGHT_PLATE_THICKNESS_CELLS = 2  # the plate's top is this far above its line; 2 holds air back at 1:2.5
FLIGHT_LENGTH_CELLS = FLIGHT_LIP_RUN_CELLS + FLIGHT_PLATE_RUN_CELLS
FLIGHT_WIDTH_CELLS = 11
FLIGHT_WALL_GAP_CELLS = 8  # a 2 m ledge between a flight's opening and the wall
WALL_LINE_CELLS = {side: cell for side, cell in
                   (("N", ATRIUM[1] * CELLS_PER_M), ("S", ATRIUM[3] * CELLS_PER_M),
                    ("W", ATRIUM[0] * CELLS_PER_M), ("E", ATRIUM[2] * CELLS_PER_M))}

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


def oblique_sides():
    """(node a, node b, minor axis, signed thickness) of the 12 oblique sides of the 16-gon, thickened outwards."""
    offsets, nodes = atrium_offsets(), atrium_nodes()
    sides = []
    for i, (a, b) in enumerate(zip(nodes, nodes[1:] + nodes[:1])):
        if i % len(QUADRANT_NODES_HALF_CELLS) == len(QUADRANT_NODES_HALF_CELLS) - 1:
            continue  # a flat, a grid wall
        minor = 1 if abs(b[0] - a[0]) >= abs(b[1] - a[1]) else 0
        mid = offsets[i][minor] + offsets[(i + 1) % len(offsets)][minor]
        sides.append((a, b, minor, PANEL_THICKNESS_CELLS * (1 if mid > 0 else -1)))
    return sides


def atrium_panels(z, height, skipped=()):
    """The oblique panels of one deck, on its floor, but for the sides (indices) that are wall segments."""
    return [wall_panel_block((*a, z), (*b, z), thickness, height)
            for i, (a, b, _, thickness) in enumerate(oblique_sides()) if i not in skipped]


def strictly_overlap(polygon, x, y):
    """Whether a convex polygon and the cell (x, y) share area (touching edges do not count)."""
    cell = [(x, y), (x + 1, y), (x + 1, y + 1), (x, y + 1)]
    edges = [(p[1] - q[1], q[0] - p[0]) for ring in (polygon, cell) for p, q in zip(ring, ring[1:] + ring[:1])]
    for nx, ny in edges:
        first = [nx * px + ny * py for px, py in polygon]
        second = [nx * px + ny * py for px, py in cell]
        if max(first) <= min(second) or max(second) <= min(first):
            return False
    return True


@functools.cache
def atrium_polygon():
    """The 16-gon's corners in cells."""
    return [((cx2 + dx) / 2, (cy2 + dy) / 2) for cx2, cy2 in [atrium_centre2()] for dx, dy in atrium_offsets()]


def inside_atrium(x, y):
    """Whether the point (cells) is inside the 16-gon or on its edge."""
    polygon = atrium_polygon()
    sides = [(bx - ax) * (y - ay) - (by - ay) * (x - ax) for (ax, ay), (bx, by) in zip(polygon, polygon[1:] + polygon[:1])]
    return all(side >= 0 for side in sides) or all(side <= 0 for side in sides)


def touches_atrium(x, y):
    """Whether the cell (x, y) has a corner in the 16-gon: a slab there stays under the panels' thickness."""
    return any(inside_atrium(x + dx, y + dy) for dx in (0, 1) for dy in (0, 1))


# ----- The gallery ring ----------------------------------------------------------------------

# The floor of every gallery is a ring of wall segments (BlueprintWall), one for each side of the 16-gon and a
# cell thick (a floor is a wall whose stick is one cell long): the game stitches them along seams at their shared
# nodes, so the ring has no steps along its edges. Its outer edge is the outer face of the walls and panels; its
# inner edge is the opening over the agora, `RING_WIDTH` cells inside the node lines along each side's minor axis.
RING_WIDTH = {"flat": 19, "steep": 15, "diagonal": 17}
FLAT_OUTER_CELLS = 1  # the flat's wall is one cell thick
FLAT_BAND_CHARS = 5  # the ring's width at a flat (20 cells)


def ring_sides():
    """The 16 sides of the 16-gon: nodes a and b, the minor axis, the outward sign along it, the wall's thickness
    outside the node line, the ring's width inside it, and the flat's name for the four flats."""
    offsets, nodes = atrium_offsets(), atrium_nodes()
    flat_names = {3: "S", 7: "W", 11: "N", 15: "E"}
    sides = []
    for i, (a, b) in enumerate(zip(nodes, nodes[1:] + nodes[:1])):
        position = i % len(QUADRANT_NODES_HALF_CELLS)
        flat = position == len(QUADRANT_NODES_HALF_CELLS) - 1
        minor = 1 if abs(b[0] - a[0]) >= abs(b[1] - a[1]) else 0
        mid = offsets[i][minor] + offsets[(i + 1) % len(offsets)][minor]
        kind = "flat" if flat else ("diagonal" if position == 1 else "steep")
        sides.append({"a": a, "b": b, "minor": minor, "out": 1 if mid > 0 else -1,
                      "outer": FLAT_OUTER_CELLS if flat else PANEL_THICKNESS_CELLS, "width": RING_WIDTH[kind],
                      "flat": flat_names.get(i)})
    return sides


def meet(first, second):
    """The cell where two lines (point, direction) cross, rounded to the nearest."""
    (px, py), (dx, dy) = first
    (qx, qy), (ex, ey) = second
    det = Fraction(dx * ey - dy * ex)
    along = Fraction((qx - px) * ey - (qy - py) * ex) / det
    return tuple(math.floor(c + Fraction(1, 2)) for c in (px + along * dx, py + along * dy))


def offset_line(side, shift):
    """The side's node line moved `shift` cells outward along its minor axis."""
    point = list(side["a"])
    point[side["minor"]] += side["out"] * shift
    return tuple(point), (side["b"][0] - side["a"][0], side["b"][1] - side["a"][1])


@functools.cache
def ring_polygons():
    """The ring's outer and inner polygons: the crossings of each side's line with the previous side's."""
    sides = ring_sides()
    outer = [meet(offset_line(sides[i - 1], sides[i - 1]["outer"]), offset_line(sides[i], sides[i]["outer"]))
             for i in range(len(sides))]
    inner = [meet(offset_line(sides[i - 1], -sides[i - 1]["width"]), offset_line(sides[i], -sides[i]["width"]))
             for i in range(len(sides))]
    return outer, inner


def inside_convex(polygon, x, y):
    sides = [(bx - ax) * (y - ay) - (by - ay) * (x - ax)
             for (ax, ay), (bx, by) in zip(polygon, polygon[1:] + polygon[:1])]
    return all(side >= 0 for side in sides) or all(side <= 0 for side in sides)


@functools.cache
def ring_cells():
    """The cells the ring's segments cut (those the outer polygon cuts and the inner one doesn't hold whole): no
    other slab goes there."""
    outer, inner = ring_polygons()
    xs, ys = [p[0] for p in outer], [p[1] for p in outer]
    cells = set()
    for y in range(min(ys) - 1, max(ys) + 1):
        for x in range(min(xs) - 1, max(xs) + 1):
            if strictly_overlap(outer, x, y) and not all(
                    inside_convex(inner, x + dx, y + dy) for dx in (0, 1) for dy in (0, 1)):
                cells.add((x, y))
    return cells


def flight_sides(index):
    """The flats that have a flight from deck `index`, and so an opening in the slab above it."""
    return FLIGHT_SIDES.get(index, ())


def run_interval(side, footprint):
    """The stretch of a flat a flight's footprint takes."""
    x0, y0, x1, y1 = footprint
    return (y0, y1) if side in "EW" else (x0, x1)


def along_flat(side, node, coordinate):
    """The point on a flat's line through `node` whose run coordinate is `coordinate`."""
    return (node[0], coordinate) if side in "EW" else (coordinate, node[1])


def run_axis(side):
    return 1 if side in "EW" else 0


def ring_segments(index):
    """The segments of the floor ring of slab layer `index` (under deck index + 1): node a, node b, thickness. A
    flat with a flight is cut in three: the middle one is only the ledge between the opening and the wall."""
    sides = ring_sides()
    outer, _ = ring_polygons()
    z = index * DECK_PITCH_CELLS
    segments = []
    for i, side in enumerate(sides):
        a, b = outer[i], outer[(i + 1) % len(sides)]
        full = -side["out"] * (side["outer"] + side["width"])
        name = side["flat"]
        if name not in flight_sides(index):
            segments.append((a, b, full))
            continue
        footprint = flight(name, index)[1]
        axis = run_axis(name)
        lo, hi = run_interval(name, footprint)
        first, second = (lo, hi) if a[axis] < b[axis] else (hi, lo)
        wall_edge = {"E": footprint[2], "S": footprint[3], "W": footprint[0], "N": footprint[1]}[name]
        thin = -side["out"] * abs(a[side["minor"]] - wall_edge)
        mid_a, mid_b = along_flat(name, a, first), along_flat(name, a, second)
        segments += [(a, mid_a, full), (mid_a, mid_b, thin), (mid_b, b, full)]
    return [((*a, z), (*b, z), thickness) for a, b, thickness in segments]


def ring_rails(index):
    """The railings along the ring's inner edge, on the floor of deck index + 1: the stretch of a flat where a flight
    arrives or leaves and the one before the forecourt stay open."""
    sides = ring_sides()
    _, inner = ring_polygons()
    z = deck_floor_z(index + 1)
    segments = []
    for i, side in enumerate(sides):
        a, b = inner[i], inner[(i + 1) % len(sides)]
        thickness = side["out"] * RAIL_THICKNESS_CELLS
        name = side["flat"]
        gap = None
        if name in flight_sides(index):
            gap = run_interval(name, flight(name, index)[1])
        elif name in flight_sides(index + 1):  # a flight starting on this deck
            gap = flight_along(name)
        elif name == "S" and index == BRIDGE_DECK - 1:
            gap = (FORECOURT[0] * CELLS_PER_M, FORECOURT[2] * CELLS_PER_M + 1)
        if gap is None:
            segments.append((a, b, thickness))
            continue
        axis = run_axis(name)
        first, second = gap if a[axis] < b[axis] else gap[::-1]
        segments += [(a, along_flat(name, a, first), thickness), (along_flat(name, a, second), b, thickness)]
    return [((*a, z), (*b, z), thickness) for a, b, thickness in segments]


def ring_walls():
    """Every wall segment of the blueprint: the floor ring of each gallery layer and its railings."""
    walls = []
    for index in range(1, DECKS):
        walls += [wall_segment(a, b, thickness, SLAB_CELLS) for a, b, thickness in ring_segments(index)]
        walls += [wall_segment(a, b, thickness, COUNTER_HEIGHT_CELLS) for a, b, thickness in ring_rails(index)]
    return walls


# ----- The east spine ------------------------------------------------------------------------

# The east spine leaves the atrium through the south-east 45-degree side (a steep side has one wall step and takes
# no opening), runs out as a stub at 45 degrees, and bends to the lattice step (5, 2), 21.8 degrees. Its walls,
# floor and ceiling are wall segments, a floor being a one-cell-thick wall the width of the corridor; the strips
# stop where a bend or an end would leave a triangle, and a stepped mask of floor blocks fills those.
STUB_SIDE = 1
SPINE_B_DECKS = SPINE_DECKS
SPINE_B_OPENING_OFFSET_STEPS = 4  # wall steps from the atrium side's first node
SPINE_B_OPENING_STEPS = 11
STUB_LENGTH_CELLS = 32  # along x and y each
STUB_STEP = (1, 1)
SPINE_B_STEP = (5, 2)
SPINE_B_STEPS = 14
SPINE_B_WALL_CELLS = 2  # the least that holds air back at 45 degrees and at (5, 2)
SPINE_B_LAMP_X = 250  # the stub's lamp
SPINE_B_LAMP_STEPS = (4, 10)  # the second segment's lamps, in steps from its start


class SpineGeometry(NamedTuple):
    side: tuple  # node a, node b, minor axis, thickness of the atrium's side
    ne: tuple  # the north-east wall's nodes: start, bend, end
    sw: tuple
    outer: tuple  # the outer footprint as two convex quads: the stub's and the second segment's
    strips: tuple  # (first x, last x) of the floor strip along the stub and along the second segment
    thicknesses: tuple  # the strips' thicknesses along y
    first_diff: int  # x - y of the stub's north-east outer face
    middle: tuple  # a point on the second segment's centre line
    lamp_diff: int  # x - y of the stub's centre line


def _along(point, steps, step):
    return point[0] + steps * step[0], point[1] + steps * step[1]


def _shifted(point, by):
    return point[0] + by[0], point[1] + by[1]


@functools.cache
def spine_b():
    a, b, minor, thickness = oblique_sides()[STUB_SIDE]
    steps = math.gcd(abs(b[0] - a[0]), abs(b[1] - a[1]))
    step = ((b[0] - a[0]) // steps, (b[1] - a[1]) // steps)
    assert step == (-1, 1) and minor == 1 and thickness > 0, "the spine is drawn for the south-east 45-degree side"
    ne_edge = _along(a, SPINE_B_OPENING_OFFSET_STEPS, step)
    sw_edge = _along(ne_edge, SPINE_B_OPENING_STEPS, step)
    face = ((a[0], a[1] + thickness), step)
    ne_start, sw_start = (meet(face, (edge, STUB_STEP)) for edge in (ne_edge, sw_edge))
    ne_stub_end, sw_stub_end = (_along(start, STUB_LENGTH_CELLS, STUB_STEP) for start in (ne_start, sw_start))
    # The second segment's centre line goes through the middle of the stub's end; its walls stand half the
    # corridor's width (the distance of the stub's inner faces) to either side.
    wall = SPINE_B_WALL_CELLS
    inner_gap = (sw_start[1] - sw_start[0]) - (ne_start[1] - ne_start[0])
    half = inner_gap / 2 / math.sqrt(2)
    norm = math.hypot(*SPINE_B_STEP)
    side = (SPINE_B_STEP[1] * half / norm, -SPINE_B_STEP[0] * half / norm)  # towards the north-east
    mid = ((ne_stub_end[0] + sw_stub_end[0]) / 2, (ne_stub_end[1] + sw_stub_end[1]) / 2)
    ne_bend = meet((ne_start, STUB_STEP), ((mid[0] + side[0], mid[1] + side[1]), SPINE_B_STEP))
    sw_bend = meet((sw_start, STUB_STEP), ((mid[0] - side[0], mid[1] - side[1]), SPINE_B_STEP))
    ne_end, sw_end = (_along(bend, SPINE_B_STEPS, SPINE_B_STEP) for bend in (ne_bend, sw_bend))

    # The footprint: the walls' outer faces, a wall's body being `wall` cells further along y (south-west wall) or
    # back (north-east wall); the cap's body is `wall` cells further along x.
    up, down = (0, -wall), (0, wall)
    ne_out = meet((_shifted(ne_start, up), STUB_STEP), (_shifted(ne_bend, up), SPINE_B_STEP))
    sw_out = meet((_shifted(sw_start, down), STUB_STEP), (_shifted(sw_bend, down), SPINE_B_STEP))
    face_ne = meet(face, (_shifted(ne_start, up), STUB_STEP))
    face_sw = meet(face, (_shifted(sw_start, down), STUB_STEP))
    cap_ne, cap_sw = _shifted(ne_end, (wall, -wall)), _shifted(sw_end, (wall, wall))
    quads = ([face_ne, ne_out, sw_out, face_sw], [ne_out, cap_ne, cap_sw, sw_out])

    # Strips: along the stub from the atrium's outer face to where either side bends, along the second segment
    # from where both have bent to where either side ends; the mask holds the rest. The second strip's nodes lie
    # on the north-east outer line at whole cells, so its x advances by whole steps.
    first_diff = ne_start[0] - ne_start[1] + wall
    sw_diff = sw_start[0] - sw_start[1] - wall
    start_x = (face[0][0] + face[0][1] + sw_diff) // 2
    bend_x = max(ne_out[0], sw_out[0])
    second_start = ne_bend[0] + SPINE_B_STEP[0] * -(-(bend_x - ne_bend[0]) // SPINE_B_STEP[0])
    second_end = ne_bend[0] + SPINE_B_STEP[0] * ((min(ne_end[0], sw_end[0]) - ne_bend[0]) // SPINE_B_STEP[0])
    slope = Fraction(SPINE_B_STEP[1], SPINE_B_STEP[0])
    second_thickness = math.ceil((sw_bend[1] + wall - slope * sw_bend[0]) - (ne_bend[1] - wall - slope * ne_bend[0]))
    return SpineGeometry(
        (a, b, minor, thickness), (ne_start, ne_bend, ne_end), (sw_start, sw_bend, sw_end), tuple(quads),
        ((start_x, min(ne_out[0], sw_out[0])), (second_start, second_end)), (first_diff - sw_diff, second_thickness),
        first_diff, ((ne_bend[0] + sw_bend[0]) // 2, (ne_bend[1] + sw_bend[1]) // 2),
        (ne_start[0] - ne_start[1] + sw_start[0] - sw_start[1]) // 2)


def spine_b_walls_of_deck(number):
    """The atrium's side with its door and the spine's walls on deck `number`."""
    g = spine_b()
    z, height = deck_floor_z(number), wall_height(number)
    a, b, _, thickness = g.side
    wall = SPINE_B_WALL_CELLS
    door = wall_opening(SPINE_B_OPENING_OFFSET_STEPS, SPINE_B_OPENING_STEPS, DOOR_HEIGHT_CELLS)
    cap = (g.ne[2], g.sw[2])
    walls = [wall_segment((*a, z), (*b, z), thickness, height, openings=[door]),
             wall_segment((*cap[0], z), (*cap[1], z), min_wall_thickness((*cap[0], z), (*cap[1], z)), height)]
    for nodes, sign in ((g.ne, -wall), (g.sw, wall)):
        walls += [wall_segment((*nodes[i], z), (*nodes[i + 1], z), sign, height) for i in range(2)]
    return walls


def spine_b_slabs(layer):
    """The spine's floor (the ceiling of the deck below) under slab layer `layer`: a strip along the stub and one
    along the second segment. On the ground the spine is only mask, since the atrium's slab fills the cells there."""
    if layer == 0:
        return []
    g = spine_b()
    z = layer * DECK_PITCH_CELLS
    (first_start, first_end), (second_start, second_end) = g.strips
    ne_bend = g.ne[1]
    slope = Fraction(SPINE_B_STEP[1], SPINE_B_STEP[0])

    def second_y(x):
        return int(ne_bend[1] - SPINE_B_WALL_CELLS + slope * (x - ne_bend[0]))

    return [wall_segment((first_start, first_start - g.first_diff, z), (first_end, first_end - g.first_diff, z),
                         g.thicknesses[0], SLAB_CELLS),
            wall_segment((second_start, second_y(second_start), z), (second_end, second_y(second_end), z),
                         g.thicknesses[1], SLAB_CELLS)]


def spine_b_slab_cells(layer):
    """The cells of floor blocks under slab layer `layer`: all of the footprint on the ground, else the columns
    between the strips and past the second one."""
    g = spine_b()
    stub_end = g.strips[0][1]
    second_start, second_end = g.strips[1]
    cells = set()
    for polygon in g.outer:
        xs, ys = [p[0] for p in polygon], [p[1] for p in polygon]
        cells |= {(x, y) for y in range(min(ys) - 1, max(ys) + 1) for x in range(min(xs) - 1, max(xs) + 1)
                  if strictly_overlap(polygon, x, y)
                  and (layer == 0 or stub_end <= x < second_start or x >= second_end)}
    return cells


def spine_b_lamps(number):
    g = spine_b()
    z = deck_floor_z(number) + wall_height(number) - 1
    spots = [(SPINE_B_LAMP_X, SPINE_B_LAMP_X - g.lamp_diff)]
    spots += [_along(g.middle, steps, SPINE_B_STEP) for steps in SPINE_B_LAMP_STEPS]
    return [block(LAMP, (x - 1, y - 1, z), (LAMP_CELLS, LAMP_CELLS, 1)) for x, y in spots]


def spine_b_walls():
    walls = []
    for number in range(1, SPINE_B_DECKS + 1):
        walls += spine_b_walls_of_deck(number)
    for layer in range(SPINE_B_DECKS + 1):
        walls += spine_b_slabs(layer)
    return walls


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
        """The bridge deck's forecourt: its front and its sides, down to the ring's inner edge; the ring's own
        railings are wall segments (ring_rails)."""
        for x in range(FORECOURT[0], FORECOURT[2] + 1):
            self.put(x, FORECOURT[1], COUNTER)
        for y in range(FORECOURT[1] + 1, FORECOURT[3] - FLAT_BAND_CHARS + 1):
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
        if number == BRIDGE_DECK:
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

def flight_along(side):
    """The cells a flight runs along on `side`, centred on the hall."""
    cx2, cy2 = atrium_centre2()
    start = ((cy2 if side in "EW" else cx2) - FLIGHT_LENGTH_CELLS + 1) // 2
    return start, start + FLIGHT_LENGTH_CELLS


def flight(side, number):
    """The blocks of the flight on `side` from deck `number` to the next: the lip wedge and the plate; and the
    footprint of the opening it leaves in the slab above. It climbs clockwise: E up +y, S towards -x, W towards
    -y, N up +x."""
    z0 = deck_floor_z(number)
    start, end = flight_along(side)
    wall = WALL_LINE_CELLS[side]
    low = (wall - FLIGHT_WALL_GAP_CELLS - FLIGHT_WIDTH_CELLS if side in "ES" else wall + 1 + FLIGHT_WALL_GAP_CELLS)
    lip_size = (FLIGHT_LIP_RUN_CELLS, FLIGHT_WIDTH_CELLS, FLIGHT_LIP_RISE_CELLS)
    top = z0 + DECK_PITCH_CELLS - FLIGHT_LIP_RISE_CELLS
    plate = {"E": ((low, start + FLIGHT_LIP_RUN_CELLS), (low, end), 0),
             "W": ((low, end - FLIGHT_LIP_RUN_CELLS), (low, start), 0),
             "S": ((end - FLIGHT_LIP_RUN_CELLS, low), (start, low), 1),
             "N": ((start + FLIGHT_LIP_RUN_CELLS, low), (end, low), 1)}[side]
    (ax, ay), (bx, by), stick_axis = plate
    lip = {"E": ((low, start, z0), TO_POS_Y), "W": ((low, end - FLIGHT_LIP_RUN_CELLS, z0), TO_NEG_Y),
           "S": ((end - FLIGHT_LIP_RUN_CELLS, low, z0), NEG_X), "N": ((start, low, z0), None)}[side]
    blocks = [block(SLOPE, lip[0], lip_size, lip[1]),
              wall_panel_block((ax, ay, z0), (bx, by, top), FLIGHT_PLATE_THICKNESS_CELLS, FLIGHT_WIDTH_CELLS,
                               stick_axis)]
    run = (min(ax, bx), min(ay, by), max(ax, bx), max(ay, by))
    footprint = ((run[0], run[1], run[0] + FLIGHT_WIDTH_CELLS, run[3]) if side in "EW"
                 else (run[0], run[1], run[2], run[1] + FLIGHT_WIDTH_CELLS))
    return blocks, footprint


def flights():
    found = []
    for number in range(1, DECKS):
        for side in FLIGHT_SIDES[number]:
            blocks, footprint = flight(side, number)
            found.append((number, blocks, footprint))
    return found


def vestibule_box(atrium_side):
    """The characters between a wing's row and the atrium's flat, between the jambs."""
    row = (ATRIUM[1] - 1, ATRIUM[1]) if atrium_side == "S" else (ATRIUM[3], ATRIUM[3] + 1)
    return WING_JAMB_X[0], row[0], WING_JAMB_X[1], row[1]


def slab_mask(index):
    """The slab under deck `index + 1` (the roof for the top deck's): its footprint. The galleries' own floor,
    the ring, is wall segments, so the slab leaves its cells and the opening over the agora."""
    x0, y0, x1, y1 = outer_box(ATRIUM)
    mask = [[False] * (WIDTH * CELLS_PER_M) for _ in range(DEPTH * CELLS_PER_M)]

    def fill(box):
        for y in range(box[1], box[3]):
            for x in range(box[0], box[2]):
                mask[y][x] = True

    if index == 0 or index == DECKS:
        for y in range(y0, y1):
            for x in range(x0, x1):
                mask[y][x] = touches_atrium(x, y)
    if index <= SPINE_DECKS:
        fill(outer_box(SPINE))
        for x, y in spine_b_slab_cells(index):
            mask[y][x] = True
    if index in (HANGAR_DECKS.start - 1, HANGAR_DECKS.stop - 1):  # no slab inside the hall
        fill(outer_box(DEPARTURE))
    elif HANGAR_DECKS.start <= index < HANGAR_DECKS.stop - 1:  # between its decks only under its walls
        dx0, dy0, dx1, dy1 = (c * CELLS_PER_M for c in DEPARTURE)
        for box in ((dx0, dy0, dx0 + 1, dy1 + 1), (dx1, dy0, dx1 + 1, dy1 + 1),
                    (dx0, dy0, dx1 + 1, dy0 + 1), (dx0, dy1, dx1 + 1, dy1 + 1)):
            fill(box)
    for wing in WINGS:
        if index in range(wing.decks.start - 1, wing.decks.stop):
            fill(outer_box(wing.box))
            fill(outer_box(vestibule_box(wing.atrium_side)))
    if index == BRIDGE_DECK - 1 or index == DECKS:
        fill(outer_box(BRIDGE))
        fill(outer_box(vestibule_box("N")))
    if index == BRIDGE_DECK - 1:
        fill(outer_box(FORECOURT))
    if 1 <= index < DECKS:
        for x, y in ring_cells():
            mask[y][x] = False
    return mask


def slab_blocks():
    blocks = []
    for index in range(DECKS + 1):
        blocks += mask_blocks(slab_mask(index), index * DECK_PITCH_CELLS if index < DECKS else
                              (DECKS - 1) * DECK_PITCH_CELLS + SLAB_CELLS + TOP_DECK_WALL_HEIGHT_CELLS, FLOOR)
    return blocks


def ring_ceiling_blocks():
    """Boxes where the ring's floor is, for the lamps to hang from; they are not written to the blueprint."""
    mask = [[False] * (WIDTH * CELLS_PER_M) for _ in range(DEPTH * CELLS_PER_M)]
    for x, y in ring_cells():
        mask[y][x] = True
    return [b for index in range(1, DECKS) for b in mask_blocks(mask, index * DECK_PITCH_CELLS, FLOOR)]


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
        deck.blocks = (wall_blocks(deck.grid, z, wall_height=height)
                       + atrium_panels(z, height, (STUB_SIDE,) if deck.number <= SPINE_B_DECKS else ()))
        deck.blocks += spawn_blocks(deck, z)
        blocks += deck.blocks
    ceilings = ring_ceiling_blocks()
    for deck in decks:
        lamps = lamp_blocks(deck, deck_floor_z(deck.number), blocks + ceilings, wall_height(deck.number))
        deck.blocks += lamps
        blocks += lamps
        if deck.number <= SPINE_B_DECKS:
            deck.blocks += spine_b_lamps(deck.number)
            blocks += spine_b_lamps(deck.number)
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
    blocks = write_blueprint(BLUEPRINT_FILE, blocks, ring_walls() + spine_b_walls())
    lines.append("")
    lines.append(STATS_BLOCKS.format(file=BLUEPRINT_FILE.name, total=len(blocks), kinds=block_kinds(blocks)))
    (OUT / STATS_FILE).write_text("\n".join(lines) + "\n", encoding="ascii", newline="\n")
    print("\n".join(lines))


if __name__ == "__main__":
    main()
