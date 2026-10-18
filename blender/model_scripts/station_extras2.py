"""Cameras across the base, emergency cabinets with extinguishers and oxygen bottles, snack and cigarette
machines, and about 250 further fittings: phones, intercoms, door readers, exit signs, notice boards,
fire alarms, coolers, coat racks, dispensers, litter baskets, cable and pipe runs, floor markings, plants and pictures.

Placement is deterministic. `add_extras2(builder, layout)` runs after the room lists and extras.
"""

import random

import station_fill as sf
import station_layout as sl

accents = sf.accents
wall_decor = sf.wall_decor
spaced = sf.spaced
edge_range = sf.edge_range

SEED = 13
CAMERA_STEP = 24
CAMERA_OFFSET = 6
TUNNEL_CAMERA_STEP = 20
CABINET_STEP = 24
CABINET_BY_DOOR = 3.5
BIG_ROOM = 150
MID_ROOM = 60
NO_CAMERA = ("cabin", "head_cabin", "restrooms", "tunnel", "maint_room")
VENDING_DEPTH = 0.0
ASHTRAY_DEPTH = 1.1
PAIR_GAP = 1.15

SECURE_KINDS = ("airlock_prep", "checkpoint", "warden", "hos_office", "armory", "brig", "prison", "interrogation", "vault", "ai_core",
                "engine", "power", "telecom", "server", "bridge", "captain", "pad_room", "office", "airlock")
EXIT_ROOMS = ("atrium", "arrivals", "escape", "cargo_dock")
PHONE_KINDS = ("bridge", "hop", "captain", "checkpoint", "warden", "hos_office", "office", "kitchen", "bar", "cargo_bay",
               "engine", "power", "atmos", "wards")
COOLER_KINDS = ("bridge", "hop", "office", "lounge", "cargo_bay", "dining", "library", "atrium", "labs")
COAT_KINDS = ("hop", "office", "captain", "meeting", "courtroom", "head_cabin")
DISPENSER_KINDS = ("restrooms", "kitchen", "surgery", "chemistry", "janitor", "virology")
BASKET_KINDS = ("office", "hop", "captain", "meeting", "library", "lounge", "dining", "bar", "checkpoint", "warden",
                "labs", "robotics", "cargo_bay", "wards", "head_cabin", "courtroom", "detective", "bridge")
PLANT_KINDS = ("atrium", "library", "lounge", "hop", "captain", "meeting", "bridge", "dining", "theatre", "gym")
PAINTING_KINDS = ("captain", "hop", "meeting", "lounge", "library", "dining", "bridge", "courtroom", "chapel", "theatre")
RUN_KINDS = ("engine", "power", "atmos", "ore_processing", "tools", "mining", "disposal", "telecom")
VENDING_SITES = (
    ("atrium", "N", 4.0), ("atrium", "S", 4.0), ("atrium", "E", 4.0), ("atrium", "W", 4.0),
    ("lounge", "E", 5.0), ("bar", "W", 5.0), ("dining", "S", 6.0), ("gym", "N", 4.0), ("cargo_bay", "N", 5.0),
    ("tools", "S", 3.0), ("labs", "S", 6.0), ("mining", "N", 8.0), ("engine", "S", 12.0), ("warden", "E", 3.0),
)
CORRIDOR_VENDING = ("c1", "c2", "c3", "m_w", "m_e")


def mount_at(s, layout, room, edge, along, template, z=0.0):
    """Hang an item on the room's wall at `along`, even right next to a door."""
    s.mount(template, sl.FACING_OF_EDGE[edge], layout.inner_face(room, edge, along), along, z)


def stand_off_wall(s, layout, room, edge, along, template, depth):
    face = layout.inner_face(room, edge, along)
    dx, dy = sl.INWARD[sl.FACING_OF_EDGE[edge]]
    x, y = (along, face + dy * depth) if edge in "NS" else (face + dx * depth, along)
    s.stand(template, x, y)


def door_sides(layout):
    for host, sides in layout.door_hosts:
        for room, edge, _, start in sides:
            yield room, edge, start


def rooms_of(layout, kinds):
    return [room for room in layout.rooms if room.kind in kinds]


# ----- Cameras -----


def cameras(s, layout):
    for room in layout.rooms:
        if (room.kind in NO_CAMERA and room.kind != "tunnel") or room.kind.startswith("old_") or room.name.startswith("old_"):
            continue
        far = sl.far_edge(layout, room)
        if room.corridor:
            long = sl.long_edges(room)
            low, high = edge_range(room, long[0])
            for k, along in enumerate(spaced(low, high, CAMERA_STEP, CAMERA_OFFSET)):
                wall_decor(s, layout, room, long[0], "decor_cctv", along, 0.0)
            low, high = edge_range(room, long[1])
            for along in spaced(low + CAMERA_STEP / 2, high, CAMERA_STEP, CAMERA_OFFSET):
                wall_decor(s, layout, room, long[1], "decor_cctv_dome", along, 0.0)
        elif room.kind == "tunnel":
            long = sl.long_edges(room)
            low, high = edge_range(room, long[0])
            for along in spaced(low, high, TUNNEL_CAMERA_STEP, 3.0):
                wall_decor(s, layout, room, long[0], "decor_cctv", along, 0.0)
        else:
            low, high = edge_range(room, far)
            wall_decor(s, layout, room, far, "decor_cctv_dome", low + 1.6, 0.0)
            if (room.x1 - room.x0) * (room.y1 - room.y0) >= BIG_ROOM:
                wall_decor(s, layout, room, far, "decor_cctv_dome", high - 1.6, 0.0)


# ----- Emergency cabinets and oxygen -----


def cabinets(s, layout):
    for room in layout.rooms:
        area = (room.x1 - room.x0) * (room.y1 - room.y0)
        if room.kind in ("cabin", "tunnel", "maint_room") or room.kind.startswith("old_"):
            continue
        if room.corridor:
            long = sl.long_edges(room)
            accents(s, layout, room, long[:1], "decor_emergency_cabinet", CABINET_STEP, 0.0, offset=0)
            continue
        if area < MID_ROOM and room.kind != "airlock_prep":
            continue
        edges = layout.door_edges(room)
        edge = edges[0] if edges else "N"
        start = layout.door_spans[(room.index, edge)][0] if edges else 0
        low, high = edge_range(room, edge)
        along = start + 1 + CABINET_BY_DOOR
        if along > high - 1.5:
            along = start + 1 - CABINET_BY_DOOR
        wall_decor(s, layout, room, edge, "decor_emergency_cabinet", along, 0.0)
        if area >= BIG_ROOM:
            far = sl.far_edge(layout, room)
            lowf, highf = edge_range(room, far)
            wall_decor(s, layout, room, far, "decor_emergency_cabinet", (lowf + highf) / 2 + 3.0, 0.0)
    for room in rooms_of(layout, ("airlock_prep",)):
        long = sl.long_edges(room)
        low, high = edge_range(room, long[0])
        for along in (low + 1.5, high - 1.5):
            stand_off_wall(s, layout, room, long[1], along, "decor_o2_tank", 0.5)
    for room in rooms_of(layout, ("engine", "atmos", "dock", "tools", "mining")):
        far = sl.far_edge(layout, room)
        low, high = edge_range(room, far)
        wall_decor(s, layout, room, far, "decor_fire_hose", (low + high) / 2 - 4.0, 0.0)
        stand_off_wall(s, layout, room, far, (low + high) / 2 + 4.0, "decor_o2_tank", 0.5)
    for room in rooms_of(layout, ("wards", "surgery", "cryo")):
        for edge in "NS":
            low, high = edge_range(room, edge)
            for along in spaced(low, high, 6.0, 3.0):
                wall_decor(s, layout, room, edge, "decor_o2_outlet", along, 0.0)


# ----- Vending -----


def vending_pair(s, layout, room, edge, along):
    wall_decor(s, layout, room, edge, "decor_vending_snack", along, 0.0)
    wall_decor(s, layout, room, edge, "decor_vending_cigs", along + PAIR_GAP + 0.45, 0.0)
    stand_off_wall(s, layout, room, edge, along + 2 * PAIR_GAP, "decor_ashtray_stand", ASHTRAY_DEPTH)


def vending(s, layout):
    by_name = layout.by_name
    for name, edge, offset in VENDING_SITES:
        room = by_name[name]
        low, _ = edge_range(room, edge)
        vending_pair(s, layout, room, edge, low + offset)
    for name in CORRIDOR_VENDING:
        room = by_name[name]
        long = sl.long_edges(room)
        low, high = edge_range(room, long[1])
        for fraction in (0.3, 0.7):
            vending_pair(s, layout, room, long[1], low + (high - low) * fraction)


# ----- Fittings -----


def doors_and_signs(s, layout):
    for room, edge, start in door_sides(layout):
        if room.kind in SECURE_KINDS:
            mount_at(s, layout, room, edge, start - 0.4, "decor_door_reader")
        if room.name in EXIT_ROOMS or room.name.startswith("lock_"):
            mount_at(s, layout, room, edge, start + 1.0, "decor_exit_sign")
        if room.kind in ("airlock", "armory", "checkpoint", "engine"):
            mount_at(s, layout, room, edge, start + 2.4, "decor_intercom")


def phones_and_boards(s, layout, rng):
    for room in rooms_of(layout, PHONE_KINDS):
        far = sl.far_edge(layout, room)
        low, high = edge_range(room, far)
        wall_decor(s, layout, room, far, "decor_wall_phone", low + 2.2, 0.0)
    for name in ("dining", "lounge", "mining", "hop", "mail", "cargo_bay", "dorm_hall", "gym", "library", "bar"):
        room = layout.by_name[name]
        edge = sl.long_edges(room)[1] if room.corridor else sl.far_edge(layout, room)
        low, high = edge_range(room, edge)
        wall_decor(s, layout, room, edge, "decor_notice_board", (low + high) / 2 - 2.5, 0.0)
    for name in ("c1", "c2", "c3", "m_w", "m_e"):
        room = layout.by_name[name]
        long = sl.long_edges(room)
        accents(s, layout, room, long[:1], "decor_notice_board", 60.0, 0.0, offset=20.0)
        accents(s, layout, room, long[:1], "decor_fire_alarm", 36.0, 0.0, offset=9.0)


def amenities(s, layout, rng):
    for room in rooms_of(layout, COOLER_KINDS):
        far = sl.far_edge(layout, room)
        stand_off_wall(s, layout, room, far, sum(edge_range(room, far)) / 2 + 5.0, "decor_water_cooler", 0.5)
    for room in rooms_of(layout, COAT_KINDS):
        s.stand("decor_coat_rack", room.x0 + 0.8, room.y1 - 0.8)
    for room in rooms_of(layout, DISPENSER_KINDS):
        for edge in ("N", "S"):
            accents(s, layout, room, edge, "decor_soap_dispenser", 3.6, 0.0, offset=1.0)
            accents(s, layout, room, edge, "decor_towel_dispenser", 3.6, 0.0, offset=2.2)
    for room in rooms_of(layout, BASKET_KINDS):
        s.stand("decor_wastebasket", room.x1 - 0.8, room.y0 + 0.8 + rng.random() * 2)
    for room in rooms_of(layout, PLANT_KINDS):
        s.stand("decor_fern", room.x1 - 1.0, room.y1 - 1.0)
        if room.kind == "atrium":
            for x, y in ((room.x0 + 1.0, room.y0 + 1.0), (room.x1 - 1.0, room.y0 + 1.0), (room.x0 + 1.0, room.y1 - 1.0)):
                s.stand("decor_fern", x, y)
    for room in rooms_of(layout, PAINTING_KINDS):
        edge = sl.far_edge(layout, room)
        low, high = edge_range(room, edge)
        wall_decor(s, layout, room, edge, "decor_painting", (low + high) / 2 - 3.0, 0.0)
    for name in ("lounge", "hop", "bridge"):
        room = layout.by_name[name]
        edge = sl.long_edges(room)[1]
        low, high = edge_range(room, edge)
        wall_decor(s, layout, room, edge, "decor_trophy_case", high - 2.0, 0.0)


def runs_and_markings(s, layout):
    for room in rooms_of(layout, RUN_KINDS):
        for edge in sl.long_edges(room):
            accents(s, layout, room, edge, "decor_pipe_run", 5.0, 0.0, offset=1.0)
        accents(s, layout, room, sl.long_edges(room)[1:], "decor_cable_tray", 5.0, 0.0, offset=3.0)
    engine = layout.by_name["engine"]
    for dx, dy in ((-3.0, 0.0), (3.0, 0.0), (0.0, -3.0), (0.0, 3.0)):
        s.stand("decor_floor_strip", engine.cx + dx, engine.cy + dy, "S" if dy else "E")
    for room in rooms_of(layout, ("airlock",)):
        s.stand("decor_floor_strip", room.cx, room.cy, "E" if (room.x1 - room.x0) >= (room.y1 - room.y0) else "S")
    dock = layout.by_name["cargo_dock"]
    for dy in (-6.0, 6.0):
        s.stand("decor_floor_strip", dock.x0 + 2.0, dock.cy + dy, "E")
    for name in ("c1", "c2", "c3", "m_w", "m_e"):
        room = layout.by_name[name]
        horizontal = (room.x1 - room.x0) >= (room.y1 - room.y0)
        low, high = (room.x0, room.x1) if horizontal else (room.y0, room.y1)
        for along in spaced(low, high, 20.0, 6.0):
            if horizontal:
                s.stand("decor_floor_arrow", along, room.cy, "W" if along > 80 else "E")
            else:
                s.stand("decor_floor_arrow", room.cx, along, "S")


def add_extras2(s, layout):
    rng = random.Random(SEED)
    cameras(s, layout)
    cabinets(s, layout)
    vending(s, layout)
    doors_and_signs(s, layout)
    phones_and_boards(s, layout, rng)
    amenities(s, layout, rng)
    runs_and_markings(s, layout)
