"""Blast and fire doors for corridors, and the ruin dressing for the abandoned wing: rubble, cobwebs, scorch marks,
puddles, glass shards, fallen beams and dangling cables.

Conventions as before: metres, standing items are centred on their footprint on Z = 0; floor marks are thin plates on Z = 0.
A gate spans its width along X and is thin along Y, so it is rotated a quarter turn to cross an east-west corridor.
"""

import math
import random

import station_models as sm

Builder = sm.Builder
mat = sm.material

ITEM4_ROW_Y = 190.0
ITEM4_STEP = 6.0
GATE_POST = 0.5
GATE_THICKNESS = 0.5
GATE_HEADER_Z = 2.5
GATE_TOP = 3.0
RUBBLE_SEED = 13


def soot():
    return mat("soot", (0.05, 0.05, 0.05, 1.0), roughness=1.0)


def gate(width):
    def make(origin):
        """Pressure gate across a corridor: wide posts holding the parked leaves, a header with two beacons and a
        hazard stripe, a gauge on each post and floor guide rails; the leaves slide shut from both sides."""
        frame, leaf, stripe = sm.gunmetal(), sm.dark_steel(), sm.hazard()
        gauge = sm.phosphor(sm.PHOSPHOR_AMBER, "crt_amber")
        b = Builder(f"decor_blast_gate{int(width)}")
        half = width / 2
        post = GATE_POST
        for sign in (-1, 1):
            x0, x1 = (-half, -half + post) if sign < 0 else (half - post, half)
            b.box(x0, -GATE_THICKNESS / 2, 0.0, x1, GATE_THICKNESS / 2, GATE_TOP, frame)
            for z in (0.0, 1.0, 2.0):
                b.box(x0, -GATE_THICKNESS / 2 - 0.01, z, x1, -GATE_THICKNESS / 2, z + 0.12, stripe if int(z) % 2 == 0 else sm.rubber())
                b.box(x0, GATE_THICKNESS / 2, z, x1, GATE_THICKNESS / 2 + 0.01, z + 0.12, stripe if int(z) % 2 == 0 else sm.rubber())
            inner = x1 if sign < 0 else x0
            b.box(inner - 0.06 * sign - 0.05, -0.12, 0.0, inner - 0.06 * sign + 0.05, 0.12, GATE_HEADER_Z, leaf)
            b.box(x0 + 0.08, -GATE_THICKNESS / 2 - 0.03, 1.3, x1 - 0.08, -GATE_THICKNESS / 2, 1.5, gauge)
            b.box(x0 + 0.08, GATE_THICKNESS / 2, 1.3, x1 - 0.08, GATE_THICKNESS / 2 + 0.03, 1.5, gauge)
        b.box(-half, -GATE_THICKNESS / 2, GATE_HEADER_Z, half, GATE_THICKNESS / 2, GATE_TOP, frame)
        b.box(-half + post, -0.2, GATE_HEADER_Z + 0.1, half - post, 0.2, GATE_TOP - 0.1, leaf)
        for x in (-0.5, 0.5):
            for y in (-GATE_THICKNESS / 2 - 0.05, GATE_THICKNESS / 2):
                b.box(x - 0.1, y, GATE_TOP - 0.3, x + 0.1, y + 0.05, GATE_TOP - 0.1, sm.led(sm.LED_RED, "led_red"))
        for y in (-0.15, 0.15):
            b.box(-half + post, y - 0.02, 0.0, half - post, y + 0.02, 0.01, sm.steel())
        return b.finish(origin)

    make.__name__ = f"decor_blast_gate{int(width)}"
    return make


def decor_debris(origin):
    """A heap of rubble 1.6 m across: broken slabs and chunks of plating."""
    rng = random.Random(RUBBLE_SEED)
    concrete, metal = mat("rubble", (0.4, 0.4, 0.42, 1.0), roughness=1.0), sm.dark_steel()
    b = Builder("decor_debris")
    for _ in range(14):
        x, y = rng.uniform(-0.7, 0.7), rng.uniform(-0.6, 0.6)
        size = rng.uniform(0.1, 0.3)
        z = rng.uniform(0.0, 0.2)
        b.box(x, y, z, x + size, y + size * rng.uniform(0.6, 1.4), z + size * rng.uniform(0.4, 1.0), concrete if rng.random() < 0.7 else metal)
    return b.finish(origin)


def decor_cobweb(origin):
    """Cobweb in a wall corner: crossed thin strands, centred on the corner point, hanging at 2.4 m."""
    web = mat("cobweb", (0.9, 0.9, 0.9, 1.0), roughness=1.0)
    b = Builder("decor_cobweb")
    for i in range(5):
        b.box(0.0, 0.0, 2.0 + i * 0.07, 0.5 - i * 0.07, 0.01, 2.01 + i * 0.07, web)
        b.box(0.0, 0.0, 2.0 + i * 0.07, 0.01, 0.5 - i * 0.07, 2.01 + i * 0.07, web)
    b.box(0.0, 0.0, 2.0, 0.01, 0.01, 2.4, web)
    return b.finish(origin)


def decor_scorch(origin):
    """Black scorch mark burnt into the floor, 1.4 x 1.0 m."""
    b = Builder("decor_scorch")
    b.box(-0.7, -0.4, 0.0, 0.7, 0.4, 0.006, soot())
    b.box(-0.4, -0.5, 0.0, 0.3, 0.5, 0.006, soot())
    b.box(-0.2, -0.3, 0.006, 0.5, 0.2, 0.008, mat("ash", (0.15, 0.15, 0.15, 1.0), roughness=1.0))
    return b.finish(origin)


def decor_puddle(origin):
    """Oily puddle on the floor, 1.2 x 0.9 m."""
    water = mat("puddle", (0.1, 0.15, 0.2, 1.0), metallic=0.8, roughness=0.05)
    b = Builder("decor_puddle")
    b.box(-0.6, -0.3, 0.0, 0.6, 0.3, 0.008, water)
    b.box(-0.4, -0.45, 0.0, 0.4, 0.45, 0.008, water)
    return b.finish(origin)


def decor_shards(origin):
    """Scattered glass shards on the floor."""
    rng = random.Random(RUBBLE_SEED + 1)
    glass = sm.glass()
    b = Builder("decor_shards")
    for _ in range(18):
        x, y = rng.uniform(-0.5, 0.5), rng.uniform(-0.5, 0.5)
        b.box(x, y, 0.0, x + rng.uniform(0.04, 0.14), y + rng.uniform(0.03, 0.1), 0.012, glass)
    return b.finish(origin)


def decor_fallen_beam(origin):
    """A steel beam lying across the floor and propped on rubble, 3 m long, along X."""
    beam, rubble = sm.steel(), mat("rubble", (0.4, 0.4, 0.42, 1.0), roughness=1.0)
    b = Builder("decor_fallen_beam")
    b.box(-1.5, -0.12, 0.2, 1.5, 0.12, 0.45, beam)
    b.box(-1.5, -0.2, 0.2, 1.5, -0.12, 0.25, beam)
    b.box(-1.0, -0.25, 0.0, -0.6, 0.25, 0.22, rubble)
    b.box(0.7, -0.25, 0.0, 1.1, 0.25, 0.3, rubble)
    return b.finish(origin)


def decor_hanging_cable(origin):
    """Torn cables dangling from the wall at 2.8 m with a bare end and a spark plate; back on Y = 0."""
    cable = mat("cable", (0.1, 0.1, 0.12, 1.0), roughness=0.9)
    b = Builder("decor_hanging_cable")
    for i, x in enumerate((-0.1, 0.0, 0.12)):
        b.box(x - 0.015, -0.04 - i * 0.02, 1.7 + i * 0.12, x + 0.015, -0.02 - i * 0.02, 2.8, cable)
        b.box(x - 0.01, -0.05 - i * 0.02, 1.65 + i * 0.12, x + 0.01, -0.03 - i * 0.02, 1.7 + i * 0.12, sm.hazard())
    b.box(-0.2, -0.04, 2.7, 0.2, 0.0, 2.8, sm.dark_steel())
    return b.finish(origin)


def decor_broken_lamp(origin):
    """A fluorescent tube hanging by one end from the wall, dark and tilted; back on Y = 0, 1 m long."""
    b = Builder("decor_broken_lamp")
    b.box(-0.5, -0.04, 2.5, -0.3, 0.0, 2.6, sm.gunmetal())
    b.slanted_slab(-0.3, 0.45, -0.1, -0.3, 0.05, 2.0, 2.5, sm.dark_steel()) if False else None
    b.box(-0.3, -0.12, 2.42, 0.5, -0.07, 2.47, sm.dark_steel())
    return b.finish(origin)


def decor_toppled_locker(origin):
    """A locker fallen on its back with its door hanging open, 1.9 m long, along X."""
    body, door = sm.gunmetal(), sm.dark_steel()
    b = Builder("decor_toppled_locker")
    b.box(-0.95, -0.3, 0.0, 0.95, 0.3, 0.5, body)
    b.box(-0.95, -0.3, 0.5, 0.95, -0.25, 0.52, door)
    b.box(0.5, -0.55, 0.0, 0.95, -0.3, 0.04, door)
    return b.finish(origin)


ITEMS4 = (
    gate(4.0),
    gate(2.0),
    decor_debris,
    decor_cobweb,
    decor_scorch,
    decor_puddle,
    decor_shards,
    decor_fallen_beam,
    decor_hanging_cable,
    decor_broken_lamp,
    decor_toppled_locker,
)


def build_items4():
    return [make((i * ITEM4_STEP, ITEM4_ROW_Y, 0.0)) for i, make in enumerate(ITEMS4)]
