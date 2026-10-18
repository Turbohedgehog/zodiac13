"""A second set of purpose-specific fittings: bar and lounge entertainment, library and chapel furnishings,
medical and laboratory equipment, engineering controls and warning signs, cargo vehicles, wall decor.

Same conventions as station_items: metres, wall items have their back on Y = 0 and look towards -Y, standing
items are centred on their footprint on Z = 0.
"""

import station_models as sm

Builder = sm.Builder
mat = sm.material

ITEM2_ROW_Y = 60.0
ITEM2_ROW_GAP = 8.0
ITEM2_PER_ROW = 14
ITEM2_STEP = 6.0

NEON = {"pink": (1.0, 0.2, 0.6, 1.0), "cyan": (0.2, 0.9, 1.0, 1.0), "amber": (1.0, 0.65, 0.1, 1.0)}
POSTER = {
    "blue": ((0.15, 0.3, 0.7, 1.0), (0.9, 0.9, 0.3, 1.0)),
    "red": ((0.75, 0.15, 0.15, 1.0), (0.95, 0.95, 0.9, 1.0)),
    "green": ((0.15, 0.55, 0.3, 1.0), (0.95, 0.7, 0.2, 1.0)),
}
FELT = (0.1, 0.45, 0.25, 1.0)
LEATHER = (0.25, 0.15, 0.1, 1.0)
GOLD = (0.9, 0.75, 0.25, 1.0)
BIOHAZARD = (0.95, 0.8, 0.1, 1.0)
GLASS_COLORS = ((0.85, 0.15, 0.2, 1.0), (0.2, 0.4, 0.9, 1.0), (0.95, 0.8, 0.2, 1.0), (0.2, 0.7, 0.4, 1.0))


# ----- Kitchen, dining, bar -----


def decor_microwave(origin):
    """Counter-top microwave 0.5 x 0.4 x 0.3 m with a window and a dial; centred, sits on a counter."""
    body, window, dial = sm.beige(), sm.screen_glass(), sm.beige_dark()
    b = Builder("decor_microwave")
    b.box(-0.25, -0.2, 0.0, 0.25, 0.2, 0.3, body)
    b.box(-0.2, -0.21, 0.05, 0.08, -0.2, 0.25, window)
    b.cylinder(0.16, -0.2, 0.15, 0.04, 0.02, dial, segments=8)
    return b.finish(origin)


def decor_pot_rack(origin):
    """Wall rail with hanging pots and pans, 1.6 m wide; back on Y = 0."""
    rail, pot = sm.steel(), sm.gunmetal()
    b = Builder("decor_pot_rack")
    b.box(-0.8, -0.06, 1.9, 0.8, 0.0, 1.95, rail)
    for i in range(5):
        x = -0.65 + i * 0.32
        b.box(x - 0.01, -0.06, 1.7, x + 0.01, -0.04, 1.9, rail)
        b.cylinder_z(x, -0.12, 1.45, 0.11, 0.1, pot, segments=10)
    return b.finish(origin)


def decor_menu_board(origin):
    """Wall menu board 1.6 x 0.8 m with amber text lines; back on Y = 0."""
    board, text = sm.dark_steel(), mat("amber_text", sm.PHOSPHOR_AMBER, emission=2.0)
    b = Builder("decor_menu_board")
    b.box(-0.8, -0.04, 1.3, 0.8, 0.0, 2.1, board)
    for i in range(5):
        b.box(-0.65, -0.045, 1.4 + i * 0.13, 0.3 + (i % 3) * 0.15, -0.04, 1.46 + i * 0.13, text)
    return b.finish(origin)


def decor_jukebox(origin):
    """80s jukebox: an arched cabinet with glowing coloured strips, 1 x 0.6 x 1.6 m; back on Y = 0."""
    body = mat("jukebox_wood", (0.45, 0.25, 0.15, 1.0), roughness=0.6)
    strips = [mat(f"jukebox_{n}", c, emission=4.0) for n, c in NEON.items()]
    b = Builder("decor_jukebox")
    b.box(-0.5, -0.6, 0.0, 0.5, 0.0, 1.4, body)
    b.box(-0.35, -0.6, 1.4, 0.35, 0.0, 1.6, body)
    for i, strip in enumerate(strips * 2):
        b.box(-0.4 + i * 0.13, -0.61, 0.3, -0.34 + i * 0.13, -0.6, 1.35, strip)
    b.box(-0.3, -0.61, 0.1, 0.3, -0.6, 0.25, sm.screen_glass())
    return b.finish(origin)


def neon_sign(colour):
    def make(origin):
        """Neon sign 1.2 x 0.5 m: a glowing outline with a zigzag, back on Y = 0."""
        tube = mat(f"neon_{colour}", NEON[colour], emission=8.0)
        b = Builder(f"decor_neon_{colour}")
        b.box(-0.6, -0.03, 1.6, 0.6, 0.0, 2.1, sm.dark_steel())
        for x0, z0, x1, z1 in ((-0.55, 1.65, 0.55, 1.68), (-0.55, 2.02, 0.55, 2.05), (-0.55, 1.65, -0.52, 2.05), (0.52, 1.65, 0.55, 2.05)):
            b.box(x0, -0.05, z0, x1, -0.03, z1, tube)
        for i in range(4):
            b.box(-0.4 + i * 0.22, -0.05, 1.72 + (i % 2) * 0.2, -0.3 + i * 0.22, -0.03, 1.8 + (i % 2) * 0.2, tube)
        return b.finish(origin)

    make.__name__ = f"decor_neon_{colour}"
    return make


def decor_pool_table(origin):
    """Pool table 2.4 x 1.3 m with green felt, wooden rails, pockets and a rack of balls; centred."""
    wood, felt = mat("jukebox_wood", (0.45, 0.25, 0.15, 1.0), roughness=0.6), mat("felt", FELT, roughness=1.0)
    b = Builder("decor_pool_table")
    for x in (-1.0, 1.0):
        for y in (-0.5, 0.5):
            b.box(x - 0.1, y - 0.1, 0.0, x + 0.1, y + 0.1, 0.75, wood)
    b.box(-1.2, -0.65, 0.75, 1.2, 0.65, 0.85, wood)
    b.box(-1.05, -0.5, 0.85, 1.05, 0.5, 0.88, felt)
    for x, y in ((-1.1, -0.55), (0.0, -0.58), (1.1, -0.55), (-1.1, 0.55), (0.0, 0.58), (1.1, 0.55)):
        b.cylinder_z(x, y, 0.85, 0.06, 0.03, sm.dark_steel(), segments=8)
    for i in range(6):
        b.box(0.4 + (i % 3) * 0.1, -0.1 + (i // 3) * 0.1, 0.88, 0.46 + (i % 3) * 0.1, -0.04 + (i // 3) * 0.1, 0.94, sm.hazard() if i % 2 else sm.beige())
    return b.finish(origin)


def decor_dartboard(origin):
    """Wall dartboard: a black and red disc on a cork backing; back on Y = 0."""
    b = Builder("decor_dartboard")
    b.box(-0.3, -0.02, 1.3, 0.3, 0.0, 1.9, mat("cork", (0.6, 0.45, 0.25, 1.0), roughness=1.0))
    b.cylinder(0.0, -0.02, 1.6, 0.22, 0.02, sm.rubber(), segments=18)
    b.cylinder(0.0, -0.04, 1.6, 0.11, 0.02, mat("dart_red", (0.85, 0.1, 0.1, 1.0)), segments=14)
    b.cylinder(0.0, -0.06, 1.6, 0.03, 0.02, mat("dart_green", (0.2, 0.7, 0.25, 1.0)), segments=8)
    return b.finish(origin)


# ----- Library, chapel, theatre -----


def decor_card_catalog(origin):
    """Library card catalogue: a wooden cabinet of 4 x 6 small drawers, 1.2 m wide; back on Y = 0."""
    wood, brass = mat("jukebox_wood", (0.45, 0.25, 0.15, 1.0), roughness=0.6), mat("brass", GOLD, metallic=0.8, roughness=0.4)
    b = Builder("decor_card_catalog")
    b.box(-0.6, -0.45, 0.0, 0.6, 0.0, 1.2, wood)
    for col in range(4):
        for row in range(6):
            x, z = -0.55 + col * 0.28, 0.06 + row * 0.19
            b.box(x, -0.46, z, x + 0.25, -0.45, z + 0.16, wood)
            b.box(x + 0.09, -0.47, z + 0.06, x + 0.16, -0.46, z + 0.1, brass)
    return b.finish(origin)


def decor_globe(origin):
    """A globe on a wooden stand, 0.5 m across; centred."""
    stand, ball = mat("jukebox_wood", (0.45, 0.25, 0.15, 1.0), roughness=0.6), mat("globe_blue", (0.2, 0.45, 0.8, 1.0), roughness=0.4)
    b = Builder("decor_globe")
    b.cylinder_z(0, 0, 0.0, 0.2, 0.05, stand, segments=10)
    b.cylinder_z(0, 0, 0.05, 0.03, 0.8, stand, segments=6)
    b.cylinder_z(0, 0, 0.85, 0.22, 0.4, ball, segments=14)
    return b.finish(origin)


def decor_lectern(origin):
    """Standing lectern with a slanted top and a book; its face looks towards -Y, centred."""
    wood = mat("jukebox_wood", (0.45, 0.25, 0.15, 1.0), roughness=0.6)
    b = Builder("decor_lectern")
    b.box(-0.3, -0.25, 0.0, 0.3, 0.25, 1.0, wood)
    b.slanted_slab(-0.35, 0.35, -0.3, -0.05, 0.05, 1.0, 1.2, wood)
    b.box(-0.2, -0.2, 1.1, 0.2, 0.0, 1.14, sm.fabric_red())
    return b.finish(origin)


def decor_candle_stand(origin):
    """Tall iron stand with three candles, centred."""
    iron, flame = sm.dark_steel(), mat("candle_glow", (1.0, 0.85, 0.4, 1.0), emission=8.0)
    b = Builder("decor_candle_stand")
    b.cylinder_z(0, 0, 0.0, 0.15, 0.04, iron, segments=8)
    b.cylinder_z(0, 0, 0.04, 0.02, 1.2, iron, segments=6)
    for x in (-0.12, 0.0, 0.12):
        b.box(x - 0.02, -0.02, 1.2, x + 0.02, 0.02, 1.35, mat("wax", sm.SHEET_WHITE, roughness=0.9))
        b.box(x - 0.01, -0.01, 1.35, x + 0.01, 0.01, 1.4, flame)
    b.box(-0.16, -0.03, 1.17, 0.16, 0.03, 1.2, iron)
    return b.finish(origin)


def decor_stained_glass(origin):
    """Chapel window panel 1.2 x 1.6 m of coloured glass tiles in a dark frame, lit; back on Y = 0."""
    frame = sm.dark_steel()
    tiles = [mat(f"stain_{i}", c, emission=2.5) for i, c in enumerate(GLASS_COLORS)]
    b = Builder("decor_stained_glass")
    b.box(-0.6, -0.04, 0.8, 0.6, 0.0, 2.4, frame)
    for col in range(4):
        for row in range(6):
            x, z = -0.55 + col * 0.275, 0.85 + row * 0.25
            b.box(x, -0.045, z, x + 0.25, -0.04, z + 0.22, tiles[(col * 3 + row) % len(tiles)])
    return b.finish(origin)


def decor_synth(origin):
    """80s synthesiser on a stand: a black body with keys and knobs, 1.2 m wide, centred, looks towards -Y."""
    body, white, black = sm.dark_steel(), sm.beige(), sm.rubber()
    b = Builder("decor_synth")
    for x in (-0.55, 0.55):
        b.box(x - 0.03, -0.1, 0.0, x + 0.03, 0.1, 0.85, sm.steel())
    b.box(-0.65, -0.2, 0.85, 0.65, 0.25, 0.95, body)
    for i in range(14):
        b.box(-0.6 + i * 0.086, -0.2, 0.95, -0.55 + i * 0.086, -0.02, 0.97, white if i % 2 else black)
    for i in range(8):
        b.cylinder_z(-0.5 + i * 0.14, 0.12, 0.95, 0.025, 0.03, sm.hazard(), segments=6)
    return b.finish(origin)


# ----- Gym and lounge -----


def decor_dumbbell_rack(origin):
    """Two-tier rack with six dumbbells, 1.6 m wide; back on Y = 0."""
    frame, bell = sm.gunmetal(), sm.rubber()
    b = Builder("decor_dumbbell_rack")
    for z in (0.5, 0.9):
        b.box(-0.8, -0.35, z, 0.8, 0.0, z + 0.04, frame)
        for i in range(6):
            x = -0.7 + i * 0.27
            b.box(x - 0.08, -0.25, z + 0.04, x + 0.08, -0.15, z + 0.2, bell)
            b.box(x - 0.01, -0.22, z + 0.04, x + 0.01, -0.18, z + 0.2, frame)
    for x in (-0.78, 0.74):
        b.box(x, -0.35, 0.0, x + 0.04, 0.0, 1.0, frame)
    return b.finish(origin)


def decor_gym_mat(origin):
    """Blue exercise mat 2 x 1 m, centred."""
    b = Builder("decor_gym_mat")
    b.box(-1.0, -0.5, 0.0, 1.0, 0.5, 0.03, mat("mat_blue", (0.15, 0.3, 0.65, 1.0), roughness=0.9))
    return b.finish(origin)


def decor_tv(origin):
    """CRT television on a low cabinet; its screen looks towards -Y, 1 m wide, centred."""
    cabinet, body = mat("jukebox_wood", (0.45, 0.25, 0.15, 1.0), roughness=0.6), sm.beige()
    b = Builder("decor_tv")
    b.box(-0.5, -0.25, 0.0, 0.5, 0.25, 0.5, cabinet)
    b.box(-0.4, -0.25, 0.5, 0.4, 0.15, 1.0, body)
    b.box(-0.33, -0.27, 0.56, 0.33, -0.25, 0.94, sm.phosphor(sm.PHOSPHOR_GREEN, "crt_green"))
    b.frustum(0.0, 0.75, 0.15, (0.7, 0.45), 0.4, (0.4, 0.25), body)
    return b.finish(origin)


def decor_coffee_table(origin):
    """Low coffee table 1.2 x 0.6 m, centred."""
    top, legs = mat("jukebox_wood", (0.45, 0.25, 0.15, 1.0), roughness=0.6), sm.steel()
    b = Builder("decor_coffee_table")
    for x in (-0.55, 0.55):
        for y in (-0.25, 0.25):
            b.box(x - 0.03, y - 0.03, 0.0, x + 0.03, y + 0.03, 0.4, legs)
    b.box(-0.6, -0.3, 0.4, 0.6, 0.3, 0.45, top)
    return b.finish(origin)


def decor_floor_lamp(origin):
    """80s floor lamp: a round base, a pole and a glowing shade, 1.7 m, centred."""
    glow = mat("lamp_glow", sm.LAMP_WARM, emission=7.0)
    b = Builder("decor_floor_lamp")
    b.cylinder_z(0, 0, 0.0, 0.16, 0.04, sm.dark_steel(), segments=10)
    b.cylinder_z(0, 0, 0.04, 0.015, 1.4, sm.steel(), segments=6)
    b.cylinder_z(0, 0, 1.4, 0.2, 0.3, glow, segments=12)
    return b.finish(origin)


def decor_rug(origin):
    """Rug 2.4 x 1.6 m, red with a cream border, centred."""
    b = Builder("decor_rug")
    b.box(-1.2, -0.8, 0.0, 1.2, 0.8, 0.015, mat("rug_cream", (0.9, 0.85, 0.7, 1.0), roughness=1.0))
    b.box(-1.1, -0.7, 0.015, 1.1, 0.7, 0.025, mat("rug_red", (0.6, 0.15, 0.15, 1.0), roughness=1.0))
    return b.finish(origin)


def decor_plant_pot(origin):
    """Potted leafy plant, 1.1 m tall, centred."""
    pot, leaf = mat("clay", (0.65, 0.35, 0.2, 1.0), roughness=0.9), mat("leaf", (0.15, 0.6, 0.2, 1.0), roughness=0.8)
    b = Builder("decor_plant_pot")
    b.cylinder_z(0, 0, 0.0, 0.2, 0.3, pot, segments=10)
    for i in range(6):
        dx, dy = ((-0.1, 0.0), (0.1, 0.0), (0.0, 0.1), (0.0, -0.1), (0.05, 0.05), (-0.05, -0.05))[i]
        b.box(dx - 0.015, dy - 0.015, 0.3, dx + 0.015, dy + 0.015, 0.7 + i * 0.07, leaf)
        b.box(dx - 0.12, dy - 0.04, 0.6 + i * 0.07, dx + 0.12, dy + 0.04, 0.64 + i * 0.07, leaf)
    return b.finish(origin)


def poster(variant):
    background, accent = POSTER[variant]

    def make(origin):
        """Wall poster 0.6 x 0.9 m; back on Y = 0."""
        b = Builder(f"decor_poster_{variant}")
        b.box(-0.3, -0.01, 1.2, 0.3, 0.0, 2.1, mat(f"poster_{variant}", background, roughness=0.6))
        b.box(-0.2, -0.015, 1.6, 0.2, -0.01, 1.7, mat(f"poster_accent_{variant}", accent, roughness=0.6))
        b.box(-0.12, -0.015, 1.35, 0.12, -0.01, 1.45, mat(f"poster_accent_{variant}", accent, roughness=0.6))
        b.box(-0.05, -0.015, 1.8, 0.05, -0.01, 2.0, mat(f"poster_accent_{variant}", accent, roughness=0.6))
        return b.finish(origin)

    make.__name__ = f"decor_poster_{variant}"
    return make


def decor_clock(origin):
    """Wall clock with a red digital display; back on Y = 0, 0.5 m wide."""
    body, digits = sm.dark_steel(), mat("clock_red", (1.0, 0.1, 0.05, 1.0), emission=6.0)
    b = Builder("decor_clock")
    b.box(-0.25, -0.06, 2.0, 0.25, 0.0, 2.25, body)
    for x in (-0.2, -0.1, 0.05, 0.15):
        b.box(x, -0.065, 2.05, x + 0.06, -0.06, 2.2, digits)
    b.box(-0.015, -0.065, 2.1, 0.015, -0.06, 2.14, digits)
    b.box(-0.015, -0.065, 2.15, 0.015, -0.06, 2.19, digits)
    return b.finish(origin)


# ----- Medical and laboratory -----


def decor_heart_monitor(origin):
    """Bedside heart monitor: a CRT on a pole with a green trace, centred, screen towards -Y."""
    base, body = sm.steel(), sm.beige()
    b = Builder("decor_heart_monitor")
    b.box(-0.2, -0.2, 0.0, 0.2, 0.2, 0.04, base)
    b.box(-0.015, -0.015, 0.04, 0.015, 0.015, 1.1, base)
    b.box(-0.22, -0.15, 1.1, 0.22, 0.15, 1.45, body)
    b.box(-0.18, -0.16, 1.15, 0.12, -0.15, 1.4, sm.phosphor(sm.PHOSPHOR_GREEN, "crt_green"))
    for i in range(6):
        b.box(-0.16 + i * 0.045, -0.165, 1.25 + (i % 3 - 1) * 0.04, -0.13 + i * 0.045, -0.16, 1.28 + (i % 3 - 1) * 0.04, sm.beige())
    return b.finish(origin)


def decor_wheelchair(origin):
    """Wheelchair: a seat, a back, two big wheels and push handles; looks towards -Y, centred."""
    frame, seat, wheel = sm.steel(), sm.rubber(), sm.rubber()
    b = Builder("decor_wheelchair")
    b.box(-0.22, -0.25, 0.45, 0.22, 0.2, 0.5, seat)
    b.box(-0.22, 0.17, 0.5, 0.22, 0.22, 0.95, seat)
    for x in (-0.3, 0.3):
        b.cylinder(x, 0.1, 0.3, 0.3, 0.05, wheel, segments=14)
        b.box(x - 0.015, -0.25, 0.45, x + 0.015, 0.2, 0.5, frame)
    b.box(-0.25, 0.2, 0.9, 0.25, 0.3, 0.95, frame)
    return b.finish(origin)


def decor_defib_cart(origin):
    """Defibrillator cart: a red case with paddles on a wheeled steel cart, centred."""
    cart, case = sm.steel(), mat("defib_red", (0.85, 0.12, 0.12, 1.0), roughness=0.5)
    b = Builder("decor_defib_cart")
    b.box(-0.3, -0.2, 0.15, 0.3, 0.2, 0.2, cart)
    b.box(-0.3, -0.2, 0.2, -0.27, 0.2, 0.9, cart)
    b.box(0.27, -0.2, 0.2, 0.3, 0.2, 0.9, cart)
    b.box(-0.3, -0.2, 0.88, 0.3, 0.2, 0.92, cart)
    b.box(-0.2, -0.15, 0.92, 0.2, 0.15, 1.12, case)
    for x in (-0.25, 0.25):
        for y in (-0.15, 0.15):
            b.cylinder_z(x, y, 0.0, 0.06, 0.15, sm.rubber(), segments=8)
    return b.finish(origin)


def decor_xray(origin):
    """Mobile X-ray unit: a steel base, a tall mast and a horizontal tube head, 1.6 m tall, centred."""
    base, head = sm.gunmetal(), sm.beige()
    b = Builder("decor_xray")
    b.box(-0.4, -0.4, 0.0, 0.4, 0.4, 0.3, base)
    b.box(-0.06, -0.06, 0.3, 0.06, 0.06, 1.6, sm.steel())
    b.box(-0.06, -0.8, 1.45, 0.06, 0.06, 1.55, sm.steel())
    b.box(-0.18, -0.9, 1.3, 0.18, -0.55, 1.6, head)
    return b.finish(origin)


def decor_autopsy_table(origin):
    """Steel autopsy table 2 x 0.8 m with a raised rim and a drain; centred."""
    steel, rim = sm.steel(), sm.gunmetal()
    b = Builder("decor_autopsy_table")
    for x in (-0.9, 0.9):
        b.box(x - 0.04, -0.3, 0.0, x + 0.04, 0.3, 0.85, rim)
    b.box(-1.0, -0.4, 0.85, 1.0, 0.4, 0.9, steel)
    b.box(-1.0, -0.4, 0.9, 1.0, -0.37, 0.95, rim)
    b.box(-1.0, 0.37, 0.9, 1.0, 0.4, 0.95, rim)
    b.cylinder_z(0.0, 0.0, 0.9, 0.05, 0.005, sm.dark_steel(), segments=8)
    return b.finish(origin)


def decor_fume_hood(origin):
    """Laboratory fume hood: a cabinet with a raised glass sash and an exhaust duct; 1.4 m wide, back on Y = 0."""
    body, duct = sm.steel(), sm.gunmetal()
    b = Builder("decor_fume_hood")
    b.box(-0.7, -0.6, 0.0, 0.7, 0.0, 0.9, body)
    b.box(-0.7, -0.6, 0.9, 0.7, -0.55, 2.0, sm.glass())
    b.box(-0.7, -0.6, 2.0, 0.7, 0.0, 2.1, body)
    b.box(-0.15, -0.3, 2.1, 0.15, 0.0, 3.0, duct)
    return b.finish(origin)


def decor_centrifuge(origin):
    """Bench centrifuge: a round drum with a lid on a box, centred, sits on a counter."""
    b = Builder("decor_centrifuge")
    b.box(-0.2, -0.2, 0.0, 0.2, 0.2, 0.2, sm.beige())
    b.cylinder_z(0, 0, 0.2, 0.17, 0.1, sm.gunmetal(), segments=14)
    b.cylinder_z(0, 0, 0.3, 0.14, 0.03, sm.glass(), segments=14)
    return b.finish(origin)


def decor_microscope(origin):
    """Bench microscope: a base, a pillar and a tilted tube, centred, sits on a counter."""
    b = Builder("decor_microscope")
    b.box(-0.12, -0.15, 0.0, 0.12, 0.15, 0.03, sm.dark_steel())
    b.box(-0.03, 0.08, 0.03, 0.03, 0.13, 0.4, sm.dark_steel())
    b.slanted_slab(-0.04, 0.04, -0.1, 0.1, 0.06, 0.15, 0.4, sm.gunmetal())
    return b.finish(origin)


def decor_biohazard_sign(origin):
    """Yellow biohazard warning plate with black rings, 0.6 x 0.6 m; back on Y = 0."""
    b = Builder("decor_biohazard_sign")
    b.box(-0.3, -0.015, 1.5, 0.3, 0.0, 2.1, mat("hazard_yellow", BIOHAZARD, roughness=0.5))
    for x, z in ((0.0, 1.9), (-0.12, 1.7), (0.12, 1.7)):
        b.cylinder(x, -0.015, z, 0.08, 0.015, sm.rubber(), segments=10)
    b.cylinder(0.0, -0.03, 1.77, 0.03, 0.015, mat("hazard_yellow", BIOHAZARD, roughness=0.5), segments=8)
    return b.finish(origin)


def decor_warning_sign(origin):
    """Yellow high-voltage warning plate with a black lightning bolt, 0.5 x 0.5 m; back on Y = 0."""
    b = Builder("decor_warning_sign")
    b.box(-0.25, -0.015, 1.5, 0.25, 0.0, 2.0, mat("hazard_yellow", BIOHAZARD, roughness=0.5))
    b.slanted_slab(-0.03, 0.03, -0.015, -0.015, 0.005, 1.55, 1.95, sm.rubber())
    b.box(-0.12, -0.03, 1.75, 0.12, -0.015, 1.8, sm.rubber())
    return b.finish(origin)


# ----- Engineering and cargo -----


def decor_turbine(origin):
    """Steam turbine 3 m long lying along Y: a steel drum with fan blades at both ends and a pipe run; centred."""
    drum, blade = sm.steel(), sm.gunmetal()
    b = Builder("decor_turbine")
    b.box(-0.9, -1.8, 0.0, 0.9, 1.8, 0.2, sm.gunmetal())
    b.cylinder(0.0, 1.5, 0.95, 0.65, 3.0, drum, segments=18)
    for i in range(8):
        a = i * 0.785
        import math

        b.box(-0.04 + 0.5 * math.cos(a) * 0.5, 1.5, 0.95 + 0.5 * math.sin(a) * 0.5 - 0.04, 0.04 + 0.5 * math.cos(a) * 0.5, 1.55, 0.95 + 0.5 * math.sin(a) * 0.5 + 0.04, blade)
    b.box(-0.1, -0.6, 1.6, 0.1, 0.6, 1.9, sm.hazard())
    return b.finish(origin)


def decor_control_wall(origin):
    """Engineering control wall 3 m wide: slanted console with screens, switch rows and warning lights; back on Y = 0."""
    body, panel = sm.gunmetal(), sm.dark_steel()
    leds = (sm.led(sm.LED_GREEN, "led_green"), sm.led(sm.LED_AMBER, "led_amber"), sm.led(sm.LED_RED, "led_red"))
    b = Builder("decor_control_wall")
    b.box(-1.5, -0.5, 0.0, 1.5, 0.0, 0.8, body)
    b.slanted_slab(-1.5, 1.5, -0.5, -0.1, 0.08, 0.8, 1.5, panel)
    for i in range(3):
        b.box(-1.3 + i * 0.9, -0.6, 1.0, -0.6 + i * 0.9, -0.5, 1.4, sm.phosphor(sm.PHOSPHOR_GREEN, "crt_green"))
    for i in range(18):
        b.box(-1.4 + i * 0.16, -0.56, 0.85, -1.35 + i * 0.16, -0.5, 0.9, leds[i % 3])
    return b.finish(origin)


def decor_valve_wheel(origin):
    """Big red valve hand wheel on a pipe stub; back on Y = 0."""
    pipe, wheel = sm.steel(), mat("valve_red", (0.8, 0.12, 0.1, 1.0), roughness=0.5)
    b = Builder("decor_valve_wheel")
    b.box(-0.08, -0.35, 1.2, 0.08, 0.0, 1.36, pipe)
    b.cylinder(0.0, -0.35, 1.28, 0.28, 0.05, wheel, segments=16)
    b.cylinder(0.0, -0.4, 1.28, 0.05, 0.05, pipe, segments=8)
    return b.finish(origin)


def decor_forklift(origin):
    """Forklift 1.2 x 2.4 m: a body, an overhead guard, a mast with forks and four wheels; forks look towards -Y; centred."""
    body, guard, fork, wheel = sm.hazard(), sm.dark_steel(), sm.steel(), sm.rubber()
    b = Builder("decor_forklift")
    b.box(-0.55, -0.2, 0.25, 0.55, 1.1, 0.8, body)
    for x in (-0.5, 0.5):
        b.box(x - 0.03, -0.1, 0.8, x + 0.03, -0.05, 2.0, guard)
        b.box(x - 0.03, 0.9, 0.8, x + 0.03, 0.95, 2.0, guard)
    b.box(-0.55, -0.1, 2.0, 0.55, 0.95, 2.05, guard)
    b.box(-0.45, -0.3, 0.25, 0.45, -0.2, 1.7, guard)
    for x in (-0.3, 0.3):
        b.box(x - 0.05, -1.2, 0.1, x + 0.05, -0.3, 0.14, fork)
    b.box(-0.2, 0.4, 0.8, 0.2, 0.9, 1.4, sm.rubber())
    for x in (-0.6, 0.6):
        for y in (-0.1, 0.9):
            b.cylinder(x, y + 0.1, 0.2, 0.2, 0.18, wheel, segments=12)
    return b.finish(origin)


def decor_scale(origin):
    """Platform scale 1.2 x 1.2 m with a phosphor display on a post, centred."""
    b = Builder("decor_scale")
    b.box(-0.6, -0.6, 0.0, 0.6, 0.6, 0.08, sm.gunmetal())
    b.box(-0.55, -0.55, 0.08, 0.55, 0.55, 0.1, sm.dark_steel())
    b.box(-0.04, 0.5, 0.1, 0.04, 0.56, 1.1, sm.steel())
    b.box(-0.2, 0.44, 1.05, 0.2, 0.56, 1.25, sm.beige())
    b.box(-0.17, 0.43, 1.1, 0.17, 0.44, 1.2, sm.phosphor(sm.PHOSPHOR_AMBER, "crt_amber"))
    return b.finish(origin)


def decor_workbench(origin):
    """Workbench 2 x 0.8 m with a vice, a tool drawer unit and scattered parts; centred."""
    top, body = sm.steel(), sm.gunmetal()
    b = Builder("decor_workbench")
    b.box(-1.0, -0.4, 0.0, -0.4, 0.4, 0.85, body)
    for x in (0.9,):
        b.box(x - 0.04, -0.35, 0.0, x + 0.04, 0.35, 0.85, sm.steel())
    b.box(-1.0, -0.4, 0.85, 1.0, 0.4, 0.9, top)
    b.box(0.3, -0.25, 0.9, 0.5, -0.05, 1.05, sm.dark_steel())
    for i in range(4):
        b.box(-0.2 + i * 0.1, -0.1, 0.9, -0.14 + i * 0.1, 0.1, 0.94, sm.hazard() if i % 2 else sm.rubber())
    return b.finish(origin)


# ----- Command, vault, hydroponics, security, utility -----


def decor_flag(origin):
    """Wall banner 1 x 1.5 m: blue with a gold star block and a stripe; back on Y = 0."""
    b = Builder("decor_flag")
    b.box(-0.5, -0.02, 0.8, 0.5, 0.0, 2.3, mat("flag_blue", (0.1, 0.2, 0.55, 1.0), roughness=0.8))
    b.box(-0.5, -0.03, 1.1, 0.5, -0.02, 1.25, mat("flag_gold", GOLD, roughness=0.5))
    b.box(-0.12, -0.03, 1.6, 0.12, -0.02, 1.84, mat("flag_gold", GOLD, roughness=0.5))
    return b.finish(origin)


def decor_viewscreen(origin):
    """Wall viewscreen 4 x 1.5 m: a dark bezel around a green grid display; back on Y = 0."""
    bezel, grid = sm.dark_steel(), sm.phosphor(sm.PHOSPHOR_GREEN, "crt_green")
    b = Builder("decor_viewscreen")
    b.box(-2.0, -0.1, 1.0, 2.0, 0.0, 2.7, bezel)
    b.box(-1.9, -0.11, 1.1, 1.9, -0.1, 2.6, sm.screen_glass())
    for i in range(7):
        b.box(-1.8 + i * 0.6, -0.115, 1.15, -1.78 + i * 0.6, -0.11, 2.55, grid)
    for j in range(4):
        b.box(-1.85, -0.115, 1.3 + j * 0.4, 1.85, -0.11, 1.32 + j * 0.4, grid)
    return b.finish(origin)


def decor_vault_door(origin):
    """Round vault door 2.2 m across with a hand wheel and bolts; back on Y = 0."""
    steel, trim = sm.steel(), sm.gunmetal()
    b = Builder("decor_vault_door")
    b.box(-1.2, -0.1, 0.0, 1.2, 0.0, 2.4, trim)
    b.cylinder(0.0, -0.1, 1.2, 1.0, 0.15, steel, segments=24)
    b.cylinder(0.0, -0.25, 1.2, 0.35, 0.05, trim, segments=16)
    for k in range(6):
        import math

        a = k * math.pi / 3
        b.box(0.45 * math.cos(a) - 0.04, -0.3, 1.2 + 0.45 * math.sin(a) - 0.04, 0.45 * math.cos(a) + 0.04, -0.25, 1.2 + 0.45 * math.sin(a) + 0.04, trim)
    return b.finish(origin)


def decor_grow_lamp(origin):
    """Hanging purple grow lamp 1.6 x 0.4 m on two rods, 3 m high; centred."""
    rod, glow = sm.steel(), mat("grow_purple", (0.7, 0.2, 0.9, 1.0), emission=6.0)
    b = Builder("decor_grow_lamp")
    for x in (-0.7, 0.7):
        b.box(x - 0.01, -0.01, 2.2, x + 0.01, 0.01, 3.0, rod)
    b.box(-0.8, -0.2, 2.1, 0.8, 0.2, 2.2, sm.gunmetal())
    b.box(-0.75, -0.15, 2.05, 0.75, 0.15, 2.1, glow)
    return b.finish(origin)


def decor_seed_rack(origin):
    """Seed rack 1.4 m wide: shelves with rows of coloured seed packets; back on Y = 0."""
    frame = sm.steel()
    packets = [mat(f"packet_{i}", c, roughness=0.7) for i, c in enumerate(GLASS_COLORS)]
    b = Builder("decor_seed_rack")
    for z in (0.5, 1.0, 1.5, 2.0):
        b.box(-0.7, -0.25, z, 0.7, 0.0, z + 0.03, frame)
        for i in range(10):
            b.box(-0.65 + i * 0.13, -0.15, z + 0.03, -0.55 + i * 0.13, -0.1, z + 0.28, packets[(i + int(z * 2)) % len(packets)])
    for x in (-0.7, 0.68):
        b.box(x, -0.25, 0.0, x + 0.02, 0.0, 2.1, frame)
    return b.finish(origin)


def decor_cctv(origin):
    """Security camera on a bracket, tilted down; back on Y = 0, mount about 2.6 m up."""
    body = sm.dark_steel()
    b = Builder("decor_cctv")
    b.box(-0.03, -0.12, 2.55, 0.03, 0.0, 2.6, sm.steel())
    b.slanted_slab(-0.07, 0.07, -0.35, -0.2, 0.1, 2.3, 2.5, body)
    b.cylinder(0.0, -0.35, 2.4, 0.04, 0.02, sm.led(sm.LED_RED, "led_red"), segments=8)
    return b.finish(origin)


def decor_wet_floor_sign(origin):
    """Yellow A-frame wet floor sign, 0.6 m tall, centred."""
    yellow = mat("hazard_yellow", BIOHAZARD, roughness=0.5)
    b = Builder("decor_wet_floor_sign")
    b.slanted_slab(-0.15, 0.15, -0.15, 0.0, 0.02, 0.0, 0.6, yellow)
    b.slanted_slab(-0.15, 0.15, 0.15, 0.0, 0.02, 0.0, 0.6, yellow)
    return b.finish(origin)


def decor_mirror(origin):
    """Wall mirror 1 x 0.8 m with a steel frame; back on Y = 0."""
    b = Builder("decor_mirror")
    b.box(-0.5, -0.03, 1.2, 0.5, 0.0, 2.0, sm.steel())
    b.box(-0.46, -0.035, 1.24, 0.46, -0.03, 1.96, mat("mirror", (0.8, 0.9, 0.95, 1.0), metallic=1.0, roughness=0.05))
    return b.finish(origin)


ITEMS2 = (
    decor_microwave,
    decor_pot_rack,
    decor_menu_board,
    decor_jukebox,
    neon_sign("pink"),
    neon_sign("cyan"),
    neon_sign("amber"),
    decor_pool_table,
    decor_dartboard,
    decor_card_catalog,
    decor_globe,
    decor_lectern,
    decor_candle_stand,
    decor_stained_glass,
    decor_synth,
    decor_dumbbell_rack,
    decor_gym_mat,
    decor_tv,
    decor_coffee_table,
    decor_floor_lamp,
    decor_rug,
    decor_plant_pot,
    poster("blue"),
    poster("red"),
    poster("green"),
    decor_clock,
    decor_heart_monitor,
    decor_wheelchair,
    decor_defib_cart,
    decor_xray,
    decor_autopsy_table,
    decor_fume_hood,
    decor_centrifuge,
    decor_microscope,
    decor_biohazard_sign,
    decor_warning_sign,
    decor_turbine,
    decor_control_wall,
    decor_valve_wheel,
    decor_forklift,
    decor_scale,
    decor_workbench,
    decor_flag,
    decor_viewscreen,
    decor_vault_door,
    decor_grow_lamp,
    decor_seed_rack,
    decor_cctv,
    decor_wet_floor_sign,
    decor_mirror,
)


def build_items2():
    objects = []
    for i, make in enumerate(ITEMS2):
        row, col = divmod(i, ITEM2_PER_ROW)
        objects.append(make((col * ITEM2_STEP, ITEM2_ROW_Y + row * ITEM2_ROW_GAP, 0.0)))
    return objects
