"""Assembles small station scenes from the station_models segments: the captain's bridge with a
frameless windshield, the corridor from it to the atrium, and the atrium with benches and flowers.

Run in Blender: exec the module and call build_scenes(); the segment templates are built first.
Plan (metres, X east, Y north, the bridge's south-west interior corner is the origin):
  bridge   X 0..24,  Y 0..18          height 4, the north wall is a sill and a leaning windshield
  corridor X 11..13, Y -10.25..-0.25  height 3, leaves the bridge through a door in its south wall
  atrium   X -6..30, Y -46.5..-10.5   height 9
A wall is a slab one cell thick; "facing" is the side of the room its front face looks at.
"""

import math
import pathlib
import sys

import bpy

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import station_items  # noqa: E402
import station_items2  # noqa: E402
import station_items3  # noqa: E402
import station_items4  # noqa: E402
import station_small  # noqa: E402
import station_models as sm  # noqa: E402

TEMPLATES_COLLECTION = "templates"
SCENE_COLLECTION = "station_scene"
SLAB = sm.WALL_THICKNESS
FACING_ANGLE = {"S": 0.0, "N": math.pi, "E": math.pi / 2, "W": -math.pi / 2}
INWARD = {"S": (0, -1), "N": (0, 1), "E": (1, 0), "W": (-1, 0)}
TERMINAL_BACK = 0.3
TERMINAL_FRONT = 0.55
CHAIR_GAP = 0.95

BRIDGE_WIDTH = 24
BRIDGE_DEPTH = 18
BRIDGE_HEIGHT = 4
SILL_HEIGHT = 1
BRIDGE_CENTER = BRIDGE_WIDTH // 2

CORRIDOR_LENGTH = 10
CORRIDOR_START = -0.25 - CORRIDOR_LENGTH
CORRIDOR_HEIGHT = 3
CORRIDOR_X0 = BRIDGE_CENTER - 1
CORRIDOR_X1 = BRIDGE_CENTER + 1

ATRIUM_WIDTH = 36
ATRIUM_DEPTH = 36
ATRIUM_HEIGHT = 9
ATRIUM_Y1 = CORRIDOR_START - SLAB
ATRIUM_Y0 = ATRIUM_Y1 - ATRIUM_DEPTH
ATRIUM_X0 = BRIDGE_CENTER - ATRIUM_WIDTH // 2
ATRIUM_X1 = ATRIUM_X0 + ATRIUM_WIDTH
ATRIUM_CENTER_Y = (ATRIUM_Y0 + ATRIUM_Y1) / 2

LAMP_HEIGHT_FRACTION = 0.8
GARDEN_HALF = 5.0
BENCH_OFFSET = 1.4


class SceneBuilder:
    def __init__(self, templates, name=SCENE_COLLECTION, visible=None):
        self.templates = templates
        self.visible = visible
        self.collection = bpy.data.collections.new(name)
        bpy.context.scene.collection.children.link(self.collection)
        self.count = 0

    def _instance(self, template, location, angle_z=0.0, clip=True):
        """Place a template; with `clip` an item outside the station's outline is skipped (returns None)."""
        if clip and self.visible is not None and not self.visible(location[0], location[1]):
            return None
        obj = bpy.data.objects.new(f"{template}.{self.count:04d}", self.templates[template].data)
        obj.location = location
        obj.rotation_euler = (0.0, 0.0, angle_z)
        self.collection.objects.link(obj)
        self.count += 1
        return obj

    def _wall_origin(self, facing, fixed, along, length):
        """Origin of an element of `length` along a wall slab whose low edge is at `fixed`."""
        if facing == "S":
            return (along, fixed)
        if facing == "N":
            return (along + length, fixed + SLAB)
        if facing == "E":
            return (fixed + SLAB, along)
        return (fixed, along + length)

    def wall_piece(self, template, facing, fixed, along, z, length=1.0):
        x, y = self._wall_origin(facing, fixed, along, length)
        return self._instance(template, (x, y, z), FACING_ANGLE[facing], clip=False)

    def wall(self, facing, fixed, start, length, rows, pick, skip=()):
        """Tile `length` metres by `rows` rows with the templates `pick(i, row)` returns."""
        for row in range(rows):
            for i in range(length):
                if (i, row) not in skip:
                    self.wall_piece(pick(i, row), facing, fixed, start + i, row)

    def door_wall(self, facing, fixed, start, length, rows, door_at, pick):
        """A wall with a door at `door_at` metres from `start`, a lintel over it and panels above."""
        door_w = int(sm.DOOR_WIDTH)
        door_rows = range(math.ceil((sm.DOOR_HEIGHT + sm.LINTEL_HEIGHT) / 1.0))
        skip = {(i, row) for i in range(door_at, door_at + door_w) for row in door_rows}
        self.wall(facing, fixed, start, length, rows, pick, skip=skip)
        self.wall_piece("wall_door", facing, fixed, start + door_at, 0, length=sm.DOOR_WIDTH)
        self.wall_piece("wall_lintel", facing, fixed, start + door_at, sm.DOOR_HEIGHT, length=sm.DOOR_WIDTH)

    def floor(self, x0, y0, width, depth):
        for ix in range(width):
            for iy in range(depth):
                self._instance("floor_deck", (x0 + ix, y0 + iy, 0.0))

    def mount(self, template, facing, fixed, along, z, inset=0.0):
        """Hang decor with its back on the wall's front face; `inset` moves deep items off the wall."""
        x, y = self._wall_origin(facing, fixed, along, 0.0)
        dx, dy = INWARD[facing]
        return self._instance(template, (x + dx * inset, y + dy * inset, z), FACING_ANGLE[facing])

    def stand(self, template, x, y, facing="S", z=0.0):
        return self._instance(template, (x, y, z), FACING_ANGLE[facing])

    def pillar(self, x, y, height):
        for z in range(height):
            self._instance("decor_pillar", (x, y, z))

    def station(self, x, y):
        """A terminal whose screen looks south at (x, y) and a crew seat in front of it, looking north."""
        self.stand("decor_terminal", x, y, "S")
        self.stand("decor_crew_chair", x, y - TERMINAL_FRONT - CHAIR_GAP, "N")


def alternate(a, b):
    return lambda i, row: a if (i + row) % 2 == 0 else b


def spread(center, count, step):
    return [center + (i - (count - 1) / 2) * step for i in range(count)]


def build_bridge(s):
    pick = alternate("wall_plate_riveted", "wall_seamed")
    s.floor(0, 0, BRIDGE_WIDTH, BRIDGE_DEPTH)
    s.wall("S", BRIDGE_DEPTH, 0, BRIDGE_WIDTH, SILL_HEIGHT, pick)
    for i in range(BRIDGE_WIDTH):
        s.wall_piece("wall_glass_slope", "S", BRIDGE_DEPTH, i, SILL_HEIGHT)
    s.wall("E", -SLAB, 0, BRIDGE_DEPTH, BRIDGE_HEIGHT, pick)
    s.wall("W", BRIDGE_WIDTH, 0, BRIDGE_DEPTH, BRIDGE_HEIGHT, pick)
    s.door_wall("N", -SLAB, 0, BRIDGE_WIDTH, BRIDGE_HEIGHT, BRIDGE_CENTER - 1, pick)

    rows = ((BRIDGE_DEPTH - 0.5, 8, 2.7), (BRIDGE_DEPTH - 5.5, 6, 3.2), (BRIDGE_DEPTH - 10.5, 4, 3.8))
    for y, count, step in rows:
        for x in spread(BRIDGE_CENTER, count, step):
            s.station(x, y)
    s.stand("decor_captain_chair", BRIDGE_CENTER, 3.0, "N")

    lamp_z = BRIDGE_HEIGHT - 0.6
    for y in range(3, BRIDGE_DEPTH, 4):
        s.mount("decor_lamp_tube", "E", -SLAB, y, lamp_z)
        s.mount("decor_lamp_tube", "W", BRIDGE_WIDTH, y, lamp_z)
    for x in (2.5, 6.5, 17.5, 21.5):
        s.mount("decor_lamp_tube", "N", -SLAB, x, lamp_z)
    for y in (5.0, 13.0):
        s.mount("decor_switch_panel", "E", -SLAB, y, 1.3)
        s.mount("decor_switch_panel", "W", BRIDGE_WIDTH, y, 1.3)
    s.mount("decor_wall_vent_grille", "E", -SLAB, 9.0, 2.0)
    s.mount("decor_wall_vent_grille", "W", BRIDGE_WIDTH, 9.0, 2.0)
    s.mount("decor_pipe_panel", "E", -SLAB, 1.5, 1.6)
    s.mount("decor_pipe_panel", "W", BRIDGE_WIDTH, 1.5, 1.6)
    for x in (BRIDGE_CENTER - 3, BRIDGE_CENTER + 3):
        s.mount("decor_lamp_beacon", "N", -SLAB, x, 2.3)


def corridor_pick(i, row):
    return "wall_hazard" if row == 0 else "wall_corrugated"


def build_corridor(s):
    s.floor(CORRIDOR_X0, int(CORRIDOR_START), 2, CORRIDOR_LENGTH)
    s.wall("E", CORRIDOR_X0 - SLAB, CORRIDOR_START, CORRIDOR_LENGTH, CORRIDOR_HEIGHT, corridor_pick)
    s.wall("W", CORRIDOR_X1, CORRIDOR_START, CORRIDOR_LENGTH, CORRIDOR_HEIGHT, corridor_pick)
    for y in (-8.5, -5.5, -2.5):
        s.mount("decor_lamp_tube", "E", CORRIDOR_X0 - SLAB, y, 2.6)
        s.mount("decor_lamp_tube", "W", CORRIDOR_X1, y, 2.6)
    s.mount("decor_wall_vent_grille", "E", CORRIDOR_X0 - SLAB, -6.0, 1.5)
    s.mount("decor_pipe_panel", "W", CORRIDOR_X1, -6.0, 1.6)
    s.mount("decor_switch_panel", "E", CORRIDOR_X0 - SLAB, -1.5, 1.3)


def build_atrium(s):
    pick = alternate("wall_bulkhead", "wall_plate_riveted")
    s.floor(ATRIUM_X0, int(ATRIUM_Y0), ATRIUM_WIDTH, ATRIUM_DEPTH)
    s.wall("N", ATRIUM_Y0 - SLAB, ATRIUM_X0, ATRIUM_WIDTH, ATRIUM_HEIGHT, pick)
    s.wall("E", ATRIUM_X0 - SLAB, ATRIUM_Y0, ATRIUM_DEPTH, ATRIUM_HEIGHT, pick)
    s.wall("W", ATRIUM_X1, ATRIUM_Y0, ATRIUM_DEPTH, ATRIUM_HEIGHT, pick)
    s.door_wall("S", ATRIUM_Y1, ATRIUM_X0, ATRIUM_WIDTH, ATRIUM_HEIGHT, CORRIDOR_X0 - ATRIUM_X0, pick)

    lamp_z = ATRIUM_HEIGHT * LAMP_HEIGHT_FRACTION
    for x in range(ATRIUM_X0 + 3, ATRIUM_X1, 6):
        s.mount("decor_lamp_tube", "N", ATRIUM_Y0 - SLAB, x, lamp_z)
        s.mount("decor_wall_vent_grille", "N", ATRIUM_Y0 - SLAB, x + 3, 3.0)
    for y in range(int(ATRIUM_Y0) + 3, int(ATRIUM_Y1), 6):
        s.mount("decor_lamp_tube", "E", ATRIUM_X0 - SLAB, y, lamp_z)
        s.mount("decor_lamp_tube", "W", ATRIUM_X1, y, lamp_z)
        s.mount("decor_wall_vent_grille", "E", ATRIUM_X0 - SLAB, y + 3, 3.0)
        s.mount("decor_wall_vent_grille", "W", ATRIUM_X1, y + 3, 3.0)

    # Planters along the three far walls, with a bench in front of every other one.
    planter_inset = 0.35
    for x in spread(BRIDGE_CENTER, 6, 5.5):
        s.mount("decor_planter", "N", ATRIUM_Y0 - SLAB, x, 0.0, inset=planter_inset)
    for y in spread(ATRIUM_CENTER_Y, 5, 6.5):
        s.mount("decor_planter", "E", ATRIUM_X0 - SLAB, y, 0.0, inset=planter_inset)
        s.mount("decor_planter", "W", ATRIUM_X1, y, 0.0, inset=planter_inset)

    # A central garden: a square of four planters with benches around it facing outwards.
    cx, cy = BRIDGE_CENTER, ATRIUM_CENTER_Y
    for dx in (-2.0, 2.0):
        s.stand("decor_planter", cx + dx, cy - GARDEN_HALF / 2, "S")
        s.stand("decor_planter", cx + dx, cy + GARDEN_HALF / 2, "N")
    for dx in (-2.0, 2.0):
        s.stand("decor_bench", cx + dx, cy - GARDEN_HALF / 2 - BENCH_OFFSET, "S")
        s.stand("decor_bench", cx + dx, cy + GARDEN_HALF / 2 + BENCH_OFFSET, "N")
    for dy in (-1.0, 1.0):
        s.stand("decor_bench", cx - 2 * GARDEN_HALF / 2 - BENCH_OFFSET - 1.0, cy + dy * 1.1, "W")
        s.stand("decor_bench", cx + 2 * GARDEN_HALF / 2 + BENCH_OFFSET + 1.0, cy + dy * 1.1, "E")

    for dx in (-12.0, 12.0):
        for dy in (-9.0, 9.0):
            s.pillar(cx + dx, cy + dy, ATRIUM_HEIGHT)
            s.stand("decor_bench", cx + dx, cy + dy + (-1.4 if dy < 0 else 1.4), "S" if dy < 0 else "N")


def prepare_templates():
    """Build the segment library and park it in a hidden collection; returns {name: object}."""
    templates = {obj.name: obj for obj in sm.build_all() + station_items.build_items() + station_items2.build_items2() + station_items3.build_items3() + station_items4.build_items4() + station_small.build_small()}
    hidden = bpy.data.collections.get(TEMPLATES_COLLECTION) or bpy.data.collections.new(TEMPLATES_COLLECTION)
    if hidden.name not in bpy.context.scene.collection.children:
        bpy.context.scene.collection.children.link(hidden)
    for obj in templates.values():
        for coll in list(obj.users_collection):
            coll.objects.unlink(obj)
        hidden.objects.link(obj)
    hidden.hide_viewport = True
    hidden.hide_render = True
    return templates


def clear_scene_collection(name):
    old = bpy.data.collections.get(name)
    if old:
        bpy.data.batch_remove(list(old.objects))
        bpy.data.collections.remove(old)


def build_scenes():
    templates = prepare_templates()
    clear_scene_collection(SCENE_COLLECTION)
    builder = SceneBuilder(templates)
    build_bridge(builder)
    build_corridor(builder)
    build_atrium(builder)
    return builder
