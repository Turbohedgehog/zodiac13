"""The old wing and its ruin dressing, plus blast and fire gates across the corridors.

Abandoned rooms keep the furniture of the room they were (quarters, canteen, kitchen, brig, medbay, offices,
laboratories, stores) but half of it is missing and the rest is tipped over; rubble, cobwebs, scorch marks,
puddles, glass and torn cables fill the gaps and most lamps are dead. `add_wing(builder, layout)` runs after the
room lists: it dresses every wing room and places the gates.
"""

import math
import random

import station_fill as sf
import station_layout as sl

SEED = 13
RNG = random.Random(SEED)

fill = sf.fill
spaced = sf.spaced
wall_decor = sf.wall_decor
edge_range = sf.edge_range
DECOR = sl.DECOR

TILT_QUARTER = 1.5
TILT_LEAN = 0.5
KEEP = 0.6
LAMP_ALIVE = 0.3
LAMP_STEP = 14
GATE_CLEAR = 3.0
GATE_STEP_CORRIDOR = 24
GATE_STEP_TUNNEL = 20
GATE_MARGIN = 6
GATE_SIGN = "ГЕРМОДВЕРЬ"
GATE_SIGN_Z = 2.65
GATE_SIGN_SIZE = 0.28
GATE_SIGN_OFFSET = 0.32
GATE_WIDTHS = (2, 4)
MIN_GATE_LENGTH = 20
COBWEB_CHANCE = 0.8
LITTER_Z = 0.012
RUINED_CORRIDORS = ("old_spine", "old_c2", "old_m", "old_c3", "old_hall", "old_garage")
CORRIDOR_RUIN_FACTOR = 3
CELL_DEPTH = 7
PANES_LEFT = (0, 1, 1, 2, 3)  # rows of glass a broken cell front keeps, from the floor up


def ruin(s, template, x, y, tilt=0.4, z=0.0, yaw=None):
    """Place a piece of furniture at random yaw, tipped over with the given probability."""
    obj = s._instance(template, (x, y, z), RNG.uniform(0.0, 2 * math.pi) if yaw is None else yaw)
    if obj is not None and RNG.random() < tilt:
        obj.rotation_euler.x = RNG.choice((TILT_QUARTER, -TILT_QUARTER, TILT_LEAN, -TILT_LEAN))
        obj.rotation_euler.y = RNG.choice((0.0, 0.0, TILT_LEAN, -TILT_LEAN))
    return obj


def scatter(s, layout, room, templates, count, tilt=0.5, margin=1.0, z=0.0):
    for _ in range(count):
        low_x, high_x = room.x0 + margin, room.x1 - margin
        low_y, high_y = room.y0 + margin, room.y1 - margin
        if high_x <= low_x or high_y <= low_y:
            continue
        x, y = RNG.uniform(low_x, high_x), RNG.uniform(low_y, high_y)
        if layout.point_free(room, x, y):
            ruin(s, RNG.choice(templates), x, y, tilt, z)


def furnish(s, layout, room, spec, keep=KEEP, tilt=0.4):
    """Furniture on a grid with some pieces missing and the rest tipped over."""
    for template, step_x, step_y, margin in spec:
        for x in spaced(room.x0, room.x1, step_x, margin):
            for y in spaced(room.y0, room.y1, step_y, margin):
                if RNG.random() < keep and layout.point_free(room, x, y):
                    ruin(s, template, x, y, tilt)


def along_wall(s, layout, room, edges, template, step, keep=0.5, offset=0.0, inset=0.0):
    for edge in edges:
        low, high = edge_range(room, edge)
        along = low + step / 2 + offset
        while along < high - 1:
            if RNG.random() < keep:
                wall_decor(s, layout, room, edge, template, along, 0.0, inset=inset)
            along += step


def dull_lamps(s, layout, room):
    """Most lamps dead or hanging by one end, a few still lit."""
    z = min(room.height - 0.6, 5.4)
    for edge in sl.long_edges(room):
        low, high = edge_range(room, edge)
        along = low + LAMP_STEP / 2
        while along < high - 1:
            if RNG.random() < LAMP_ALIVE:
                wall_decor(s, layout, room, edge, "decor_lamp_tube", along, z)
            else:
                wall_decor(s, layout, room, edge, "decor_broken_lamp", along, 0.0)
            along += LAMP_STEP


# ----- Wing rooms -----


def d_old_store(s, layout, room):
    dull_lamps(s, layout, room)
    furnish(s, layout, room, (("decor_crate", 3.2, 3.2, 2.5),), keep=0.55, tilt=0.3)
    scatter(s, layout, room, ("decor_toppled_locker",), 3, tilt=0.0)


def d_old_workshop(s, layout, room):
    dull_lamps(s, layout, room)
    furnish(s, layout, room, (("decor_workbench", 5.0, 4.0, 3.0), ("decor_generator", 8.0, 8.0, 4.0)), keep=0.55)
    along_wall(s, layout, room, "NS", "decor_tool_wall", 2.1, keep=0.4)


def d_old_cabin(s, layout, room):
    dull_lamps(s, layout, room)
    far = sl.far_edge(layout, room)
    y = room.y1 - 1.4 if far == "N" else room.y0 + 1.4
    for dx in (1.2, 2.6):
        if RNG.random() < 0.7:
            ruin(s, "decor_bed", room.x0 + dx, y, 0.35, yaw=0.0 if far == "N" else math.pi)
    scatter(s, layout, room, ("decor_toppled_locker", "decor_locker"), 1, tilt=0.0, margin=1.0)
    scatter(s, layout, room, ("decor_table", "decor_crew_chair"), 2, tilt=0.6, margin=1.2)


def d_old_canteen(s, layout, room):
    dull_lamps(s, layout, room)
    for x in spaced(room.x0, room.x1, 4.2, 3.0):
        for y in spaced(room.y0, room.y1, 3.4, 3.0):
            if RNG.random() < KEEP and layout.point_free(room, x, y):
                ruin(s, "decor_table", x, y, 0.35)
                for _ in range(2):
                    ruin(s, "decor_crew_chair", x + RNG.uniform(-1.0, 1.0), y + RNG.uniform(-1.0, 1.0), 0.6)
    along_wall(s, layout, room, "N", "decor_counter", 2.1, keep=0.5, inset=0.3)
    along_wall(s, layout, room, "S", "decor_vending", 4.0, keep=0.4)


def d_old_kitchen(s, layout, room):
    dull_lamps(s, layout, room)
    along_wall(s, layout, room, "W", "decor_stove", 2.4, keep=0.5)
    along_wall(s, layout, room, "N", "decor_fridge", 2.1, keep=0.4)
    along_wall(s, layout, room, "E", "decor_sink", 2.6, keep=0.5)
    furnish(s, layout, room, (("decor_prep_table", 3.6, 3.6, 4.0),), keep=0.5)


def d_old_brig(s, layout, room):
    """Cells along both walls like the brig's, but the glass fronts are broken and the armoured doors stand open."""
    dull_lamps(s, layout, room)
    south_front = room.y0 + CELL_DEPTH
    north_front = room.y1 - CELL_DEPTH
    x, cells = room.x0, []
    for width in sf.cell_widths(room.x1 - room.x0):
        cells.append((x, width))
        x += width
    for start, width in cells:
        door = start + width // 2
        for facing, front in (("N", south_front), ("S", north_front)):
            for x_ in range(start, start + width):
                if x_ == door:
                    continue
                for row in range(RNG.choice(PANES_LEFT)):
                    s.wall_piece("wall_window", facing, front, x_, row)
            ruin(s, "decor_shards", door + 0.5, front + (-0.5 if facing == "N" else 1.0), 0.0, yaw=0.0)
        centre = start + width / 2
        ruin(s, "decor_bed", centre, room.y0 + 3.0, 0.4, yaw=0.0)
        ruin(s, "decor_bed", centre, room.y1 - 3.0, 0.4, yaw=0.0)
    for start, _ in cells[1:]:
        for y in list(range(room.y0, south_front)) + list(range(north_front, room.y1)):
            for row in range(sl.DOOR_ROWS):
                s.wall_piece("wall_plate_riveted", "E", start, y, row)
    scatter(s, layout, room, ("decor_table", "decor_crew_chair"), 2, tilt=0.7, margin=1.5)


def ruined_desks(s, layout, room, step_x=4.0, step_y=4.0, margin=3.0):
    for x in spaced(room.x0, room.x1, step_x, margin):
        for y in spaced(room.y0, room.y1, step_y, margin):
            if RNG.random() < KEEP and layout.point_free(room, x, y):
                ruin(s, "decor_table", x, y, 0.3)
                ruin(s, "decor_monitor_crt", x + RNG.uniform(-0.8, 0.8), y + RNG.uniform(-0.6, 0.6), 0.8, z=0.0)
                ruin(s, "decor_crew_chair", x + RNG.uniform(-1.2, 1.2), y - 1.0, 0.6)


def d_old_guard(s, layout, room):
    dull_lamps(s, layout, room)
    ruined_desks(s, layout, room)
    scatter(s, layout, room, ("decor_toppled_locker",), 2, tilt=0.0)
    along_wall(s, layout, room, "EW", "decor_weapon_rack", 2.2, keep=0.2)


def d_old_medbay(s, layout, room):
    dull_lamps(s, layout, room)
    furnish(
        s, layout, room,
        (("decor_bed", 3.4, 4.0, 2.5), ("decor_iv_stand", 3.4, 4.0, 3.0), ("decor_wheelchair", 9.0, 9.0, 4.0)),
        keep=0.55, tilt=0.4,
    )
    along_wall(s, layout, room, "NS", "decor_med_cabinet", 4.0, keep=0.4)


def d_old_office(s, layout, room):
    dull_lamps(s, layout, room)
    ruined_desks(s, layout, room)
    scatter(s, layout, room, ("decor_toppled_locker",), 2, tilt=0.0)
    scatter(s, layout, room, ("decor_small_papers", "decor_small_clipboard"), 10, tilt=0.0, margin=1.0, z=LITTER_Z)


def d_old_lab(s, layout, room):
    dull_lamps(s, layout, room)
    for y in spaced(room.y0, room.y1, 4.5, 3.0):
        for x in spaced(room.x0, room.x1, 4.0, 3.0):
            if RNG.random() < KEEP and layout.point_free(room, x, y):
                ruin(s, "decor_counter", x, y, 0.25)
                ruin(s, "decor_analyzer", x + 0.4, y, 0.7, z=0.0)
    along_wall(s, layout, room, "N", "decor_fume_hood", 3.2, keep=0.5)
    scatter(s, layout, room, ("decor_centrifuge", "decor_microscope", "decor_small_beaker_set"), 6, tilt=0.9)


def d_old_server(s, layout, room):
    dull_lamps(s, layout, room)
    furnish(s, layout, room, (("decor_mainframe", 3.2, 3.2, 2.5),), keep=0.6, tilt=0.35)


def d_old_lounge(s, layout, room):
    dull_lamps(s, layout, room)
    furnish(s, layout, room, (("decor_sofa", 4.5, 4.5, 3.0), ("decor_coffee_table", 4.5, 4.5, 4.0)), keep=0.55, tilt=0.35)
    scatter(s, layout, room, ("decor_arcade", "decor_tv", "decor_pool_table"), 3, tilt=0.5, margin=2.5)


def d_barracks(s, layout, room):
    """Temporary quarters set up in a converted hall: rows of beds, lockers along the walls, a table and a cooler."""
    lamps_step = 6
    z = min(room.height - 0.6, 5.4)
    for edge in "NS":
        low, high = edge_range(room, edge)
        along = low + lamps_step / 2
        while along < high - 1:
            wall_decor(s, layout, room, edge, "decor_lamp_tube", along, z)
            along += lamps_step
    fill(s, layout, room, "decor_bed", 3.2, 4.4, margin=2.5)
    along_wall(s, layout, room, "EW", "decor_locker", 1.6, keep=1.0)
    s.stand("decor_table", room.cx, room.cy, "S")
    wall_decor(s, layout, room, sl.far_edge(layout, room), "decor_notice_board", room.cx, 0.0)


DECOR.update({name[2:]: fn for name, fn in list(globals().items()) if name.startswith("d_old_") and callable(fn)})
DECOR["barracks"] = d_barracks


# ----- Ruin dressing -----


def cobwebs(s, room):
    for x, y, yaw in (
        (room.x0 + 0.3, room.y0 + 0.3, 0.0),
        (room.x1 - 0.3, room.y0 + 0.3, math.pi / 2),
        (room.x1 - 0.3, room.y1 - 0.3, math.pi),
        (room.x0 + 0.3, room.y1 - 0.3, -math.pi / 2),
    ):
        if RNG.random() < COBWEB_CHANCE:
            s._instance("decor_cobweb", (x, y, 0.0), yaw)


def dress(s, layout, room):
    """Rubble, scorch marks, puddles, glass, fallen beams, torn cables and cobwebs, in proportion to the room's size."""
    area = (room.x1 - room.x0) * (room.y1 - room.y0)
    if room.name in RUINED_CORRIDORS:
        area *= CORRIDOR_RUIN_FACTOR
    margin = 0.4 if min(room.x1 - room.x0, room.y1 - room.y0) <= 4 else 1.0
    scatter(s, layout, room, ("decor_debris",), area // 100 + 1, tilt=0.0, margin=margin)
    scatter(s, layout, room, ("decor_scorch",), area // 220, tilt=0.0, margin=margin, z=LITTER_Z)
    scatter(s, layout, room, ("decor_puddle",), area // 130 + 1, tilt=0.0, margin=margin, z=LITTER_Z)
    scatter(s, layout, room, ("decor_shards",), area // 160, tilt=0.0, margin=margin, z=LITTER_Z)
    scatter(s, layout, room, ("decor_fallen_beam",), area // 260, tilt=0.0, margin=margin)
    for _ in range(area // 120 + 1):
        edge = RNG.choice(sl.long_edges(room))
        low, high = edge_range(room, edge)
        wall_decor(s, layout, room, edge, "decor_hanging_cable", RNG.uniform(low + 1, high - 1), 0.0)
    cobwebs(s, room)


def is_wing(room):
    return room.kind.startswith("old_") or room.name.startswith("old_")


# ----- Blast and fire gates -----


def door_near(layout, room, along, clear=GATE_CLEAR):
    for edge in "NSEW":
        for start in layout.door_spans.get((room.index, edge), []):
            if start - clear <= along <= start + 2 + clear:
                return True
    return False


def place_gate(s, width, room, along, horizontal):
    """A pressure gate across the corridor at `along`, with its sign on both faces."""
    x, y = (along, room.cy) if horizontal else (room.cx, along)
    s._instance(f"decor_blast_gate{width}", (x, y, 0.0), math.pi / 2 if horizontal else 0.0)
    faces = (("E", (GATE_SIGN_OFFSET, 0.0)), ("W", (-GATE_SIGN_OFFSET, 0.0))) if horizontal else (
        ("N", (0.0, GATE_SIGN_OFFSET)), ("S", (0.0, -GATE_SIGN_OFFSET)))
    for facing, (dx, dy) in faces:
        if s.visible is None or s.visible(x + dx, y + dy):
            sf.add_text(s, GATE_SIGN, (x + dx, y + dy, GATE_SIGN_Z), facing, GATE_SIGN_SIZE)


def add_gates(s, layout):
    """A blast and fire door across every corridor and tunnel at intervals, away from the side doors."""
    placed = {}
    for room in layout.rooms:
        if not (room.corridor or room.kind == "tunnel"):
            continue
        horizontal = (room.x1 - room.x0) >= (room.y1 - room.y0)
        width = min(room.x1 - room.x0, room.y1 - room.y0)
        low, high = (room.x0, room.x1) if horizontal else (room.y0, room.y1)
        if width not in GATE_WIDTHS or high - low < MIN_GATE_LENGTH:
            continue
        step = GATE_STEP_CORRIDOR if room.corridor else GATE_STEP_TUNNEL
        for along in spaced(low, high, step, GATE_MARGIN):
            if door_near(layout, room, along):
                continue
            place_gate(s, width, room, along, horizontal)
            placed.setdefault(room.name, []).append(along)
    return placed


SEALED = (("c2", 38.0), ("m_w", 38.0), ("c3", 38.0), ("old_c2", 30.0), ("old_m", 30.0), ("old_c3", 30.0))


SEAL_X = (60.0, 100.0, 140.0)
SEAL_ROWS = {
    "s1": (94.0), "s4": (26.0), "old_spine": (50.0, 76.0, 96.0), "v4": (94.0), "heads_hall": (120.0),
    "v3b": (73.0), "v3c": (51.0), "v3d": (26.0),
}
SEAL_SEARCH = 12
SEAL_CLEAR = 2.6
SEAL_APART = 6.0


def free_spot(layout, room, target, low, high, placed):
    """The nearest place to `target` on a corridor that is clear of doors and of the gates already there."""
    for shift in range(SEAL_SEARCH + 1):
        for along in (target + shift, target - shift):
            if low + 2 <= along <= high - 2 and not door_near(layout, room, along, SEAL_CLEAR) and all(
                abs(along - other) >= SEAL_APART for other in placed
            ):
                return along
    return None


def add_sealing_lines(s, layout, placed=None):
    """Gates across every corridor on a few lines through the station, so that a breach in one part is sealed
    off from the others: the long corridors at fixed meridians, the cross corridors once or twice each."""
    placed = {} if placed is None else placed

    def put(room, target):
        horizontal = (room.x1 - room.x0) >= (room.y1 - room.y0)
        width = min(room.x1 - room.x0, room.y1 - room.y0)
        if width not in GATE_WIDTHS:
            return
        low, high = (room.x0, room.x1) if horizontal else (room.y0, room.y1)
        spot = free_spot(layout, room, target, low, high, placed.setdefault(room.name, []))
        if spot is not None:
            placed[room.name].append(spot)
            place_gate(s, width, room, spot, horizontal)

    for name in ("c1", "c2", "c3", "m_w", "m_e"):
        room = layout.by_name.get(name)
        if room is None:
            continue
        for x in SEAL_X:
            if room.x0 + 2 <= x <= room.x1 - 2:
                put(room, x)
    for name, rows in SEAL_ROWS.items():
        room = layout.by_name.get(name)
        if room is not None:
            for y in (rows if isinstance(rows, tuple) else (rows,)):
                put(room, y)


def add_sealed_gates(s, layout):
    """The blast gates that were shut across the working corridors and the old ones when the sector was cut off."""
    for name, along in SEALED:
        room = layout.by_name.get(name)
        if room is not None:
            s._instance("decor_blast_gate4", (along, room.cy, 0.0), math.pi / 2)


def add_wing(s, layout):
    for room in layout.rooms:
        if is_wing(room):
            dress(s, layout, room)
    add_sealing_lines(s, layout, add_gates(s, layout))
    add_sealed_gates(s, layout)
