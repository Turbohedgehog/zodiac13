"""Safety, access and amenity items for the station: emergency cabinets with an extinguisher and oxygen bottles,
snack and cigarette machines, door card readers, intercoms, wall phones, notice boards, exit signs, fire alarms,
fire hose reels, water coolers, dispensers, cable runs, floor markings and a few plants and pictures.

Same conventions as before: metres, wall items have their back on Y = 0 and look towards -Y, standing items
are centred on their footprint on Z = 0, floor markings are thin plates on Z = 0.
"""

import station_models as sm

Builder = sm.Builder
mat = sm.material

ITEM3_ROW_Y = 150.0
ITEM3_ROW_GAP = 5.0
ITEM3_PER_ROW = 14
ITEM3_STEP = 3.5

RED = (0.85, 0.12, 0.1, 1.0)
O2_GREEN = (0.15, 0.6, 0.3, 1.0)
WHITE = (0.95, 0.96, 0.98, 1.0)
EXIT_GREEN = (0.1, 0.9, 0.35, 1.0)
CORK = (0.6, 0.45, 0.25, 1.0)
WOOD = (0.5, 0.33, 0.2, 1.0)
HAZARD_Y = (0.95, 0.8, 0.1, 1.0)
PACK_COLORS = ((0.9, 0.2, 0.15, 1.0), (0.95, 0.75, 0.15, 1.0), (0.2, 0.55, 0.9, 1.0), (0.25, 0.7, 0.35, 1.0), (0.85, 0.4, 0.1, 1.0))
CIG_COLORS = ((0.9, 0.9, 0.85, 1.0), (0.85, 0.15, 0.15, 1.0), (0.15, 0.3, 0.75, 1.0), (0.85, 0.7, 0.2, 1.0))
CANVAS = ((0.25, 0.45, 0.7, 1.0), (0.3, 0.6, 0.35, 1.0), (0.8, 0.55, 0.25, 1.0))


def red():
    return mat("emergency_red", RED, roughness=0.4)


def o2():
    return mat("o2_green", O2_GREEN, metallic=0.4, roughness=0.5)


def white():
    return mat("sheet", WHITE, roughness=0.5)


# ----- Emergency equipment -----


def decor_emergency_cabinet(origin):
    """Red wall cabinet 0.8 x 0.3 x 1.1 m with a glass door: an extinguisher and two oxygen bottles with a mask; back on Y = 0."""
    box, glass, extinguisher, tank, valve = red(), sm.glass(), red(), o2(), sm.steel()
    b = Builder("decor_emergency_cabinet")
    b.box(-0.4, -0.3, 0.9, 0.4, 0.0, 2.0, box)
    b.box(-0.36, -0.31, 0.94, 0.36, -0.3, 1.96, glass)
    b.cylinder_z(-0.22, -0.12, 0.98, 0.07, 0.5, extinguisher, segments=10)
    b.box(-0.25, -0.15, 1.45, -0.19, -0.09, 1.52, sm.dark_steel())
    for x in (0.0, 0.2):
        b.cylinder_z(x, -0.12, 0.98, 0.055, 0.6, tank, segments=10)
        b.cylinder_z(x, -0.12, 1.58, 0.02, 0.06, valve, segments=6)
    b.box(0.1, -0.2, 1.7, 0.3, -0.12, 1.85, sm.rubber())
    b.box(-0.3, -0.32, 1.97, 0.3, -0.31, 2.0, mat("sign_white", WHITE, roughness=0.5))
    return b.finish(origin)


def decor_o2_tank(origin):
    """Oxygen bottle on a chained stand, 1.4 m tall, centred."""
    tank = o2()
    b = Builder("decor_o2_tank")
    b.box(-0.18, -0.18, 0.0, 0.18, 0.18, 0.04, sm.gunmetal())
    b.cylinder_z(0, 0, 0.04, 0.1, 1.1, tank, segments=12)
    b.cylinder_z(0, 0, 1.14, 0.06, 0.1, tank, segments=10)
    b.box(-0.05, -0.05, 1.24, 0.05, 0.05, 1.3, sm.steel())
    b.box(-0.11, -0.11, 0.8, 0.11, 0.11, 0.84, sm.dark_steel())
    return b.finish(origin)


def decor_fire_hose(origin):
    """Wall fire hose reel in a red cabinet, 0.8 m across; back on Y = 0."""
    b = Builder("decor_fire_hose")
    b.box(-0.4, -0.3, 0.9, 0.4, 0.0, 1.8, red())
    b.cylinder(0.0, -0.3, 1.35, 0.3, 0.12, mat("hose", (0.85, 0.8, 0.7, 1.0), roughness=0.8), segments=18)
    b.cylinder(0.0, -0.42, 1.35, 0.08, 0.06, sm.steel(), segments=10)
    b.box(0.28, -0.12, 0.92, 0.38, -0.02, 1.0, sm.steel())
    return b.finish(origin)


def decor_fire_alarm(origin):
    """Wall pull station and bell: a red box with a pull handle above a small bell; back on Y = 0."""
    b = Builder("decor_fire_alarm")
    b.box(-0.07, -0.05, 1.1, 0.07, 0.0, 1.35, red())
    b.box(-0.05, -0.06, 1.18, 0.05, -0.05, 1.24, mat("sign_white", WHITE, roughness=0.5))
    b.cylinder(0.0, -0.05, 1.55, 0.07, 0.05, sm.steel(), segments=10)
    b.box(-0.01, -0.06, 1.35, 0.01, -0.05, 1.5, sm.dark_steel())
    return b.finish(origin)


def decor_first_aid(origin):
    """White first-aid box with a red cross; back on Y = 0."""
    b = Builder("decor_first_aid")
    b.box(-0.2, -0.12, 1.2, 0.2, 0.0, 1.55, white())
    b.box(-0.03, -0.13, 1.3, 0.03, -0.12, 1.47, red())
    b.box(-0.09, -0.13, 1.36, 0.09, -0.12, 1.41, red())
    return b.finish(origin)


def decor_o2_outlet(origin):
    """Wall oxygen outlet with a gauge and a flow dial for the wards; back on Y = 0."""
    b = Builder("decor_o2_outlet")
    b.box(-0.18, -0.08, 1.1, 0.18, 0.0, 1.5, sm.steel())
    b.cylinder(-0.07, -0.08, 1.35, 0.06, 0.02, sm.beige(), segments=12)
    b.cylinder(0.08, -0.08, 1.35, 0.04, 0.04, o2(), segments=8)
    b.box(-0.1, -0.085, 1.15, 0.1, -0.08, 1.2, mat("sign_white", WHITE, roughness=0.5))
    return b.finish(origin)


# ----- Vending -----


def decor_vending_snack(origin):
    """80s snack machine, 0.9 x 0.7 x 1.9 m: a red body with a lit window of spiral rows of packs and a coin slot; back on Y = 0."""
    body = mat("snack_red", (0.8, 0.12, 0.12, 1.0), roughness=0.5)
    packs = [mat(f"pack_{i}", c, roughness=0.6) for i, c in enumerate(PACK_COLORS)]
    b = Builder("decor_vending_snack")
    b.box(-0.45, -0.7, 0.0, 0.45, 0.0, 1.9, body)
    b.box(-0.38, -0.71, 0.75, 0.2, -0.7, 1.8, mat("vend_light", (1.0, 0.95, 0.8, 1.0), emission=2.5))
    for row in range(5):
        for col in range(4):
            b.box(-0.35 + col * 0.14, -0.74, 0.82 + row * 0.19, -0.27 + col * 0.14, -0.71, 0.96 + row * 0.19, packs[(row + col) % len(packs)])
    b.box(0.25, -0.72, 1.35, 0.4, -0.7, 1.5, sm.dark_steel())
    b.box(0.28, -0.73, 1.38, 0.37, -0.72, 1.42, sm.led(sm.LED_AMBER, "led_amber"))
    b.box(-0.3, -0.72, 0.15, 0.3, -0.7, 0.45, sm.dark_steel())
    return b.finish(origin)


def decor_vending_cigs(origin):
    """80s cigarette machine, 0.8 x 0.5 x 1.5 m: a blue body with columns of packs and pull knobs; back on Y = 0."""
    body = mat("cig_blue", (0.15, 0.25, 0.55, 1.0), roughness=0.5)
    packs = [mat(f"cigpack_{i}", c, roughness=0.6) for i, c in enumerate(CIG_COLORS)]
    b = Builder("decor_vending_cigs")
    b.box(-0.4, -0.5, 0.0, 0.4, 0.0, 1.5, body)
    b.box(-0.33, -0.51, 0.55, 0.33, -0.5, 1.4, sm.screen_glass())
    for row in range(4):
        for col in range(6):
            b.box(-0.3 + col * 0.1, -0.53, 0.6 + row * 0.2, -0.22 + col * 0.1, -0.5, 0.74 + row * 0.2, packs[(row * 2 + col) % len(packs)])
    for col in range(6):
        b.cylinder(-0.26 + col * 0.1, -0.52, 0.5, 0.025, 0.03, sm.hazard(), segments=8)
    b.box(0.0, -0.52, 0.12, 0.3, -0.5, 0.3, sm.dark_steel())
    return b.finish(origin)


def decor_ashtray_stand(origin):
    """Standing ashtray: a steel column with a sand-filled top, 0.9 m tall, centred."""
    b = Builder("decor_ashtray_stand")
    b.cylinder_z(0, 0, 0.0, 0.14, 0.03, sm.dark_steel(), segments=10)
    b.cylinder_z(0, 0, 0.03, 0.04, 0.8, sm.steel(), segments=8)
    b.cylinder_z(0, 0, 0.83, 0.13, 0.09, sm.gunmetal(), segments=12)
    b.cylinder_z(0, 0, 0.9, 0.11, 0.015, mat("sand", (0.75, 0.65, 0.45, 1.0), roughness=1.0), segments=12)
    return b.finish(origin)


# ----- Access and communication -----


def decor_door_reader(origin):
    """Door card reader with a keypad, a card slot and a status light, 0.14 m wide at 1.2 m; back on Y = 0."""
    body, keys = sm.dark_steel(), sm.beige()
    b = Builder("decor_door_reader")
    b.box(-0.07, -0.04, 1.05, 0.07, 0.0, 1.4, body)
    b.box(-0.045, -0.045, 1.25, 0.045, -0.04, 1.28, sm.rubber())
    for row in range(3):
        for col in range(3):
            b.box(-0.045 + col * 0.032, -0.05, 1.08 + row * 0.04, -0.025 + col * 0.032, -0.04, 1.1 + row * 0.04, keys)
    b.box(-0.02, -0.045, 1.33, 0.02, -0.04, 1.37, sm.led(sm.LED_GREEN, "led_green"))
    return b.finish(origin)


def decor_intercom(origin):
    """Wall intercom: a grille, a talk button and a lit indicator; back on Y = 0."""
    b = Builder("decor_intercom")
    b.box(-0.09, -0.04, 1.2, 0.09, 0.0, 1.5, sm.gunmetal())
    for i in range(5):
        b.box(-0.06, -0.045, 1.37 + i * 0.02, 0.06, -0.04, 1.385 + i * 0.02, sm.rubber())
    b.cylinder(0.0, -0.045, 1.28, 0.025, 0.015, sm.hazard(), segments=8)
    b.box(0.05, -0.045, 1.43, 0.075, -0.04, 1.46, sm.led(sm.LED_RED, "led_red"))
    return b.finish(origin)


def decor_wall_phone(origin):
    """Wall telephone with a coiled-cord handset, beige; back on Y = 0."""
    b = Builder("decor_wall_phone")
    b.box(-0.1, -0.08, 1.3, 0.1, 0.0, 1.65, sm.beige())
    b.box(-0.2, -0.1, 1.5, -0.1, -0.05, 1.6, sm.beige_dark())
    b.box(0.1, -0.1, 1.5, 0.2, -0.05, 1.6, sm.beige_dark())
    b.box(-0.2, -0.1, 1.5, 0.2, -0.07, 1.54, sm.beige_dark())
    for col in range(3):
        for row in range(3):
            b.box(-0.06 + col * 0.045, -0.085, 1.34 + row * 0.04, -0.03 + col * 0.045, -0.08, 1.37 + row * 0.04, sm.rubber())
    return b.finish(origin)


def decor_exit_sign(origin):
    """Green emergency exit sign with an arrow, above doors; back on Y = 0."""
    glow = mat("exit_glow", EXIT_GREEN, emission=5.0)
    b = Builder("decor_exit_sign")
    b.box(-0.25, -0.06, 2.7, 0.25, 0.0, 2.95, sm.dark_steel())
    b.box(-0.2, -0.065, 2.74, 0.2, -0.06, 2.91, glow)
    b.box(-0.13, -0.068, 2.8, 0.09, -0.065, 2.85, sm.dark_steel())
    return b.finish(origin)


def decor_notice_board(origin):
    """Cork notice board 1.2 x 0.8 m with pinned papers; back on Y = 0."""
    import random

    rng = random.Random(sm.FLOWER_SEED)
    cork, paper = mat("cork", CORK, roughness=1.0), mat("paper", (0.95, 0.95, 0.9, 1.0), roughness=0.9)
    b = Builder("decor_notice_board")
    b.box(-0.6, -0.03, 1.1, 0.6, 0.0, 1.9, sm.dark_steel())
    b.box(-0.57, -0.035, 1.13, 0.57, -0.03, 1.87, cork)
    for _ in range(7):
        x, z = rng.uniform(-0.5, 0.35), rng.uniform(1.17, 1.65)
        b.box(x, -0.038, z, x + 0.2, -0.035, z + 0.25, paper)
    return b.finish(origin)


# ----- Amenities -----


def decor_water_cooler(origin):
    """Water cooler: a blue bottle on a cabinet with two taps, 1.5 m, centred."""
    body, bottle = sm.beige(), mat("water_blue", (0.4, 0.65, 0.9, 1.0), roughness=0.2)
    b = Builder("decor_water_cooler")
    b.box(-0.17, -0.17, 0.0, 0.17, 0.17, 0.95, body)
    b.cylinder_z(0, 0, 0.95, 0.14, 0.45, bottle, segments=12)
    b.box(-0.07, -0.19, 0.7, -0.03, -0.17, 0.75, mat("tap_blue", (0.2, 0.4, 0.8, 1.0), roughness=0.4))
    b.box(0.03, -0.19, 0.7, 0.07, -0.17, 0.75, red())
    return b.finish(origin)


def decor_coat_rack(origin):
    """Standing coat rack with six hooks and a coat, 1.8 m, centred."""
    b = Builder("decor_coat_rack")
    b.cylinder_z(0, 0, 0.0, 0.2, 0.03, sm.dark_steel(), segments=10)
    b.cylinder_z(0, 0, 0.03, 0.02, 1.7, sm.dark_steel(), segments=6)
    for dx, dy in ((0.12, 0), (-0.12, 0), (0, 0.12), (0, -0.12)):
        b.box(dx - 0.015, dy - 0.015, 1.6, dx + 0.015, dy + 0.015, 1.7, sm.dark_steel())
    b.box(-0.15, -0.04, 1.0, -0.05, 0.04, 1.6, mat("coat_grey", (0.35, 0.35, 0.4, 1.0), roughness=0.9))
    return b.finish(origin)


def decor_soap_dispenser(origin):
    """Wall soap dispenser, white with a pump; back on Y = 0."""
    b = Builder("decor_soap_dispenser")
    b.box(-0.05, -0.08, 1.15, 0.05, 0.0, 1.35, white())
    b.box(-0.015, -0.12, 1.2, 0.015, -0.08, 1.24, sm.steel())
    return b.finish(origin)


def decor_towel_dispenser(origin):
    """Wall paper towel dispenser, 0.3 m wide; back on Y = 0."""
    b = Builder("decor_towel_dispenser")
    b.box(-0.15, -0.15, 1.2, 0.15, 0.0, 1.5, sm.beige_dark())
    b.box(-0.1, -0.16, 1.18, 0.1, -0.12, 1.22, white())
    return b.finish(origin)


def decor_wastebasket(origin):
    """Small wire wastebasket, 0.4 m tall, centred."""
    b = Builder("decor_wastebasket")
    b.cylinder_z(0, 0, 0.0, 0.14, 0.4, sm.dark_steel(), segments=10)
    b.cylinder_z(0, 0, 0.35, 0.12, 0.06, mat("paper", (0.95, 0.95, 0.9, 1.0), roughness=0.9), segments=8)
    return b.finish(origin)


def decor_fern(origin):
    """Tall potted fern, 1.5 m, centred."""
    pot, leaf = mat("clay", (0.65, 0.35, 0.2, 1.0), roughness=0.9), mat("leaf", (0.15, 0.6, 0.2, 1.0), roughness=0.8)
    b = Builder("decor_fern")
    b.cylinder_z(0, 0, 0.0, 0.25, 0.4, pot, segments=10)
    for k in range(8):
        import math

        a = k * math.pi / 4
        for t, h in ((0.15, 0.7), (0.3, 0.55)):
            x, y = math.cos(a) * t, math.sin(a) * t
            b.box(x - 0.02, y - 0.02, 0.4, x + 0.02, y + 0.02, 0.4 + h + t, leaf)
            b.box(x - 0.1, y - 0.03, 0.4 + h, x + 0.1, y + 0.03, 0.46 + h, leaf)
    return b.finish(origin)


def decor_painting(origin):
    """Framed landscape painting 1.2 x 0.8 m; back on Y = 0."""
    frame = mat("jukebox_wood", (0.45, 0.25, 0.15, 1.0), roughness=0.6)
    sky, land, sun = mat("canvas_sky", CANVAS[0], roughness=0.9), mat("canvas_land", CANVAS[1], roughness=0.9), mat("canvas_sun", CANVAS[2], roughness=0.9)
    b = Builder("decor_painting")
    b.box(-0.6, -0.04, 1.3, 0.6, 0.0, 2.1, frame)
    b.box(-0.55, -0.045, 1.35, 0.55, -0.04, 2.05, sky)
    b.box(-0.55, -0.047, 1.35, 0.55, -0.04, 1.6, land)
    b.box(0.2, -0.05, 1.75, 0.35, -0.047, 1.9, sun)
    return b.finish(origin)


def decor_trophy_case(origin):
    """Glass trophy case 1 m wide with three gold cups on shelves; back on Y = 0."""
    gold = mat("brass", (0.9, 0.75, 0.25, 1.0), metallic=0.8, roughness=0.4)
    b = Builder("decor_trophy_case")
    b.box(-0.5, -0.35, 0.0, 0.5, 0.0, 1.9, sm.dark_steel())
    b.box(-0.46, -0.36, 0.08, 0.46, -0.35, 1.86, sm.glass())
    for z in (0.5, 1.0, 1.5):
        b.box(-0.46, -0.33, z, 0.46, -0.02, z + 0.02, sm.glass())
        for x in (-0.25, 0.0, 0.25):
            b.cylinder_z(x, -0.18, z + 0.02, 0.05, 0.15, gold, segments=8)
    return b.finish(origin)


# ----- Runs and floor markings -----


def decor_cable_tray(origin):
    """Two-metre cable tray along a wall at 2.6 m with bundled cables; back on Y = 0."""
    b = Builder("decor_cable_tray")
    b.box(-1.0, -0.2, 2.55, 1.0, 0.0, 2.6, sm.steel())
    b.box(-1.0, -0.2, 2.6, 1.0, -0.17, 2.7, sm.steel())
    for i, y in enumerate((-0.05, -0.1, -0.15)):
        b.box(-1.0, y - 0.02, 2.6, 1.0, y + 0.02, 2.68, mat(f"cable_{i}", ((0.1, 0.1, 0.12), (0.5, 0.1, 0.1), (0.1, 0.2, 0.5))[i] + (1.0,), roughness=0.8))
    return b.finish(origin)


def decor_pipe_run(origin):
    """Two-metre horizontal pipe along a wall at 2.7 m with flanges and a valve; back on Y = 0."""
    pipe = sm.steel()
    b = Builder("decor_pipe_run")
    b.box(-1.0, -0.18, 2.62, 1.0, -0.06, 2.74, pipe)
    for x in (-0.95, 0.0, 0.95):
        b.box(x - 0.03, -0.2, 2.58, x + 0.03, -0.04, 2.78, sm.gunmetal())
    b.cylinder_z(0.45, -0.12, 2.74, 0.02, 0.12, sm.hazard(), segments=8)
    return b.finish(origin)


def decor_floor_strip(origin):
    """Two-metre yellow-and-black hazard strip laid on the floor, centred."""
    b = Builder("decor_floor_strip")
    for i in range(10):
        b.box(-1.0 + i * 0.2, -0.1, 0.0, -0.8 + i * 0.2, 0.1, 0.006, mat("hazard_yellow", HAZARD_Y, roughness=0.5) if i % 2 == 0 else sm.rubber())
    return b.finish(origin)


def decor_floor_arrow(origin):
    """Direction arrow painted on the floor, 1.2 m long, pointing towards -Y, centred."""
    paint = mat("floor_paint", (0.95, 0.9, 0.3, 1.0), roughness=0.6)
    b = Builder("decor_floor_arrow")
    b.box(-0.05, -0.2, 0.0, 0.05, 0.6, 0.006, paint)
    b.box(-0.25, -0.3, 0.0, 0.25, -0.2, 0.006, paint)
    b.box(-0.15, -0.4, 0.0, 0.15, -0.3, 0.006, paint)
    b.box(-0.06, -0.5, 0.0, 0.06, -0.4, 0.006, paint)
    return b.finish(origin)


# ----- Cameras -----


def decor_cctv_dome(origin):
    """Corner dome camera on a bracket, tilted down; back on Y = 0, mount about 2.7 m up."""
    b = Builder("decor_cctv_dome")
    b.box(-0.04, -0.14, 2.62, 0.04, 0.0, 2.68, sm.steel())
    b.cylinder_z(0.0, -0.2, 2.45, 0.12, 0.2, sm.dark_steel(), segments=12)
    b.cylinder(0.0, -0.32, 2.5, 0.05, 0.03, sm.screen_glass(), segments=8)
    b.box(0.07, -0.18, 2.58, 0.1, -0.14, 2.62, sm.led(sm.LED_RED, "led_red"))
    return b.finish(origin)


ITEMS3 = (
    decor_emergency_cabinet,
    decor_o2_tank,
    decor_fire_hose,
    decor_fire_alarm,
    decor_first_aid,
    decor_o2_outlet,
    decor_vending_snack,
    decor_vending_cigs,
    decor_ashtray_stand,
    decor_door_reader,
    decor_intercom,
    decor_wall_phone,
    decor_exit_sign,
    decor_notice_board,
    decor_water_cooler,
    decor_coat_rack,
    decor_soap_dispenser,
    decor_towel_dispenser,
    decor_wastebasket,
    decor_fern,
    decor_painting,
    decor_trophy_case,
    decor_cable_tray,
    decor_pipe_run,
    decor_floor_strip,
    decor_floor_arrow,
    decor_cctv_dome,
)


def build_items3():
    objects = []
    for i, make in enumerate(ITEMS3):
        row, col = divmod(i, ITEM3_PER_ROW)
        objects.append(make((col * ITEM3_STEP, ITEM3_ROW_Y + row * ITEM3_ROW_GAP, 0.0)))
    return objects
