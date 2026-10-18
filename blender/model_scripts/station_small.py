"""Small props for the station: cups, plates, books, papers, tools, lab glass, a boombox and the like.

`build_small()` makes the templates; `add_small(builder, layout)` finds the tables, counters, beds and benches
already placed in the scene and puts about SMALL_PROP_COUNT props on them, picking the kind from what the room
is for, plus a few litter bins along the corridors. Placement is seeded, so the station comes out the same each time.
"""

import math
import random

import station_models as sm

Builder = sm.Builder
mat = sm.material

SMALL_ROW_Y = 120.0
SMALL_ROW_GAP = 4.0
SMALL_PER_ROW = 16
SMALL_STEP = 3.0
SMALL_PROP_COUNT = 190
PER_ROOM_CAP = 3
BIN_COUNT = 30
SMALL_SEED = 13
BIN_SPACING = 18.0

# surface kind: (top height, half length, half width, margin kept free in the middle)
SURFACES = {
    "decor_table": (0.77, 0.6, 0.25),
    "decor_prep_table": (0.94, 0.6, 0.25),
    "decor_counter": (0.95, 0.8, 0.15),
    "decor_coffee_table": (0.45, 0.4, 0.15),
    "decor_workbench": (0.9, 0.7, 0.25),
    "decor_bed": (0.5, 0.15, 0.7),
}

# which props suit which rooms
POOLS = {
    "food": ("mug", "plate", "glass", "bottle", "tray", "fruit_bowl", "kettle", "toaster", "coffee_machine", "knife_block"),
    "office": ("book_stack", "papers", "clipboard", "pen_cup", "desk_lamp", "floppy_stack", "cassette_stack", "mug"),
    "security": ("clipboard", "radio", "mug", "papers", "pen_cup"),
    "medical": ("clipboard", "syringe_tray", "pill_bottles", "beaker_set", "papers"),
    "science": ("beaker_set", "test_tube_rack", "flask", "clipboard", "papers", "petri_dishes"),
    "engineering": ("toolbox", "hard_hat", "cable_spool", "wrench", "clipboard", "oil_can"),
    "cargo": ("parcel_stack", "clipboard", "radio", "toolbox", "tape_roll"),
    "crew": ("boombox", "magazine", "cassette_stack", "chess_set", "mug", "book_stack", "alarm_clock"),
    "default": ("mug", "papers", "book_stack", "clipboard"),
}
GROUP_OF_KIND = {
    "kitchen": "food", "dining": "food", "bar": "food", "hydroponics": "food",
    "library": "office", "hop": "office", "office": "office", "meeting": "office", "bridge": "office",
    "captain": "office", "head_cabin": "office", "courtroom": "office", "pad_room": "office", "ai_core": "office",
    "checkpoint": "security", "warden": "security", "detective": "security", "armory": "security",
    "interrogation": "security", "brig": "security",
    "wards": "medical", "surgery": "medical", "cryo": "medical", "garage": "medical", "virology": "medical",
    "morgue": "medical", "genetics": "medical", "chemistry": "science",
    "labs": "science", "robotics": "science", "mech_bay": "science", "xenobio": "science", "toxins": "science",
    "server": "science",
    "engine": "engineering", "power": "engineering", "atmos": "engineering", "tools": "engineering",
    "telecom": "engineering", "disposal": "engineering", "solars": "engineering", "ore_processing": "cargo",
    "cargo_bay": "cargo", "cargo_receiving": "cargo", "mining": "cargo", "mail": "cargo",
    "lounge": "crew", "gym": "crew", "cabin": "crew", "chapel": "crew", "theatre": "crew",
}

PAPER = (0.95, 0.95, 0.9, 1.0)
BROWN = (0.5, 0.33, 0.2, 1.0)
CHINA = (0.95, 0.95, 0.92, 1.0)
COVERS = ((0.7, 0.15, 0.15, 1.0), (0.15, 0.35, 0.7, 1.0), (0.85, 0.7, 0.2, 1.0), (0.2, 0.55, 0.3, 1.0))
RED = (0.85, 0.15, 0.1, 1.0)
YELLOW = (0.95, 0.8, 0.1, 1.0)


def paper_mat():
    return mat("paper", PAPER, roughness=0.9)


def china_mat():
    return mat("china", CHINA, roughness=0.3)


def cover_mats():
    return [mat(f"cover_{i}", c, roughness=0.7) for i, c in enumerate(COVERS)]


def prop(name, build):
    """Wrap a drawing function into an item maker: build(b) draws around the origin, standing on Z = 0."""

    def make(origin):
        b = Builder(name)
        build(b)
        return b.finish(origin)

    make.__name__ = name
    make.item_name = name
    return make


def d_mug(b):
    b.cylinder_z(0, 0, 0.0, 0.04, 0.09, china_mat(), segments=10)
    b.box(0.035, -0.01, 0.03, 0.06, 0.01, 0.07, china_mat())


def d_plate(b):
    b.cylinder_z(0, 0, 0.0, 0.13, 0.015, china_mat(), segments=14)
    b.cylinder_z(0, 0, 0.015, 0.05, 0.02, mat("food", (0.8, 0.5, 0.25, 1.0), roughness=0.9), segments=8)


def d_glass(b):
    b.cylinder_z(0, 0, 0.0, 0.035, 0.12, sm.glass(), segments=10)
    b.cylinder_z(0, 0, 0.0, 0.03, 0.04, mat("drink", (0.9, 0.6, 0.15, 1.0), roughness=0.2), segments=10)


def d_bottle(b):
    bottle = mat("bottle_green", (0.2, 0.5, 0.25, 1.0), roughness=0.15)
    b.cylinder_z(0, 0, 0.0, 0.04, 0.2, bottle, segments=10)
    b.cylinder_z(0, 0, 0.2, 0.015, 0.08, bottle, segments=8)


def d_tray(b):
    metal = sm.steel()
    b.box(-0.2, -0.14, 0.0, 0.2, 0.14, 0.02, metal)
    b.box(-0.2, -0.14, 0.02, 0.2, -0.13, 0.04, metal)
    b.box(-0.2, 0.13, 0.02, 0.2, 0.14, 0.04, metal)
    b.box(-0.15, -0.08, 0.02, -0.03, 0.08, 0.04, mat("food", (0.8, 0.5, 0.25, 1.0), roughness=0.9))
    b.cylinder_z(0.1, 0.0, 0.02, 0.05, 0.05, china_mat(), segments=8)


def d_fruit_bowl(b):
    b.cylinder_z(0, 0, 0.0, 0.14, 0.06, china_mat(), segments=12)
    for dx, dy, color in ((-0.05, 0.0, RED), (0.05, 0.03, YELLOW), (0.0, -0.05, (0.3, 0.7, 0.2, 1.0))):
        b.box(dx - 0.035, dy - 0.035, 0.06, dx + 0.035, dy + 0.035, 0.12, mat(f"fruit_{int(color[0] * 10)}", color, roughness=0.5))


def d_kettle(b):
    b.cylinder_z(0, 0, 0.0, 0.1, 0.18, sm.steel(), segments=12)
    b.box(0.09, -0.015, 0.1, 0.17, 0.015, 0.14, sm.dark_steel())
    b.box(-0.02, -0.02, 0.18, 0.02, 0.02, 0.22, sm.rubber())


def d_toaster(b):
    b.box(-0.15, -0.1, 0.0, 0.15, 0.1, 0.17, sm.steel())
    b.box(-0.1, -0.06, 0.17, 0.1, -0.02, 0.18, sm.dark_steel())
    b.box(-0.1, 0.02, 0.17, 0.1, 0.06, 0.18, sm.dark_steel())
    b.box(0.17, -0.02, 0.1, 0.2, 0.02, 0.14, sm.rubber())


def d_coffee_machine(b):
    b.box(-0.15, -0.12, 0.0, 0.15, 0.12, 0.35, sm.beige())
    b.box(-0.1, -0.14, 0.05, 0.1, -0.12, 0.12, sm.dark_steel())
    b.cylinder_z(0, -0.03, 0.12, 0.05, 0.12, sm.glass(), segments=8)
    b.box(-0.1, -0.13, 0.28, 0.1, -0.12, 0.33, sm.led(sm.LED_RED, "led_red"))


def d_knife_block(b):
    b.box(-0.08, -0.06, 0.0, 0.08, 0.06, 0.22, mat("wood_block", sm_wood(), roughness=0.8))
    for i in range(4):
        b.box(-0.06 + i * 0.04, -0.07, 0.22, -0.04 + i * 0.04, -0.05, 0.27, sm.rubber())


def sm_wood():
    return (0.5, 0.33, 0.2, 1.0)


def d_book_stack(b):
    for i, cover in enumerate(cover_mats()[:3]):
        b.box(-0.1 + i * 0.01, -0.07, i * 0.035, 0.1 - i * 0.01, 0.07, (i + 1) * 0.035, cover)
    b.box(-0.09, -0.065, 0.0, 0.09, -0.06, 0.1, paper_mat())


def d_papers(b):
    b.box(-0.1, -0.14, 0.0, 0.1, 0.14, 0.03, paper_mat())
    b.box(-0.09, -0.12, 0.03, 0.1, 0.13, 0.04, paper_mat())


def d_clipboard(b):
    b.box(-0.1, -0.14, 0.0, 0.1, 0.14, 0.012, mat("clipboard", BROWN, roughness=0.8))
    b.box(-0.085, -0.12, 0.012, 0.085, 0.1, 0.016, paper_mat())
    b.box(-0.04, 0.1, 0.012, 0.04, 0.14, 0.03, sm.steel())


def d_pen_cup(b):
    b.cylinder_z(0, 0, 0.0, 0.04, 0.09, sm.dark_steel(), segments=10)
    for dx, color in ((-0.015, RED), (0.0, (0.1, 0.2, 0.7, 1.0)), (0.015, sm.PHOSPHOR_GREEN)):
        b.box(dx - 0.004, -0.004, 0.09, dx + 0.004, 0.004, 0.16, mat(f"pen_{int(dx * 1000)}", color, roughness=0.5))


def d_desk_lamp(b):
    b.cylinder_z(0, 0, 0.0, 0.07, 0.02, sm.dark_steel(), segments=10)
    b.box(-0.01, -0.01, 0.02, 0.01, 0.01, 0.28, sm.steel())
    b.slanted_slab(-0.05, 0.05, 0.0, -0.1, 0.1, 0.26, 0.34, mat("lamp_glow_small", sm.LAMP_WARM, emission=6.0))


def d_floppy_stack(b):
    black = sm.rubber()
    for i in range(3):
        b.box(-0.07 + i * 0.01, -0.07, i * 0.006, 0.07 + i * 0.01, 0.07, (i + 1) * 0.006, black)
    b.box(-0.03, -0.07, 0.018, 0.03, -0.03, 0.02, sm.steel())


def d_cassette_stack(b):
    for i in range(3):
        b.box(-0.055, -0.035, i * 0.016, 0.055, 0.035, (i + 1) * 0.016, mat(f"tape_{i}", COVERS[i], roughness=0.5))


def d_boombox(b):
    body, speaker = sm.dark_steel(), sm.rubber()
    b.box(-0.28, -0.07, 0.0, 0.28, 0.07, 0.18, body)
    for x in (-0.17, 0.17):
        b.cylinder(x, -0.07, 0.09, 0.07, 0.015, speaker, segments=12)
    b.box(-0.07, -0.072, 0.1, 0.07, -0.07, 0.16, sm.phosphor(sm.PHOSPHOR_AMBER, "crt_amber"))
    b.box(-0.2, -0.02, 0.18, 0.2, 0.02, 0.21, sm.steel())


def d_radio(b):
    b.box(-0.05, -0.03, 0.0, 0.05, 0.03, 0.16, sm.dark_steel())
    b.box(0.03, -0.005, 0.16, 0.035, 0.005, 0.27, sm.steel())
    b.box(-0.03, -0.032, 0.1, 0.03, -0.03, 0.14, sm.phosphor(sm.PHOSPHOR_GREEN, "crt_green"))


def d_toolbox(b):
    b.box(-0.22, -0.1, 0.0, 0.22, 0.1, 0.16, mat("toolbox_red", RED, roughness=0.5))
    b.box(-0.2, -0.015, 0.16, 0.2, 0.015, 0.2, sm.steel())
    b.box(-0.02, -0.101, 0.07, 0.02, -0.1, 0.1, sm.steel())


def d_hard_hat(b):
    b.cylinder_z(0, 0, 0.0, 0.14, 0.015, mat("hat_yellow", YELLOW, roughness=0.5), segments=14)
    b.cylinder_z(0, 0, 0.015, 0.1, 0.08, mat("hat_yellow", YELLOW, roughness=0.5), segments=14)


def d_cable_spool(b):
    b.cylinder(0, 0.07, 0.15, 0.15, 0.02, sm.dark_steel(), segments=14)
    b.cylinder(0, -0.05, 0.15, 0.15, 0.02, sm.dark_steel(), segments=14)
    b.cylinder(0, 0.05, 0.15, 0.09, 0.1, mat("cable", (0.1, 0.1, 0.12, 1.0), roughness=0.9), segments=12)


def d_wrench(b):
    b.box(-0.015, -0.14, 0.0, 0.015, 0.1, 0.01, sm.steel())
    b.box(-0.04, 0.1, 0.0, 0.04, 0.16, 0.012, sm.steel())


def d_oil_can(b):
    b.cylinder_z(0, 0, 0.0, 0.05, 0.12, sm.steel(), segments=10)
    b.box(0.0, -0.01, 0.1, 0.12, 0.01, 0.13, sm.steel())
    b.box(0.1, -0.01, 0.12, 0.13, 0.01, 0.2, sm.steel())


def d_parcel_stack(b):
    cardboard = mat("cardboard", (0.65, 0.5, 0.3, 1.0), roughness=0.9)
    b.box(-0.18, -0.12, 0.0, 0.18, 0.12, 0.14, cardboard)
    b.box(-0.12, -0.09, 0.14, 0.12, 0.09, 0.26, cardboard)
    b.box(-0.18, -0.005, 0.0, 0.18, 0.005, 0.14, mat("tape", (0.9, 0.8, 0.5, 1.0), roughness=0.4))


def d_tape_roll(b):
    b.cylinder_z(0, 0, 0.0, 0.05, 0.05, mat("tape", (0.9, 0.8, 0.5, 1.0), roughness=0.4), segments=12)


def d_beaker_set(b):
    liquids = (mat("liquid_blue", (0.2, 0.5, 0.9, 1.0), roughness=0.2), mat("liquid_green", (0.3, 0.8, 0.4, 1.0), roughness=0.2), mat("liquid_red", (0.85, 0.2, 0.2, 1.0), roughness=0.2))
    for i, x in enumerate((-0.1, 0.0, 0.1)):
        b.cylinder_z(x, 0.0, 0.0, 0.035, 0.1 + i * 0.02, sm.glass(), segments=10)
        b.cylinder_z(x, 0.0, 0.0, 0.03, 0.05 + i * 0.01, liquids[i], segments=10)


def d_test_tube_rack(b):
    b.box(-0.12, -0.04, 0.0, 0.12, 0.04, 0.03, mat("wood_block", sm_wood(), roughness=0.8))
    for i in range(5):
        b.cylinder_z(-0.09 + i * 0.045, 0.0, 0.03, 0.012, 0.12, sm.glass(), segments=6)


def d_flask(b):
    b.cylinder_z(0, 0, 0.0, 0.07, 0.04, sm.glass(), segments=12)
    b.cylinder_z(0, 0, 0.04, 0.045, 0.05, sm.glass(), segments=12)
    b.cylinder_z(0, 0, 0.09, 0.02, 0.08, sm.glass(), segments=8)
    b.cylinder_z(0, 0, 0.0, 0.06, 0.03, mat("liquid_green", (0.3, 0.8, 0.4, 1.0), roughness=0.2), segments=12)


def d_petri_dishes(b):
    for i in range(3):
        b.cylinder_z(0, 0, i * 0.02, 0.07, 0.018, sm.glass(), segments=14)
    b.cylinder_z(0, 0, 0.056, 0.05, 0.004, mat("agar", (0.9, 0.85, 0.5, 1.0), roughness=0.3), segments=12)


def d_syringe_tray(b):
    b.box(-0.15, -0.1, 0.0, 0.15, 0.1, 0.015, sm.steel())
    for i in range(4):
        b.box(-0.1 + i * 0.06, -0.07, 0.015, -0.085 + i * 0.06, 0.07, 0.03, sm.glass())
        b.box(-0.1 + i * 0.06, 0.07, 0.015, -0.085 + i * 0.06, 0.09, 0.025, sm.steel())


def d_pill_bottles(b):
    orange = mat("pill_orange", (0.9, 0.5, 0.1, 1.0), roughness=0.4)
    for x in (-0.06, 0.0, 0.06):
        b.cylinder_z(x, 0.0, 0.0, 0.025, 0.08, orange, segments=8)
        b.cylinder_z(x, 0.0, 0.08, 0.026, 0.015, china_mat(), segments=8)


def d_magazine(b):
    b.box(-0.11, -0.15, 0.0, 0.11, 0.15, 0.008, mat("magazine", COVERS[0], roughness=0.5))
    b.box(-0.07, -0.08, 0.008, 0.07, 0.0, 0.009, mat("magazine_title", YELLOW, roughness=0.5))


def d_chess_set(b):
    dark, light = mat("chess_dark", (0.2, 0.12, 0.08, 1.0), roughness=0.6), mat("chess_light", (0.85, 0.75, 0.55, 1.0), roughness=0.6)
    b.box(-0.2, -0.2, 0.0, 0.2, 0.2, 0.02, dark)
    for i in range(4):
        for j in range(4):
            if (i + j) % 2 == 0:
                b.box(-0.2 + i * 0.1, -0.2 + j * 0.1, 0.02, -0.1 + i * 0.1, -0.1 + j * 0.1, 0.021, light)
    for x, y, tone in ((-0.15, -0.15, light), (0.05, -0.05, dark), (-0.05, 0.15, light), (0.15, 0.05, dark)):
        b.cylinder_z(x, y, 0.021, 0.02, 0.06, tone, segments=8)


def d_alarm_clock(b):
    b.box(-0.06, -0.035, 0.0, 0.06, 0.035, 0.09, sm.dark_steel())
    b.box(-0.045, -0.037, 0.03, 0.045, -0.035, 0.07, mat("clock_red", (1.0, 0.1, 0.05, 1.0), emission=6.0))


SMALL_DRAWINGS = {
    "mug": d_mug, "plate": d_plate, "glass": d_glass, "bottle": d_bottle, "tray": d_tray, "fruit_bowl": d_fruit_bowl,
    "kettle": d_kettle, "toaster": d_toaster, "coffee_machine": d_coffee_machine, "knife_block": d_knife_block,
    "book_stack": d_book_stack, "papers": d_papers, "clipboard": d_clipboard, "pen_cup": d_pen_cup,
    "desk_lamp": d_desk_lamp, "floppy_stack": d_floppy_stack, "cassette_stack": d_cassette_stack, "boombox": d_boombox,
    "radio": d_radio, "toolbox": d_toolbox, "hard_hat": d_hard_hat, "cable_spool": d_cable_spool, "wrench": d_wrench,
    "oil_can": d_oil_can, "parcel_stack": d_parcel_stack, "tape_roll": d_tape_roll, "beaker_set": d_beaker_set,
    "test_tube_rack": d_test_tube_rack, "flask": d_flask, "petri_dishes": d_petri_dishes,
    "syringe_tray": d_syringe_tray, "pill_bottles": d_pill_bottles, "magazine": d_magazine, "chess_set": d_chess_set,
    "alarm_clock": d_alarm_clock,
}


def d_trash_can(b):
    b.cylinder_z(0, 0, 0.0, 0.22, 0.7, sm.gunmetal(), segments=12)
    b.cylinder_z(0, 0, 0.7, 0.24, 0.05, sm.dark_steel(), segments=12)
    b.box(-0.04, -0.25, 0.55, 0.04, -0.2, 0.6, sm.hazard())


SMALL_DRAWINGS["trash_can"] = d_trash_can


def build_small():
    objects = []
    for i, (name, draw) in enumerate(SMALL_DRAWINGS.items()):
        row, col = divmod(i, SMALL_PER_ROW)
        objects.append(prop(f"decor_small_{name}", draw)((col * SMALL_STEP, SMALL_ROW_Y + row * SMALL_ROW_GAP, 0.0)))
    return objects


def surface_objects(s):
    """(template, x, y, yaw) of every table, counter, bed and so on already placed."""
    found = []
    for obj in s.collection.objects:
        base = obj.name.split(".")[0]
        if base in SURFACES:
            found.append((base, obj.location.x, obj.location.y, obj.rotation_euler.z))
    return found


def room_group(layout, x, y):
    room = layout.room_at((int(math.floor(x)), int(math.floor(y))))
    if room is None:
        return None, "default"
    return room, GROUP_OF_KIND.get(room.kind, "default")


def add_small(s, layout):
    rng = random.Random(SMALL_SEED)
    surfaces = surface_objects(s)
    rng.shuffle(surfaces)
    placed = 0
    per_room = {}
    for base, x, y, yaw in surfaces:
        if placed >= SMALL_PROP_COUNT:
            break
        top, half_x, half_y = SURFACES[base]
        room, group = room_group(layout, x, y)
        if room is None or room.corridor or per_room.get(room.name, 0) >= PER_ROOM_CAP:
            continue
        per_room[room.name] = per_room.get(room.name, 0) + 1
        name = rng.choice(POOLS[group])
        local = (rng.choice((-1, 1)) * rng.uniform(half_x * 0.4, half_x), rng.uniform(-half_y, half_y))
        dx = local[0] * math.cos(yaw) - local[1] * math.sin(yaw)
        dy = local[0] * math.sin(yaw) + local[1] * math.cos(yaw)
        s._instance(f"decor_small_{name}", (x + dx, y + dy, top), yaw + rng.uniform(-0.6, 0.6))
        placed += 1
    bins = 0
    for room in layout.rooms:
        if not room.corridor or bins >= BIN_COUNT:
            continue
        low, high = (room.x0, room.x1) if (room.x1 - room.x0) >= (room.y1 - room.y0) else (room.y0, room.y1)
        along = low + BIN_SPACING / 2
        while along < high - 2 and bins < BIN_COUNT:
            if (room.x1 - room.x0) >= (room.y1 - room.y0):
                s._instance("decor_small_trash_can", (along, room.y0 + 0.5, 0.0))
            else:
                s._instance("decor_small_trash_can", (room.x0 + 0.5, along, 0.0))
            bins += 1
            along += BIN_SPACING
    return placed, bins
