"""Department-specific machines and furniture for the station: kitchen, bar, medical, science,
engineering, cargo, security, command and crew-life items. They extend the segment library of
station_models; `build_items()` makes the templates, which the scenes instance.

Conventions are the same as in station_models: metres, wall-mounted items have their back on Y = 0
and look towards -Y, standing items are centred on their footprint and stand on Z = 0.
"""

import math

import station_models as sm

Builder = sm.Builder

ITEM_ROW_Y = 20.0
ITEM_ROW_GAP = 4.0
ITEMS_PER_ROW = 14
ROW_STEP = 6.0

TANK_COLORS = {"yellow": (0.95, 0.75, 0.1, 1.0), "blue": (0.2, 0.45, 0.9, 1.0), "red": (0.85, 0.2, 0.15, 1.0)}
BOTTLE_COLORS = ((0.2, 0.55, 0.25, 1.0), (0.85, 0.6, 0.15, 1.0), (0.25, 0.4, 0.8, 1.0), (0.7, 0.15, 0.2, 1.0))
GREEN_CROSS = (0.1, 0.85, 0.3, 1.0)
WOOD = (0.5, 0.36, 0.22, 1.0)
CURTAIN = (0.6, 0.1, 0.12, 1.0)
CANDLE = (1.0, 0.85, 0.4, 1.0)
ORE = (0.55, 0.32, 0.16, 1.0)
SUIT_WHITE = (0.93, 0.93, 0.95, 1.0)
SUIT_ORANGE = (0.95, 0.45, 0.1, 1.0)
GUN_STEEL = (0.12, 0.13, 0.15, 1.0)


def _mat(name, color, **kw):
    return sm.material(name, color, **kw)


# ----- Kitchen and bar -----


def decor_stove(origin):
    """Range with four burners, an oven door and an extraction hood and flue above; back on Y = 0."""
    body, dark, hood = sm.dark_steel(), sm.rubber(), sm.gunmetal()
    b = Builder("decor_stove")
    b.box(-0.45, -0.7, 0.0, 0.45, 0.0, 0.9, body)
    b.box(-0.4, -0.72, 0.1, 0.4, -0.7, 0.6, dark)
    for x in (-0.2, 0.2):
        for y in (-0.2, -0.5):
            b.cylinder_z(x, y, 0.9, 0.09, 0.03, dark, segments=10)
    b.box(-0.5, -0.55, 1.7, 0.5, 0.0, 2.0, hood)
    b.box(-0.15, -0.25, 2.0, 0.15, 0.0, 3.0, hood)
    return b.finish(origin)


def decor_fridge(origin):
    """Tall double-door refrigerator; back on Y = 0."""
    body, line = sm.beige(), sm.beige_dark()
    b = Builder("decor_fridge")
    b.box(-0.45, -0.7, 0.0, 0.45, 0.0, 2.0, body)
    b.box(-0.01, -0.705, 0.05, 0.01, -0.7, 1.95, line)
    for x in (-0.08, 0.05):
        b.box(x, -0.73, 0.9, x + 0.03, -0.7, 1.5, line)
    return b.finish(origin)


def decor_sink(origin):
    """Counter unit with a basin and a tap; back on Y = 0."""
    body, basin, tap = sm.beige_dark(), sm.dark_steel(), sm.steel()
    b = Builder("decor_sink")
    b.box(-0.5, -0.6, 0.0, 0.5, 0.0, 0.9, body)
    b.box(-0.52, -0.62, 0.9, 0.52, 0.02, 0.94, sm.dark_steel())
    b.box(-0.3, -0.5, 0.93, 0.3, -0.1, 0.95, basin)
    b.box(-0.02, -0.12, 0.95, 0.02, -0.08, 1.15, tap)
    b.box(-0.02, -0.3, 1.13, 0.02, -0.08, 1.17, tap)
    return b.finish(origin)


def decor_prep_table(origin):
    """Steel prep table 1.6 x 0.8 m with a lower shelf, a cutting board and a bowl, centred."""
    steel, wood = sm.steel(), _mat("wood", WOOD, roughness=0.8)
    b = Builder("decor_prep_table")
    for x in (-0.76, 0.76):
        for y in (-0.36, 0.36):
            b.box(x - 0.025, y - 0.025, 0.0, x + 0.025, y + 0.025, 0.9, steel)
    b.box(-0.8, -0.4, 0.9, 0.8, 0.4, 0.94, steel)
    b.box(-0.78, -0.38, 0.25, 0.78, 0.38, 0.28, steel)
    b.box(-0.5, -0.25, 0.94, -0.05, 0.1, 0.97, wood)
    b.cylinder_z(0.4, 0.0, 0.94, 0.15, 0.1, sm.gunmetal(), segments=10)
    return b.finish(origin)


def decor_keg(origin):
    """Beer keg: a steel barrel with two hazard bands, centred."""
    barrel, band = sm.steel(), sm.hazard()
    b = Builder("decor_keg")
    b.cylinder_z(0, 0, 0.0, 0.28, 0.8, barrel, segments=14)
    for z in (0.12, 0.6):
        b.cylinder_z(0, 0, z, 0.29, 0.06, band, segments=14)
    return b.finish(origin)


def decor_bottle_shelf(origin):
    """Two-metre back-bar shelf with rows of coloured bottles, 0.25 deep; back on Y = 0."""
    wood = _mat("wood", WOOD, roughness=0.8)
    glass_bottles = [_mat(f"bottle_{i}", color, roughness=0.15) for i, color in enumerate(sm.BOOK_COLORS + BOTTLE_COLORS)]
    b = Builder("decor_bottle_shelf")
    b.box(-1.0, -0.02, 0.9, 1.0, 0.0, 2.3, wood)
    for z in (1.1, 1.6, 2.1):
        b.box(-1.0, -0.25, z, 1.0, 0.0, z + 0.03, wood)
        x = -0.95
        index = 0
        while x < 0.9:
            b.box(x, -0.18, z + 0.03, x + 0.07, -0.1, z + 0.3, glass_bottles[index % len(glass_bottles)])
            index += 1
            x += 0.13
    return b.finish(origin)


def decor_bar_stool(origin):
    """Bar stool: a round seat on a pole with a footrest, centred."""
    seat, metal = sm.rubber(), sm.steel()
    b = Builder("decor_bar_stool")
    b.cylinder_z(0, 0, 0.0, 0.2, 0.04, metal, segments=10)
    b.cylinder_z(0, 0, 0.04, 0.03, 0.6, metal, segments=6)
    b.cylinder_z(0, 0, 0.62, 0.18, 0.08, seat, segments=12)
    return b.finish(origin)


# ----- Medical -----


def decor_iv_stand(origin):
    """IV drip stand: a base, a pole and a fluid bag, centred."""
    metal, bag = sm.steel(), sm.glass()
    b = Builder("decor_iv_stand")
    b.box(-0.2, -0.2, 0.0, 0.2, 0.2, 0.04, metal)
    b.box(-0.015, -0.015, 0.04, 0.015, 0.015, 1.8, metal)
    b.box(-0.15, -0.015, 1.75, 0.15, 0.015, 1.78, metal)
    b.box(-0.17, -0.05, 1.45, -0.09, 0.05, 1.75, bag)
    return b.finish(origin)


def decor_med_cabinet(origin):
    """Wall medicine cabinet with a green cross; back on Y = 0."""
    body = sm.material("sheet", sm.SHEET_WHITE, roughness=0.5)
    cross = sm.material("green_cross", GREEN_CROSS, emission=3.0)
    b = Builder("decor_med_cabinet")
    b.box(-0.4, -0.3, 0.8, 0.4, 0.0, 1.9, body)
    b.box(-0.04, -0.315, 1.2, 0.04, -0.3, 1.6, cross)
    b.box(-0.2, -0.315, 1.36, 0.2, -0.3, 1.44, cross)
    return b.finish(origin)


def decor_scanner(origin):
    """Body scanner: a bed running into a glass tube on a steel base, 1.2 x 2.4 m, centred."""
    base, tube, ring = sm.steel(), sm.glass(), sm.gunmetal()
    b = Builder("decor_scanner")
    b.box(-0.6, -1.2, 0.0, 0.6, 1.2, 0.4, base)
    b.box(-0.35, -1.1, 0.4, 0.35, 0.6, 0.48, sm.beige())
    for y in (-0.6, 0.6):
        b.cylinder(0, y + 0.1, 0.85, 0.5, 0.2, ring, segments=16)
    b.cylinder(0, 0.4, 0.85, 0.46, 1.2, tube, segments=16)
    return b.finish(origin)


def decor_chem_dispenser(origin):
    """Chemical dispenser: a cabinet with a lit panel and a beaker on top; back on Y = 0."""
    body, panel = sm.steel(), sm.material("dispenser_glow", (0.3, 1.0, 0.7, 1.0), emission=3.0)
    b = Builder("decor_chem_dispenser")
    b.box(-0.5, -0.5, 0.0, 0.5, 0.0, 1.4, body)
    b.box(-0.35, -0.505, 0.9, 0.35, -0.5, 1.3, panel)
    b.box(-0.2, -0.45, 0.4, 0.2, -0.2, 0.7, sm.dark_steel())
    b.cylinder_z(0.0, -0.3, 1.4, 0.08, 0.25, sm.glass(), segments=10)
    return b.finish(origin)


def decor_incubator(origin):
    """Incubator cabinet with a glass door and an indicator light; back on Y = 0."""
    body, led = sm.steel(), sm.led(sm.LED_GREEN, "led_green")
    b = Builder("decor_incubator")
    b.box(-0.4, -0.5, 0.0, 0.4, 0.0, 1.3, body)
    b.box(-0.3, -0.51, 0.2, 0.3, -0.5, 1.1, sm.glass())
    b.box(0.3, -0.52, 1.2, 0.36, -0.5, 1.26, led)
    return b.finish(origin)


def decor_analyzer(origin):
    """Bench analyser: a small box with a phosphor screen and a dial, centred, sits on a counter."""
    body, dial = sm.beige(), sm.dark_steel()
    b = Builder("decor_analyzer")
    b.box(-0.3, -0.25, 0.0, 0.3, 0.25, 0.35, body)
    b.box(-0.22, -0.26, 0.15, 0.1, -0.25, 0.3, sm.phosphor(sm.PHOSPHOR_GREEN, "crt_green"))
    b.cylinder(0.2, -0.25, 0.22, 0.05, 0.03, dial, segments=8)
    return b.finish(origin)


def decor_dna_scanner(origin):
    """Genetics scanner: an upright glass cylinder on a steel plinth, centred."""
    plinth, tube = sm.steel(), sm.glass()
    glow = sm.material("cryo_glow", sm.CRYO_GLOW, emission=6.0)
    b = Builder("decor_dna_scanner")
    b.cylinder_z(0, 0, 0.0, 0.6, 0.3, plinth, segments=16)
    b.cylinder_z(0, 0, 0.3, 0.5, 1.9, tube, segments=16)
    b.cylinder_z(0, 0, 0.3, 0.06, 1.9, glow, segments=8)
    b.cylinder_z(0, 0, 2.2, 0.6, 0.2, plinth, segments=16)
    return b.finish(origin)


def decor_surgery_lamp(origin):
    """Ceiling surgical lamp on a pole, 3 m high, centred over the table."""
    metal, glow = sm.steel(), sm.material("lamp_glow", sm.LAMP_WARM, emission=10.0)
    b = Builder("decor_surgery_lamp")
    b.box(-0.03, -0.03, 2.3, 0.03, 0.03, 3.0, metal)
    b.cylinder_z(0, 0, 2.15, 0.4, 0.06, metal, segments=14)
    b.cylinder_z(0, 0, 2.1, 0.32, 0.05, glow, segments=14)
    return b.finish(origin)


def decor_instrument_tray(origin):
    """Wheeled instrument tray with a few steel instruments, centred."""
    frame, tools = sm.steel(), sm.gunmetal()
    b = Builder("decor_instrument_tray")
    for x in (-0.22, 0.22):
        for y in (-0.15, 0.15):
            b.box(x - 0.02, y - 0.02, 0.0, x + 0.02, y + 0.02, 0.85, frame)
    b.box(-0.27, -0.2, 0.85, 0.27, 0.2, 0.88, frame)
    for i in range(4):
        b.box(-0.2 + i * 0.1, -0.12, 0.88, -0.17 + i * 0.1, 0.12, 0.9, tools)
    return b.finish(origin)


# ----- Science and robotics -----


def decor_robot_arm(origin):
    """Industrial robot arm: a base, a post, a bent arm and a gripper; hazard-orange, centred."""
    base, arm, claw = sm.steel(), sm.hazard(), sm.dark_steel()
    b = Builder("decor_robot_arm")
    b.cylinder_z(0, 0, 0.0, 0.35, 0.25, base, segments=12)
    b.box(-0.1, -0.1, 0.25, 0.1, 0.1, 1.2, arm)
    b.box(-0.08, -0.08, 1.2, 0.08, 0.6, 1.36, arm)
    b.box(-0.06, 0.5, 0.9, 0.06, 0.62, 1.3, arm)
    b.box(-0.12, 0.45, 0.8, -0.06, 0.65, 0.95, claw)
    b.box(0.06, 0.45, 0.8, 0.12, 0.65, 0.95, claw)
    return b.finish(origin)


def decor_mech(origin):
    """Combat exosuit, 3.2 m tall: legs, torso with a cockpit window, arms and a hazard stripe, centred."""
    hull, joint, stripe = sm.gunmetal(), sm.dark_steel(), sm.hazard()
    b = Builder("decor_mech")
    for x in (-0.45, 0.45):
        b.box(x - 0.18, -0.2, 0.0, x + 0.18, 0.35, 0.3, joint)
        b.box(x - 0.14, -0.1, 0.3, x + 0.14, 0.2, 1.4, hull)
    b.box(-0.8, -0.4, 1.4, 0.8, 0.5, 2.4, hull)
    b.box(-0.5, -0.42, 1.8, 0.5, -0.4, 2.3, sm.glass())
    b.box(-0.8, -0.41, 1.4, 0.8, -0.4, 1.5, stripe)
    for x in (-1.1, 0.9):
        b.box(x, -0.2, 1.1, x + 0.2, 0.3, 2.3, joint)
        b.box(x - 0.05, -0.35, 0.8, x + 0.25, 0.45, 1.1, hull)
    return b.finish(origin)


def decor_gas_tank(color_name):
    color = TANK_COLORS[color_name]

    def make(origin):
        """Compressed-gas tank 0.7 m wide, 1.8 m tall with a valve, centred."""
        paint = _mat(f"tank_{color_name}", color, metallic=0.4, roughness=0.5)
        b = Builder(f"decor_gas_tank_{color_name}")
        b.cylinder_z(0, 0, 0.0, 0.35, 1.5, paint, segments=14)
        b.cylinder_z(0, 0, 1.5, 0.22, 0.12, paint, segments=14)
        b.cylinder_z(0, 0, 1.62, 0.05, 0.12, sm.steel(), segments=8)
        b.box(-0.12, -0.03, 1.7, 0.12, 0.03, 1.76, sm.gunmetal())
        return b.finish(origin)

    make.__name__ = f"decor_gas_tank_{color_name}"
    return make


def decor_pen(origin):
    """Specimen pen 2 x 2 m: a steel tray with glass walls and a grate lid, centred."""
    tray, wall, bars = sm.steel(), sm.glass(), sm.dark_steel()
    b = Builder("decor_pen")
    b.box(-1.0, -1.0, 0.0, 1.0, 1.0, 0.15, tray)
    for x in (-1.0, 0.97):
        b.box(x, -1.0, 0.15, x + 0.03, 1.0, 1.3, wall)
    for y in (-1.0, 0.97):
        b.box(-1.0, y, 0.15, 1.0, y + 0.03, 1.3, wall)
    for x in (-0.6, -0.2, 0.2, 0.6):
        b.box(x - 0.015, -1.0, 1.3, x + 0.015, 1.0, 1.34, bars)
    return b.finish(origin)


# ----- Engineering -----


def decor_generator(origin):
    """Diesel-style generator, 2 x 1 x 1.4 m, with cooling fins, an exhaust and a status light, centred."""
    body, fins, exhaust = sm.gunmetal(), sm.steel(), sm.dark_steel()
    b = Builder("decor_generator")
    b.box(-1.0, -0.5, 0.0, 1.0, 0.5, 1.2, body)
    for i in range(8):
        b.box(-0.9 + i * 0.22, -0.55, 0.2, -0.82 + i * 0.22, 0.55, 1.1, fins)
    b.cylinder_z(0.7, 0.0, 1.2, 0.1, 0.5, exhaust, segments=8)
    b.box(-0.9, -0.52, 1.0, -0.7, -0.5, 1.15, sm.led(sm.LED_GREEN, "led_green"))
    return b.finish(origin)


def decor_smes(origin):
    """Power storage cabinet, 1 x 0.8 x 2.2 m, with rows of charge lights and a cable duct; centred."""
    body, duct = sm.gunmetal(), sm.dark_steel()
    leds = (sm.led(sm.LED_GREEN, "led_green"), sm.led(sm.LED_AMBER, "led_amber"), sm.led(sm.LED_RED, "led_red"))
    b = Builder("decor_smes")
    b.box(-0.5, -0.4, 0.0, 0.5, 0.4, 2.2, body)
    b.box(-0.4, -0.41, 1.5, 0.4, -0.4, 2.0, duct)
    for row in range(6):
        for col in range(8):
            mat = leds[(col + row) % 3] if col <= row + 1 else duct
            b.box(-0.37 + col * 0.095, -0.42, 1.55 + row * 0.07, -0.31 + col * 0.095, -0.41, 1.59 + row * 0.07, mat)
    b.box(-0.4, -0.41, 0.2, 0.4, -0.4, 1.3, duct)
    return b.finish(origin)


def decor_pump(origin):
    """Gas pump: a motor box with a hand wheel and two pipe stubs; back on Y = 0."""
    body, wheel, pipe = sm.gunmetal(), sm.hazard(), sm.steel()
    b = Builder("decor_pump")
    b.box(-0.35, -0.5, 0.0, 0.35, 0.0, 0.8, body)
    b.cylinder(0, -0.5, 0.45, 0.2, 0.05, wheel, segments=10)
    for x in (-0.45, 0.35):
        b.box(x, -0.3, 0.3, x + 0.1, -0.1, 0.5, pipe)
    return b.finish(origin)


def decor_antenna(origin):
    """Comms mast with a dish facing -Y, 4 m tall, centred."""
    mast, dish = sm.steel(), sm.beige()
    b = Builder("decor_antenna")
    b.box(-0.4, -0.4, 0.0, 0.4, 0.4, 0.3, sm.gunmetal())
    b.box(-0.05, -0.05, 0.3, 0.05, 0.05, 3.6, mast)
    b.cylinder(0, 0.05, 3.4, 0.8, 0.12, dish, segments=18)
    b.box(-0.02, -0.55, 3.38, 0.02, 0.05, 3.42, mast)
    return b.finish(origin)


def decor_tool_wall(origin):
    """Two-metre tool pegboard with hung wrenches, hammers and saws; back on Y = 0."""
    board, tool, handle = sm.gunmetal(), sm.steel(), sm.hazard()
    b = Builder("decor_tool_wall")
    b.box(-1.0, -0.04, 0.9, 1.0, 0.0, 2.1, board)
    for i in range(9):
        x = -0.85 + i * 0.2
        b.box(x, -0.07, 1.4, x + 0.04, -0.04, 1.9, tool)
        b.box(x - 0.03, -0.07, 1.8, x + 0.07, -0.04, 1.9, tool)
        b.box(x, -0.07, 1.0, x + 0.05, -0.04, 1.3, handle)
    return b.finish(origin)


# ----- Cargo -----


def decor_conveyor(origin):
    """Three-metre belt conveyor on a steel frame, running along X, centred."""
    frame, belt, roller = sm.steel(), sm.rubber(), sm.dark_steel()
    b = Builder("decor_conveyor")
    for x in (-1.4, 1.4):
        for y in (-0.35, 0.35):
            b.box(x - 0.04, y - 0.04, 0.0, x + 0.04, y + 0.04, 0.8, frame)
    b.box(-1.5, -0.4, 0.8, 1.5, 0.4, 0.86, belt)
    for x in (-1.5, 1.5):
        b.box(x - 0.05, -0.45, 0.8, x + 0.05, 0.45, 0.9, roller)
    for y in (-0.42, 0.42):
        b.box(-1.5, y - 0.02, 0.8, 1.5, y + 0.02, 0.95, frame)
    return b.finish(origin)


def decor_bin(origin):
    """Waste bin 1.2 x 0.8 x 1 m with a hazard-striped lid, centred."""
    body, lid = sm.gunmetal(), sm.hazard()
    b = Builder("decor_bin")
    b.box(-0.6, -0.4, 0.0, 0.6, 0.4, 0.95, body)
    b.box(-0.62, -0.42, 0.95, 0.62, 0.42, 1.05, lid)
    return b.finish(origin)


def decor_cart(origin):
    """Hand cart 1.2 x 0.7 m with a handle and four wheels, centred."""
    deck, wheel = sm.steel(), sm.rubber()
    b = Builder("decor_cart")
    b.box(-0.6, -0.35, 0.18, 0.6, 0.35, 0.24, deck)
    b.box(-0.6, 0.3, 0.24, -0.55, 0.35, 0.9, deck)
    b.box(0.55, 0.3, 0.24, 0.6, 0.35, 0.9, deck)
    b.box(-0.6, 0.3, 0.88, 0.6, 0.35, 0.92, deck)
    for x in (-0.5, 0.5):
        for y in (-0.3, 0.3):
            b.cylinder(x, y + 0.03, 0.09, 0.09, 0.06, wheel, segments=10)
    return b.finish(origin)


def decor_pigeonholes(origin):
    """Mail sorting wall: a grid of 6 x 5 cubbies, 2 m wide; back on Y = 0."""
    body, hole = sm.steel(), sm.dark_steel()
    b = Builder("decor_pigeonholes")
    b.box(-1.0, -0.35, 0.3, 1.0, 0.0, 2.0, body)
    for col in range(6):
        for row in range(5):
            x, z = -0.95 + col * 0.325, 0.35 + row * 0.33
            b.box(x, -0.36, z, x + 0.29, -0.35, z + 0.29, hole)
    return b.finish(origin)


def decor_ore_cart(origin):
    """Ore wagon 1.6 x 0.9 m piled with rough ore, centred."""
    import random

    body, ore, wheel = sm.gunmetal(), _mat("ore", ORE, roughness=1.0), sm.rubber()
    rng = random.Random(sm.FLOWER_SEED)
    b = Builder("decor_ore_cart")
    b.box(-0.8, -0.45, 0.2, 0.8, 0.45, 0.25, body)
    for x in (-0.8, 0.77):
        b.box(x, -0.45, 0.25, x + 0.03, 0.45, 0.8, body)
    for y in (-0.45, 0.42):
        b.box(-0.8, y, 0.25, 0.8, y + 0.03, 0.8, body)
    for _ in range(16):
        x, y = rng.uniform(-0.7, 0.7), rng.uniform(-0.35, 0.35)
        s = rng.uniform(0.1, 0.2)
        b.box(x, y, 0.25, x + s, y + s, 0.25 + s * rng.uniform(1.0, 2.5), ore)
    for x in (-0.55, 0.55):
        for y in (-0.5, 0.5):
            b.cylinder(x, y + 0.04, 0.12, 0.12, 0.08, wheel, segments=10)
    return b.finish(origin)


def decor_shuttle(origin):
    """Small shuttle, 7 m wide and 11 m long, nose towards -Y: hull, cockpit glass, wings and engines; centred."""
    hull, wing, engine = sm.steel(), sm.gunmetal(), sm.material("reactor_glow", sm.REACTOR_GLOW, emission=8.0)
    b = Builder("decor_shuttle")
    b.box(-1.6, -3.0, 0.4, 1.6, 5.0, 2.8, hull)
    b.frustum(0.0, 1.6, -5.5, (0.9, 1.1), -3.0, (3.2, 2.4), hull)
    b.box(-0.7, -5.0, 1.8, 0.7, -3.0, 2.2, sm.glass())
    for side in (-1, 1):
        x0, x1 = (1.6, 4.4) if side > 0 else (-4.4, -1.6)
        b.box(x0, -0.5, 0.9, x1, 4.5, 1.2, wing)
        b.box(x0, -0.5, 0.9, x1, 0.0, 1.5, sm.hazard())
    for x in (-0.9, 0.9):
        b.cylinder(x, 5.7, 1.5, 0.5, 0.7, engine, segments=14)
    return b.finish(origin)


# ----- Security and command -----


def decor_weapon_rack(origin):
    """Wall rack with five rifles hung on a steel panel, 2 m wide; back on Y = 0."""
    panel, gun, grip = sm.gunmetal(), _mat("gun_steel", GUN_STEEL, metallic=0.8, roughness=0.4), sm.rubber()
    b = Builder("decor_weapon_rack")
    b.box(-1.0, -0.04, 0.8, 1.0, 0.0, 2.0, panel)
    for i in range(5):
        x = -0.8 + i * 0.4
        b.box(x, -0.1, 1.0, x + 0.06, -0.04, 1.9, gun)
        b.box(x - 0.03, -0.12, 1.0, x + 0.09, -0.04, 1.35, grip)
    return b.finish(origin)


def decor_gate(origin):
    """Security gate: two posts and a beam with a red light, 1.2 m wide, centred."""
    frame, light = sm.steel(), sm.led(sm.LED_RED, "led_red")
    b = Builder("decor_gate")
    for x in (-0.6, 0.55):
        b.box(x, -0.15, 0.0, x + 0.05, 0.15, 2.2, frame)
    b.box(-0.6, -0.15, 2.2, 0.6, 0.15, 2.35, frame)
    b.box(-0.08, -0.17, 2.25, 0.08, -0.15, 2.32, light)
    return b.finish(origin)


def decor_map_table(origin):
    """Tactical map table 2.4 x 1.4 m with a glowing green plot surface, centred."""
    body, glow = sm.gunmetal(), sm.phosphor(sm.PHOSPHOR_GREEN, "crt_green")
    b = Builder("decor_map_table")
    for x in (-1.1, 1.1):
        for y in (-0.6, 0.6):
            b.box(x - 0.06, y - 0.06, 0.0, x + 0.06, y + 0.06, 0.85, body)
    b.box(-1.2, -0.7, 0.85, 1.2, 0.7, 0.95, body)
    b.box(-1.1, -0.6, 0.95, 1.1, 0.6, 0.97, glow)
    return b.finish(origin)


def decor_suit(origin):
    """EVA suit on a stand: an orange torso, a white helmet with a visor and a backpack, centred."""
    white, orange = _mat("suit_white", SUIT_WHITE, roughness=0.6), _mat("suit_orange", SUIT_ORANGE, roughness=0.6)
    b = Builder("decor_suit")
    b.box(-0.3, -0.25, 0.0, 0.3, 0.25, 0.08, sm.gunmetal())
    for x in (-0.12, 0.12):
        b.box(x - 0.08, -0.08, 0.08, x + 0.08, 0.08, 0.9, white)
    b.box(-0.25, -0.15, 0.9, 0.25, 0.15, 1.5, orange)
    for x in (-0.4, 0.28):
        b.box(x, -0.07, 0.9, x + 0.12, 0.07, 1.4, white)
    b.box(-0.2, 0.15, 0.95, 0.2, 0.35, 1.45, white)
    b.box(-0.17, -0.17, 1.5, 0.17, 0.17, 1.8, white)
    b.box(-0.13, -0.18, 1.58, 0.13, -0.17, 1.72, sm.screen_glass())
    return b.finish(origin)


def decor_safe(origin):
    """Vault safe 1 x 1 x 1.4 m with a dial and a handle, centred, front towards -Y."""
    body, trim = sm.gunmetal(), sm.steel()
    b = Builder("decor_safe")
    b.box(-0.5, -0.5, 0.0, 0.5, 0.5, 1.4, body)
    b.box(-0.4, -0.52, 0.2, 0.4, -0.5, 1.2, trim)
    b.cylinder(0.0, -0.52, 0.8, 0.12, 0.04, sm.dark_steel(), segments=12)
    b.box(0.15, -0.58, 0.6, 0.35, -0.52, 0.65, sm.hazard())
    return b.finish(origin)


def decor_ai_core(origin):
    """AI core: a glowing blue column inside three rings on a steel plinth, centred."""
    plinth, ring = sm.steel(), sm.gunmetal()
    glow = sm.material("reactor_glow", sm.REACTOR_GLOW, emission=10.0)
    b = Builder("decor_ai_core")
    b.cylinder_z(0, 0, 0.0, 0.9, 0.3, plinth, segments=20)
    b.cylinder_z(0, 0, 0.3, 0.25, 2.0, glow, segments=12)
    for z in (0.7, 1.4, 2.1):
        b.cylinder_z(0, 0, z, 0.55, 0.08, ring, segments=20)
    b.cylinder_z(0, 0, 2.3, 0.9, 0.2, plinth, segments=20)
    return b.finish(origin)


def decor_gate_ring(origin):
    """Gateway frame: a 3 x 3 m square ring with a blue glow, standing on a plinth, front towards -Y."""
    frame, glow = sm.gunmetal(), sm.material("pad_glow", sm.REACTOR_GLOW, emission=6.0)
    b = Builder("decor_gate_ring")
    b.box(-1.6, -0.4, 0.0, 1.6, 0.4, 0.2, frame)
    b.box(-1.5, -0.2, 0.2, -1.2, 0.2, 3.2, frame)
    b.box(1.2, -0.2, 0.2, 1.5, 0.2, 3.2, frame)
    b.box(-1.5, -0.2, 3.0, 1.5, 0.2, 3.3, frame)
    b.box(-1.2, -0.05, 0.2, 1.2, 0.05, 3.0, glow)
    return b.finish(origin)


# ----- Service and crew life -----


def decor_altar(origin):
    """Chapel altar: a stone block with a cloth and four candles, 2 x 0.8 m, centred."""
    stone, cloth = sm.steel(), sm.fabric_red()
    flame = sm.material("candle_glow", CANDLE, emission=8.0)
    b = Builder("decor_altar")
    b.box(-1.0, -0.4, 0.0, 1.0, 0.4, 1.0, stone)
    b.box(-0.9, -0.45, 0.98, 0.9, 0.45, 1.02, cloth)
    for x in (-0.7, -0.25, 0.25, 0.7):
        b.box(x - 0.03, -0.03, 1.02, x + 0.03, 0.03, 1.2, sm.material("sheet", sm.SHEET_WHITE, roughness=0.9))
        b.box(x - 0.015, -0.015, 1.2, x + 0.015, 0.015, 1.26, flame)
    return b.finish(origin)


def decor_stage(origin):
    """Theatre stage 4 x 2.5 m raised 0.6 m with a red curtain at the back and a spotlight, centred."""
    deck, curtain, rig = sm.gunmetal(), sm.fabric_red(), sm.dark_steel()
    spot = sm.material("lamp_glow", sm.LAMP_WARM, emission=8.0)
    b = Builder("decor_stage")
    b.box(-2.0, -1.25, 0.0, 2.0, 1.25, 0.6, deck)
    b.box(-2.0, 1.1, 0.6, 2.0, 1.25, 3.0, curtain)
    for x in (-2.0, 1.95):
        b.box(x, -1.2, 0.6, x + 0.05, 1.1, 3.0, curtain)
    b.box(-1.8, -1.0, 2.8, 1.8, -0.95, 2.85, rig)
    for x in (-1.2, 0.0, 1.2):
        b.box(x - 0.08, -1.0, 2.6, x + 0.08, -0.85, 2.8, spot)
    return b.finish(origin)


def decor_judge_bench(origin):
    """Court bench: a raised desk 3 m wide with a front panel and a chair behind it, centred."""
    panel, top, seat = sm.gunmetal(), sm.steel(), sm.rubber()
    b = Builder("decor_judge_bench")
    b.box(-1.5, -0.5, 0.0, 1.5, 0.5, 1.2, panel)
    b.box(-1.55, -0.55, 1.2, 1.55, 0.55, 1.26, top)
    b.box(-0.3, 0.6, 0.0, 0.3, 1.0, 0.5, seat)
    b.box(-0.3, 0.95, 0.5, 0.3, 1.05, 1.3, seat)
    return b.finish(origin)


def decor_treadmill(origin):
    """Treadmill: a belt on a frame and a console with a phosphor display; faces -Y, centred."""
    frame, belt, screen = sm.gunmetal(), sm.rubber(), sm.phosphor(sm.PHOSPHOR_AMBER, "crt_amber")
    b = Builder("decor_treadmill")
    b.box(-0.4, -0.9, 0.0, 0.4, 0.9, 0.15, frame)
    b.box(-0.35, -0.85, 0.15, 0.35, 0.85, 0.2, belt)
    for x in (-0.4, 0.36):
        b.box(x, 0.7, 0.15, x + 0.04, 0.8, 1.2, frame)
    b.box(-0.35, 0.65, 1.1, 0.35, 0.85, 1.3, frame)
    b.box(-0.25, 0.64, 1.15, 0.25, 0.65, 1.25, screen)
    return b.finish(origin)


def decor_weight_rack(origin):
    """Weight bench with a barbell on stands and plates, 2 m wide, centred."""
    frame, plate, bar = sm.gunmetal(), sm.rubber(), sm.steel()
    b = Builder("decor_weight_rack")
    b.box(-0.2, -0.6, 0.0, 0.2, 0.6, 0.45, frame)
    for x in (-0.55, 0.55):
        b.box(x - 0.03, 0.55, 0.0, x + 0.03, 0.65, 1.1, frame)
    b.box(-1.0, 0.57, 1.05, 1.0, 0.63, 1.11, bar)
    for x in (-0.9, -0.75, 0.75, 0.9):
        b.box(x - 0.03, 0.47, 0.82, x + 0.03, 0.73, 1.34, plate)
    return b.finish(origin)


def decor_punching_bag(origin):
    """Punching bag on a frame, 2.3 m tall, centred."""
    frame, bag = sm.steel(), sm.fabric_red()
    b = Builder("decor_punching_bag")
    b.box(-0.5, -0.5, 0.0, 0.5, -0.4, 0.05, frame)
    b.box(-0.03, -0.5, 0.0, 0.03, -0.45, 2.3, frame)
    b.box(-0.03, -0.5, 2.25, 0.03, 0.0, 2.3, frame)
    b.cylinder_z(0.0, 0.0, 0.9, 0.18, 1.2, bag, segments=12)
    return b.finish(origin)


def decor_arcade(origin):
    """Arcade cabinet: a slanted lit screen over a control deck with a joystick; faces -Y, centred."""
    body, deck, glow = sm.gunmetal(), sm.hazard(), sm.phosphor(sm.PHOSPHOR_GREEN, "crt_green")
    b = Builder("decor_arcade")
    b.box(-0.35, -0.4, 0.0, 0.35, 0.4, 1.9, body)
    b.box(-0.3, -0.45, 1.0, 0.3, -0.35, 1.1, deck)
    b.box(-0.28, -0.41, 1.15, 0.28, -0.4, 1.75, glow)
    b.box(-0.02, -0.5, 1.1, 0.02, -0.46, 1.2, sm.led(sm.LED_RED, "led_red"))
    return b.finish(origin)


def decor_mop_bucket(origin):
    """Janitor cart: a bucket with a wringer and a mop leaning on it, centred."""
    bucket, mop = sm.hazard(), sm.beige()
    b = Builder("decor_mop_bucket")
    b.cylinder_z(0, 0, 0.0, 0.22, 0.4, bucket, segments=12)
    b.box(-0.02, 0.1, 0.4, 0.02, 0.14, 1.6, sm.steel())
    b.box(-0.08, 0.07, 1.45, 0.08, 0.17, 1.62, mop)
    return b.finish(origin)


def decor_extinguisher(origin):
    """Wall fire extinguisher with a bracket; back on Y = 0."""
    red, bracket = sm.material("extinguisher", (0.85, 0.1, 0.1, 1.0), roughness=0.4), sm.steel()
    b = Builder("decor_extinguisher")
    b.box(-0.08, -0.03, 0.9, 0.08, 0.0, 1.5, bracket)
    b.cylinder_z(0, -0.12, 0.95, 0.08, 0.45, red, segments=10)
    b.box(-0.02, -0.14, 1.4, 0.02, -0.1, 1.5, sm.dark_steel())
    return b.finish(origin)


def decor_water_tank(origin):
    """Hydroponics water tank: a blue upright cylinder with a pipe, centred."""
    tank, pipe = sm.material("water_blue", (0.3, 0.55, 0.85, 1.0), roughness=0.3), sm.steel()
    b = Builder("decor_water_tank")
    b.cylinder_z(0, 0, 0.0, 0.6, 1.4, tank, segments=16)
    b.cylinder_z(0, 0, 1.4, 0.5, 0.1, pipe, segments=16)
    b.box(0.55, -0.04, 0.1, 0.85, 0.04, 0.2, pipe)
    return b.finish(origin)


ITEMS = (
    decor_stove,
    decor_fridge,
    decor_sink,
    decor_prep_table,
    decor_keg,
    decor_bottle_shelf,
    decor_bar_stool,
    decor_iv_stand,
    decor_med_cabinet,
    decor_scanner,
    decor_chem_dispenser,
    decor_incubator,
    decor_analyzer,
    decor_dna_scanner,
    decor_surgery_lamp,
    decor_instrument_tray,
    decor_robot_arm,
    decor_mech,
    decor_gas_tank("yellow"),
    decor_gas_tank("blue"),
    decor_gas_tank("red"),
    decor_pen,
    decor_generator,
    decor_smes,
    decor_pump,
    decor_antenna,
    decor_tool_wall,
    decor_conveyor,
    decor_bin,
    decor_cart,
    decor_pigeonholes,
    decor_ore_cart,
    decor_shuttle,
    decor_weapon_rack,
    decor_gate,
    decor_map_table,
    decor_suit,
    decor_safe,
    decor_ai_core,
    decor_gate_ring,
    decor_altar,
    decor_stage,
    decor_judge_bench,
    decor_treadmill,
    decor_weight_rack,
    decor_punching_bag,
    decor_arcade,
    decor_mop_bucket,
    decor_extinguisher,
    decor_water_tank,
)


def build_items():
    objects = []
    for i, make in enumerate(ITEMS):
        row, col = divmod(i, ITEMS_PER_ROW)
        objects.append(make((col * ROW_STEP, ITEM_ROW_Y + row * ITEM_ROW_GAP * 2, 0.0)))
    return objects
