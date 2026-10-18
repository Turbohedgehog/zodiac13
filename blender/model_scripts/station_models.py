"""Generates the station's wall panel variants and decor in Blender (bpy).

The style is 80s retro-futurism: bare brushed metal, rivets and seams, beige plastic machines,
CRT monitors with phosphor screens, floppy drives, tape reels, toggle switches and caged lamps.

Run headless: blender --background --python blender/model_scripts/station_models.py
Units are metres; one grid cell is 0.25 m (`kCellSize`), a wall is one cell thick.
A wall panel is 1 m wide and 1 m tall (the repeatable middle zone) and lies in the XZ plane;
its front face looks towards -Y and its thickness runs along +Y from 0 to WALL_THICKNESS.
Decor has its back on Y = 0 and its front towards -Y (floor and desk items stand on Z = 0),
ready to hang on a wall face or stand on a floor.
"""

import math
import pathlib

import bmesh
import bpy
from mathutils import Matrix

CELL = 0.25
WALL_THICKNESS = CELL
PANEL_SIZE = 4 * CELL
EXPORT_DIR = pathlib.Path(__file__).resolve().parents[1] / "export"
EXPORT_NAME = "station_models.fbx"
GENERATED_PREFIXES = ("wall_", "decor_", "floor_")

STEEL = (0.9, 0.91, 0.93, 1.0)
GUNMETAL = (0.62, 0.65, 0.7, 1.0)
DARK_STEEL = (0.36, 0.38, 0.42, 1.0)
HAZARD = (0.9, 0.62, 0.05, 1.0)
BEIGE = (0.84, 0.8, 0.7, 1.0)
BEIGE_DARK = (0.58, 0.55, 0.48, 1.0)
PHOSPHOR_GREEN = (0.25, 1.0, 0.4, 1.0)
PHOSPHOR_AMBER = (1.0, 0.62, 0.1, 1.0)
GLASS_DARK = (0.02, 0.05, 0.04, 1.0)
LAMP_WARM = (1.0, 0.88, 0.62, 1.0)
LAMP_TUBE = (0.85, 0.95, 1.0, 1.0)
LED_RED = (1.0, 0.1, 0.05, 1.0)
LED_GREEN = (0.1, 1.0, 0.2, 1.0)
LED_AMBER = (1.0, 0.7, 0.05, 1.0)
RUBBER = (0.04, 0.04, 0.045, 1.0)
KEY_CAP = (0.55, 0.52, 0.45, 1.0)


def material(name, color, emission=0.0, metallic=0.0, roughness=0.6):
    mat = bpy.data.materials.get(name) or bpy.data.materials.new(name)
    mat.use_nodes = True
    bsdf = next(n for n in mat.node_tree.nodes if n.type == "BSDF_PRINCIPLED")
    bsdf.inputs["Base Color"].default_value = color
    bsdf.inputs["Metallic"].default_value = metallic
    bsdf.inputs["Roughness"].default_value = roughness
    bsdf.inputs["Emission Color"].default_value = color
    bsdf.inputs["Emission Strength"].default_value = emission
    mat.diffuse_color = color
    mat.metallic = metallic
    mat.roughness = roughness
    if emission == 0.0 and color[3] >= 1.0:
        add_surface_texture(mat, bsdf, color, metallic)
    return mat


GRAIN_SCALE = 60.0
BLOTCH_SCALE = 4.0
SCRATCH_SCALE = (2.0, 90.0, 2.0)
BUMP_STRENGTH = 0.15
TINT_SPREAD = 0.12


def add_surface_texture(mat, bsdf, color, metallic):
    """Procedural wear: soft colour blotches, fine grain bump and, on metal, brushed streaks in the roughness."""
    tree = mat.node_tree
    if tree.nodes.get("wear_grain"):
        return
    nodes, links = tree.nodes, tree.links
    coords = nodes.new("ShaderNodeTexCoord")
    blotch = nodes.new("ShaderNodeTexNoise")
    blotch.inputs["Scale"].default_value = BLOTCH_SCALE
    blotch.inputs["Detail"].default_value = 3.0
    links.new(coords.outputs["Object"], blotch.inputs["Vector"])
    grain = nodes.new("ShaderNodeTexNoise")
    grain.name = "wear_grain"
    grain.inputs["Scale"].default_value = GRAIN_SCALE
    grain.inputs["Detail"].default_value = 6.0
    links.new(coords.outputs["Object"], grain.inputs["Vector"])
    ramp = nodes.new("ShaderNodeValToRGB")
    ramp.color_ramp.elements[0].color = (1 - TINT_SPREAD, 1 - TINT_SPREAD, 1 - TINT_SPREAD, 1.0)
    ramp.color_ramp.elements[1].color = (1 + TINT_SPREAD / 2, 1 + TINT_SPREAD / 2, 1 + TINT_SPREAD / 2, 1.0)
    links.new(blotch.outputs["Fac"], ramp.inputs["Fac"])
    tint = nodes.new("ShaderNodeMix")
    tint.data_type = "RGBA"
    tint.blend_type = "MULTIPLY"
    tint.inputs["Factor"].default_value = 1.0
    tint.inputs["A"].default_value = color
    links.new(ramp.outputs["Color"], tint.inputs["B"])
    links.new(tint.outputs["Result"], bsdf.inputs["Base Color"])
    bump = nodes.new("ShaderNodeBump")
    bump.inputs["Strength"].default_value = BUMP_STRENGTH
    bump.inputs["Distance"].default_value = 0.01
    links.new(grain.outputs["Fac"], bump.inputs["Height"])
    links.new(bump.outputs["Normal"], bsdf.inputs["Normal"])
    if metallic > 0.3:
        streaks = nodes.new("ShaderNodeTexNoise")
        streaks.inputs["Scale"].default_value = 1.0
        streaks.inputs["Detail"].default_value = 2.0
        scale = nodes.new("ShaderNodeMapping")
        scale.inputs["Scale"].default_value = SCRATCH_SCALE
        links.new(coords.outputs["Object"], scale.inputs["Vector"])
        links.new(scale.outputs["Vector"], streaks.inputs["Vector"])
        rough = nodes.new("ShaderNodeMapRange")
        rough.inputs["To Min"].default_value = max(bsdf.inputs["Roughness"].default_value - 0.15, 0.1)
        rough.inputs["To Max"].default_value = bsdf.inputs["Roughness"].default_value + 0.2
        links.new(streaks.outputs["Fac"], rough.inputs["Value"])
        links.new(rough.outputs["Result"], bsdf.inputs["Roughness"])


def steel():
    return material("steel", STEEL, metallic=0.55, roughness=0.4)


def gunmetal():
    return material("gunmetal", GUNMETAL, metallic=0.6, roughness=0.45)


def dark_steel():
    return material("dark_steel", DARK_STEEL, metallic=0.6, roughness=0.5)


def hazard():
    return material("hazard", HAZARD, metallic=0.6, roughness=0.5)


def beige():
    return material("beige_plastic", BEIGE, roughness=0.55)


def beige_dark():
    return material("beige_plastic_dark", BEIGE_DARK, roughness=0.6)


def rubber():
    return material("rubber", RUBBER, roughness=0.9)


def screen_glass():
    return material("crt_glass", GLASS_DARK, metallic=0.0, roughness=0.15)


def phosphor(color, name):
    return material(name, color, emission=1.6, roughness=0.2)


def led(color, name):
    return material(name, color, emission=6.0, roughness=0.3)


class Builder:
    """Collects boxes, frustums and cylinders into one mesh object."""

    def __init__(self, name):
        self.name = name
        self.bm = bmesh.new()
        self.materials = []

    def _material_index(self, mat):
        if mat not in self.materials:
            self.materials.append(mat)
        return self.materials.index(mat)

    def _assign(self, verts, mat):
        index = self._material_index(mat)
        for face in {f for v in verts for f in v.link_faces}:
            face.material_index = index

    def box(self, x0, y0, z0, x1, y1, z1, mat):
        geom = bmesh.ops.create_cube(self.bm, size=1.0)
        for v in geom["verts"]:
            v.co.x = x0 if v.co.x < 0 else x1
            v.co.y = y0 if v.co.y < 0 else y1
            v.co.z = z0 if v.co.z < 0 else z1
        self._assign(geom["verts"], mat)

    def frustum(self, cx, cz, y_front, front, y_back, back, mat):
        """Truncated pyramid from a front rectangle to a smaller back one; sizes are (width, height)."""
        geom = bmesh.ops.create_cube(self.bm, size=1.0)
        for v in geom["verts"]:
            (width, height), y = (front, y_front) if v.co.y < 0 else (back, y_back)
            v.co.x = cx + (width / 2 if v.co.x > 0 else -width / 2)
            v.co.z = cz + (height / 2 if v.co.z > 0 else -height / 2)
            v.co.y = y
        self._assign(geom["verts"], mat)

    def slanted_slab(self, x0, x1, y_bottom, y_top, thickness, z0, z1, mat):
        """Slab leaning in the YZ plane: its bottom front edge at y_bottom, its top front edge at y_top."""
        geom = bmesh.ops.create_cube(self.bm, size=1.0)
        for v in geom["verts"]:
            y_front = y_top if v.co.z > 0 else y_bottom
            v.co.x = x0 if v.co.x < 0 else x1
            v.co.y = y_front + (thickness if v.co.y > 0 else 0.0)
            v.co.z = z1 if v.co.z > 0 else z0
        self._assign(geom["verts"], mat)

    def cylinder_z(self, cx, cy, z0, radius, height, mat, segments=16):
        """Upright cylinder standing on z0."""
        geom = bmesh.ops.create_cone(
            self.bm, cap_ends=True, segments=segments, radius1=radius, radius2=radius, depth=height
        )
        bmesh.ops.translate(self.bm, verts=geom["verts"], vec=(cx, cy, z0 + height / 2))
        self._assign(geom["verts"], mat)

    def cylinder(self, cx, cy, cz, radius, depth, mat, segments=12):
        """Cylinder whose axis runs along Y, spanning cy .. cy - depth (towards the viewer)."""
        geom = bmesh.ops.create_cone(
            self.bm, cap_ends=True, segments=segments, radius1=radius, radius2=radius, depth=depth
        )
        bmesh.ops.rotate(self.bm, verts=geom["verts"], cent=(0, 0, 0), matrix=Matrix.Rotation(math.pi / 2, 3, "X"))
        bmesh.ops.translate(self.bm, verts=geom["verts"], vec=(cx, cy - depth / 2, cz))
        self._assign(geom["verts"], mat)

    def finish(self, location):
        mesh = bpy.data.meshes.new(self.name)
        self.bm.normal_update()
        self.bm.to_mesh(mesh)
        self.bm.free()
        for mat in self.materials:
            mesh.materials.append(mat)
        for poly in mesh.polygons:
            poly.use_smooth = False
        obj = bpy.data.objects.new(self.name, mesh)
        obj.location = location
        bpy.context.scene.collection.objects.link(obj)
        return obj


def clear_generated():
    bpy.data.batch_remove([o for o in bpy.data.objects if o.name.startswith(GENERATED_PREFIXES)])
    for mesh in [m for m in bpy.data.meshes if m.users == 0]:
        bpy.data.meshes.remove(mesh)


def rivet_row(b, z, xs, mat, y=0.0):
    for x in xs:
        b.cylinder(x, y, z, 0.012, 0.01, mat, segments=6)


# ----- Wall panels -----

RIVET_XS = (0.06, 0.25, 0.5, 0.75, 0.94)


RELIEF = 0.008
RIVET_SIZE = 0.03
PLATE_RELIEF = 0.005
RIB_RELIEF = 0.01


def _rivets(b, points, mat):
    half = RIVET_SIZE / 2
    for x, z in points:
        b.box(x - half, -RELIEF, z - half, x + half, 0.0, z + half, mat)


def _plate(b, x0, z0, x1, z1, mat, relief=PLATE_RELIEF):
    b.box(x0, -relief, z0, x1, 0.0, z1, mat)


def _frame(b, margin, width, mat):
    far = PANEL_SIZE - margin
    _plate(b, margin, margin, far, margin + width, mat)
    _plate(b, margin, far - width, far, far, mat)
    _plate(b, margin, margin + width, margin + width, far - width, mat)
    _plate(b, far - width, margin + width, far, far - width, mat)


def wall_plate_riveted(origin):
    """A wall panel with low relief: small rivets and thin horizontal details that run on across neighbouring panels."""
    b = Builder("wall_plate_riveted")
    b.box(0, 0, 0, PANEL_SIZE, WALL_THICKNESS, PANEL_SIZE, steel())
    _plate(b, 0.0, 0.485, PANEL_SIZE, 0.515, dark_steel(), 0.004)
    _rivets(b, [(x, z) for x in (0.125, 0.375, 0.625, 0.875) for z in (0.08, 0.92)], gunmetal())
    return b.finish(origin)

def wall_seamed(origin):
    """A wall panel with low relief: small rivets and thin horizontal details that run on across neighbouring panels."""
    b = Builder("wall_seamed")
    b.box(0, 0, 0, PANEL_SIZE, WALL_THICKNESS, PANEL_SIZE, steel())
    _plate(b, 0.0, 0.47, PANEL_SIZE, 0.53, dark_steel(), 0.005)
    _rivets(b, [(x, z) for x in (0.1, 0.3, 0.5, 0.7, 0.9) for z in (0.41, 0.59)], gunmetal())
    return b.finish(origin)

def wall_corrugated(origin):
    """A wall panel with low relief: small rivets, thin overlay plates and shallow patterns."""
    b = Builder("wall_corrugated")
    b.box(0, 0, 0, PANEL_SIZE, WALL_THICKNESS, PANEL_SIZE, gunmetal())
    for i in range(8):
        _plate(b, 0.03 + i * 0.125, 0.0, 0.08 + i * 0.125, PANEL_SIZE, steel(), RIB_RELIEF)
    return b.finish(origin)

def wall_vented(origin):
    """A wall panel with low relief: small rivets and thin horizontal details that run on across neighbouring panels."""
    b = Builder("wall_vented")
    b.box(0, 0, 0, PANEL_SIZE, WALL_THICKNESS, PANEL_SIZE, steel())
    for i in range(5):
        _plate(b, 0.0, 0.22 + i * 0.12, PANEL_SIZE, 0.245 + i * 0.12, dark_steel(), 0.003)
    _rivets(b, [(0.125, 0.08), (0.375, 0.08), (0.625, 0.08), (0.875, 0.08)], gunmetal())
    return b.finish(origin)

def wall_hazard(origin):
    """A wall panel with low relief: small rivets, thin overlay plates and shallow patterns."""
    b = Builder("wall_hazard")
    stripes = 8
    step = PANEL_SIZE / stripes
    b.box(0, 0, FLAT_BAND_HEIGHT, PANEL_SIZE, WALL_THICKNESS, PANEL_SIZE, steel())
    for i in range(stripes):
        b.box(i * step, 0, 0, (i + 1) * step, WALL_THICKNESS, FLAT_BAND_HEIGHT, hazard() if i % 2 == 0 else dark_steel())
    _rivets(b, [(x, 0.27) for x in (0.1, 0.3, 0.5, 0.7, 0.9)], gunmetal())
    return b.finish(origin)

def wall_bulkhead(origin):
    """A wall panel with low relief: small rivets and thin horizontal details that run on across neighbouring panels."""
    b = Builder("wall_bulkhead")
    b.box(0, 0, 0, PANEL_SIZE, WALL_THICKNESS, PANEL_SIZE, steel())
    for x in (0.125, 0.375, 0.625, 0.875):
        for z in (0.3, 0.5, 0.7):
            _plate(b, x - 0.02, z - 0.02, x + 0.02, z + 0.02, dark_steel(), 0.006)
    _plate(b, 0.0, 0.0, PANEL_SIZE, 0.03, gunmetal(), 0.004)
    return b.finish(origin)

def decor_lamp_ceiling(origin):
    """Caged bulkhead lamp: round steel base, glowing dome and four cage bars."""
    base, glow, cage = gunmetal(), material("lamp_glow", LAMP_WARM, emission=10.0), dark_steel()
    b = Builder("decor_lamp_ceiling")
    b.cylinder(0, 0.0, 0, 0.13, 0.03, base, segments=16)
    b.cylinder(0, -0.03, 0, 0.085, 0.06, glow, segments=16)
    for dx, dz in ((0.1, 0), (-0.1, 0), (0, 0.1), (0, -0.1)):
        b.box(dx - 0.008, -0.1, dz - 0.008, dx + 0.008, -0.03, dz + 0.008, cage)
    b.cylinder(0, -0.1, 0, 0.1, 0.012, cage, segments=16)
    return b.finish(origin)


def decor_lamp_tube(origin):
    """Wall-mounted fluorescent tube, 1 m long, in a steel channel with end caps."""
    housing, glow = gunmetal(), material("lamp_tube", LAMP_TUBE, emission=10.0)
    b = Builder("decor_lamp_tube")
    b.box(-0.5, -0.03, -0.045, 0.5, 0.0, 0.045, housing)
    b.box(-0.46, -0.05, -0.02, 0.46, -0.03, 0.02, glow)
    for x in (-0.5, 0.46):
        b.box(x, -0.06, -0.04, x + 0.04, 0.0, 0.04, housing)
    return b.finish(origin)


def decor_lamp_beacon(origin):
    """Amber warning beacon: steel base and a faceted glowing lens."""
    base, lens = gunmetal(), material("beacon_amber", PHOSPHOR_AMBER, emission=8.0)
    b = Builder("decor_lamp_beacon")
    b.cylinder(0, 0.0, 0, 0.09, 0.03, base, segments=12)
    b.cylinder(0, -0.03, 0, 0.065, 0.07, lens, segments=12)
    b.cylinder(0, -0.1, 0, 0.04, 0.02, base, segments=12)
    return b.finish(origin)


# ----- Wall decor -----


def decor_switch_panel(origin):
    """Wall switch panel: steel plate, a row of toggle switches and three indicator lights."""
    plate, toggle = gunmetal(), steel()
    red, green, amber = led(LED_RED, "led_red"), led(LED_GREEN, "led_green"), led(LED_AMBER, "led_amber")
    b = Builder("decor_switch_panel")
    b.box(-0.2, -0.015, -0.14, 0.2, 0.0, 0.14, plate)
    for i in range(5):
        x = -0.15 + i * 0.075
        b.box(x - 0.012, -0.022, -0.09, x + 0.012, -0.015, -0.03, dark_steel())
        b.cylinder(x, -0.022, -0.06, 0.008, 0.03, toggle, segments=6)
    for x, mat in ((-0.1, red), (0.0, green), (0.1, amber)):
        b.cylinder(x, -0.015, 0.07, 0.014, 0.012, mat, segments=8)
    return b.finish(origin)


def decor_pipe_panel(origin):
    """Wall conduit panel: a steel box with two pipes and valve wheels."""
    box, pipe = gunmetal(), steel()
    b = Builder("decor_pipe_panel")
    b.box(-0.4, -0.05, -0.12, 0.4, 0.0, 0.12, box)
    for z in (-0.05, 0.05):
        b.box(-0.45, -0.09, z - 0.02, 0.45, -0.05, z + 0.02, pipe)
    for x in (-0.2, 0.2):
        b.cylinder(x, -0.09, 0.0, 0.04, 0.02, hazard(), segments=8)
    return b.finish(origin)


def decor_wall_vent_grille(origin):
    """Small round exhaust grille: a ring and three crossbars."""
    ring, bars = gunmetal(), dark_steel()
    b = Builder("decor_wall_vent_grille")
    b.cylinder(0, 0.0, 0, 0.14, 0.03, ring, segments=16)
    b.cylinder(0, -0.03, 0, 0.115, 0.005, dark_steel(), segments=16)
    for z in (-0.06, 0.0, 0.06):
        b.box(-0.115, -0.045, z - 0.008, 0.115, -0.03, z + 0.008, bars)
    return b.finish(origin)


# ----- Computers (CRT era) -----


def crt_monitor(b, cx, cz, front_y, screen_color, name, size=(0.34, 0.28), depth=0.3):
    """CRT monitor body: a bezel, a convex glass face and the tapering tube housing behind."""
    body, bezel = beige(), beige_dark()
    width, height = size
    b.box(cx - width / 2, front_y, cz - height / 2, cx + width / 2, front_y + 0.04, cz + height / 2, bezel)
    inset = 0.03
    b.box(
        cx - width / 2 + inset,
        front_y - 0.012,
        cz - height / 2 + inset,
        cx + width / 2 - inset,
        front_y,
        cz + height / 2 - inset,
        phosphor(screen_color, name),
    )
    b.frustum(cx, cz, front_y + 0.04, (width - 0.02, height - 0.02), front_y + depth, (width * 0.45, height * 0.45), body)


def decor_monitor_crt(origin):
    """Desktop CRT monitor with a green phosphor screen on a swivel foot, standing on Z = 0."""
    b = Builder("decor_monitor_crt")
    foot = beige_dark()
    b.box(-0.12, -0.1, 0.0, 0.12, 0.12, 0.025, foot)
    b.box(-0.05, -0.05, 0.025, 0.05, 0.06, 0.06, foot)
    crt_monitor(b, 0.0, 0.06 + 0.16, -0.12, PHOSPHOR_GREEN, "crt_green", depth=0.3)
    return b.finish(origin)


def decor_monitor_crt_wall(origin):
    """CRT monitor on a wall bracket, amber phosphor; the deep tube sticks out from the wall."""
    b = Builder("decor_monitor_crt_wall")
    bracket = gunmetal()
    b.box(-0.08, -0.03, -0.08, 0.08, 0.0, 0.08, bracket)
    crt_monitor(b, 0.0, 0.0, -0.3, PHOSPHOR_AMBER, "crt_amber", depth=0.3)
    return b.finish(origin)


def decor_keyboard(origin):
    """Chunky keyboard: a wedge-shaped case and three rows of key caps."""
    case, cap = beige(), material("key_cap", KEY_CAP, roughness=0.6)
    b = Builder("decor_keyboard")
    b.box(-0.22, -0.1, 0.0, 0.22, 0.1, 0.03, case)
    for row in range(3):
        for i in range(12):
            x = -0.2 + i * 0.034
            y = -0.075 + row * 0.05
            b.box(x, y, 0.03, x + 0.028, y + 0.04, 0.042, cap)
    return b.finish(origin)


def decor_computer(origin):
    """Desktop system unit: beige case with two 5.25-inch floppy drives, a power switch and LEDs."""
    case, drive, slot = beige(), beige_dark(), dark_steel()
    run, disk = led(LED_GREEN, "led_green"), led(LED_RED, "led_red")
    b = Builder("decor_computer")
    b.box(-0.25, -0.2, 0.0, 0.25, 0.2, 0.14, case)
    for dx in (-0.14, 0.14):
        b.box(dx - 0.1, -0.205, 0.04, dx + 0.1, -0.2, 0.11, drive)
        b.box(dx - 0.08, -0.21, 0.07, dx + 0.08, -0.205, 0.08, slot)
        b.box(dx + 0.06, -0.21, 0.045, dx + 0.075, -0.205, 0.055, disk)
    b.cylinder(0.0, -0.2, 0.025, 0.01, 0.01, run, segments=8)
    b.box(0.2, -0.21, 0.02, 0.23, -0.2, 0.035, slot)
    return b.finish(origin)


def decor_terminal(origin):
    """Workstation on the floor: a steel stand holding a CRT, a keyboard shelf and a floppy drive, Z = 0."""
    frame = gunmetal()
    b = Builder("decor_terminal")
    b.box(-0.4, -0.3, 0.0, 0.4, 0.3, 0.7, frame)
    b.box(-0.38, -0.32, 0.4, 0.38, -0.3, 0.68, dark_steel())
    crt_monitor(b, 0.0, 0.58, -0.32, PHOSPHOR_GREEN, "crt_green", size=(0.5, 0.36), depth=0.45)
    b.box(-0.36, -0.55, 0.38, 0.36, -0.3, 0.42, frame)
    cap = material("key_cap", KEY_CAP, roughness=0.6)
    for row in range(3):
        for i in range(14):
            x = -0.33 + i * 0.05
            y = -0.52 + row * 0.07
            b.box(x, y, 0.42, x + 0.04, y + 0.055, 0.435, cap)
    drive = beige_dark()
    b.box(-0.3, -0.31, 0.1, 0.3, -0.3, 0.3, drive)
    b.box(-0.25, -0.315, 0.2, 0.0, -0.31, 0.22, dark_steel())
    b.cylinder(0.12, -0.31, 0.21, 0.012, 0.006, led(LED_RED, "led_red"), segments=8)
    return b.finish(origin)


def decor_mainframe(origin):
    """Tall mainframe cabinet: steel body, a pair of tape reels behind a window, a lamp panel, Z = 0."""
    body, panel = steel(), gunmetal()
    b = Builder("decor_mainframe")
    b.box(-0.3, -0.35, 0.0, 0.3, 0.35, 1.8, body)
    b.box(-0.26, -0.37, 1.1, 0.26, -0.35, 1.7, dark_steel())
    for x in (-0.12, 0.12):
        b.cylinder(x, -0.37, 1.45, 0.09, 0.02, beige(), segments=14)
        b.cylinder(x, -0.39, 1.45, 0.03, 0.01, steel(), segments=8)
    b.box(-0.26, -0.37, 0.7, 0.26, -0.35, 1.02, panel)
    for row in range(3):
        for i in range(8):
            mat = (led(LED_RED, "led_red"), led(LED_GREEN, "led_green"), led(LED_AMBER, "led_amber"))[(i + row) % 3]
            b.box(-0.22 + i * 0.06, -0.385, 0.76 + row * 0.08, -0.19 + i * 0.06, -0.37, 0.8 + row * 0.08, mat)
    b.box(-0.26, -0.37, 0.1, 0.26, -0.35, 0.6, panel)
    return b.finish(origin)


# ----- Windows, doors and floor -----

DOOR_WIDTH = 2.0
DOOR_HEIGHT = 2.5
LINTEL_HEIGHT = 0.5
FLOOR_THICKNESS = CELL
FRAME_BAR = 0.06
FLAT_BAND_HEIGHT = 0.18


def glass():
    mat = material("glass", (0.55, 0.8, 0.9, 0.18), roughness=0.05)
    bsdf = next(n for n in mat.node_tree.nodes if n.type == "BSDF_PRINCIPLED")
    bsdf.inputs["Alpha"].default_value = 0.18
    try:
        mat.surface_render_method = "BLENDED"
    except (AttributeError, TypeError):
        mat.blend_method = "BLEND"
    return mat


def wall_window(origin):
    """One metre of window: steel frame, thin glass pane in the middle of the wall's thickness."""
    frame, pane = gunmetal(), glass()
    b = Builder("wall_window")
    bar = FRAME_BAR
    b.box(0, 0, 0, PANEL_SIZE, WALL_THICKNESS, bar, frame)
    b.box(0, 0, PANEL_SIZE - bar, PANEL_SIZE, WALL_THICKNESS, PANEL_SIZE, frame)
    b.box(0, 0, bar, bar, WALL_THICKNESS, PANEL_SIZE - bar, frame)
    b.box(PANEL_SIZE - bar, 0, bar, PANEL_SIZE, WALL_THICKNESS, PANEL_SIZE - bar, frame)
    b.box(bar, 0.1, bar, PANEL_SIZE - bar, 0.15, PANEL_SIZE - bar, pane)
    return b.finish(origin)


def wall_door(origin):
    """Sliding door in its frame, DOOR_WIDTH by DOOR_HEIGHT: steel frame, gunmetal leaf, window slit, hazard foot."""
    frame, leaf, stripe, slit = steel(), gunmetal(), hazard(), screen_glass()
    b = Builder("wall_door")
    side = 0.12
    b.box(0, 0, 0, side, WALL_THICKNESS, DOOR_HEIGHT, frame)
    b.box(DOOR_WIDTH - side, 0, 0, DOOR_WIDTH, WALL_THICKNESS, DOOR_HEIGHT, frame)
    b.box(side, 0, DOOR_HEIGHT - side, DOOR_WIDTH - side, WALL_THICKNESS, DOOR_HEIGHT, frame)
    b.box(side, 0.05, 0, DOOR_WIDTH - side, 0.2, DOOR_HEIGHT - side, leaf)
    b.box(side, -0.005, 0, DOOR_WIDTH - side, 0.05, 0.14, stripe)
    b.box(0.75, -0.01, 1.5, 1.25, 0.05, 1.9, slit)
    b.box(DOOR_WIDTH / 2 - 0.02, -0.03, 0.9, DOOR_WIDTH / 2 + 0.02, 0.05, 1.3, stripe)
    return b.finish(origin)


def wall_cell_door(origin):
    """Armoured cell door in its frame, 1 m wide and the full 3 m high: a heavy leaf with a glass slit, a lock light and a hazard foot."""
    frame, leaf, stripe, slit = steel(), gunmetal(), hazard(), screen_glass()
    lock = led(LED_RED, "led_red")
    b = Builder("wall_cell_door")
    post = 0.08
    b.box(0, 0, 0, post, WALL_THICKNESS, 3.0, frame)
    b.box(PANEL_SIZE - post, 0, 0, PANEL_SIZE, WALL_THICKNESS, 3.0, frame)
    b.box(post, 0, DOOR_HEIGHT, PANEL_SIZE - post, WALL_THICKNESS, 3.0, frame)
    b.box(post, 0.06, 0, PANEL_SIZE - post, 0.2, DOOR_HEIGHT, leaf)
    b.box(post, -0.005, 0, PANEL_SIZE - post, 0.06, 0.12, stripe)
    b.box(0.35, -0.01, 1.55, 0.65, 0.06, 1.95, slit)
    b.box(0.74, -0.015, 1.0, 0.8, 0.06, 1.2, lock)
    return b.finish(origin)


def wall_maint(origin):
    """A wall panel with low relief: small rivets, thin overlay plates and shallow patterns."""
    b = Builder("wall_maint")
    rust = material('rust', (0.42, 0.24, 0.12, 1.0), roughness=1.0)
    b.box(0, 0, 0, PANEL_SIZE, WALL_THICKNESS, PANEL_SIZE, dark_steel())
    _plate(b, 0.1, 0.55, 0.42, 0.78, rust, 0.004)
    _plate(b, 0.55, 0.14, 0.92, 0.32, rust, 0.004)
    _plate(b, 0.6, 0.62, 0.8, 0.72, rust, 0.004)
    _rivets(b, [(x, z) for x in (0.1, 0.5, 0.9) for z in (0.06, 0.94)], steel())
    return b.finish(origin)

def wall_maint_door(origin):
    """Maintenance hatch door, DOOR_WIDTH by DOOR_HEIGHT: a dark frame, a worn leaf with a hazard top stripe and a small window."""
    frame, leaf, stripe, slit = dark_steel(), gunmetal(), hazard(), screen_glass()
    b = Builder("wall_maint_door")
    side = 0.12
    b.box(0, 0, 0, side, WALL_THICKNESS, DOOR_HEIGHT, frame)
    b.box(DOOR_WIDTH - side, 0, 0, DOOR_WIDTH, WALL_THICKNESS, DOOR_HEIGHT, frame)
    b.box(side, 0, DOOR_HEIGHT - side, DOOR_WIDTH - side, WALL_THICKNESS, DOOR_HEIGHT, frame)
    b.box(side, 0.05, 0, DOOR_WIDTH - side, 0.2, DOOR_HEIGHT - side, leaf)
    b.box(side, -0.005, DOOR_HEIGHT - 0.3, DOOR_WIDTH - side, 0.05, DOOR_HEIGHT - side, stripe)
    b.box(0.75, -0.01, 1.5, 1.25, 0.05, 1.75, slit)
    b.box(DOOR_WIDTH / 2 - 0.15, -0.03, 0.95, DOOR_WIDTH / 2 + 0.15, 0.05, 1.05, material("rust", (0.42, 0.24, 0.12, 1.0), roughness=1.0))
    return b.finish(origin)


def wall_lintel(origin):
    """Plate over a door, DOOR_WIDTH by LINTEL_HEIGHT, with a rivet line."""
    plate, rivets = steel(), gunmetal()
    b = Builder("wall_lintel")
    b.box(0, 0, 0, DOOR_WIDTH, WALL_THICKNESS, LINTEL_HEIGHT, plate)
    rivet_row(b, 0.25, (0.1, 0.5, 1.0, 1.5, 1.9), rivets)
    return b.finish(origin)


def floor_deck(origin):
    """One metre of deck plating; the top is Z = 0, the plate hangs below it."""
    plate, groove = gunmetal(), dark_steel()
    b = Builder("floor_deck")
    b.box(0, 0, -FLOOR_THICKNESS, PANEL_SIZE, PANEL_SIZE, 0.0, plate)
    for p in (0.0, 0.5):
        b.box(p, 0, 0.0, p + 0.02, PANEL_SIZE, 0.008, groove)
        b.box(0, p, 0.0, PANEL_SIZE, p + 0.02, 0.008, groove)
    return b.finish(origin)


FLOOR_TINTS = {
    "hall": (0.5, 0.52, 0.56, 1.0),
    "command": (0.25, 0.38, 0.7, 1.0),
    "medical": (0.3, 0.72, 0.55, 1.0),
    "engineering": (0.65, 0.55, 0.15, 1.0),
    "cargo": (0.62, 0.38, 0.18, 1.0),
    "science": (0.3, 0.55, 0.85, 1.0),
    "quarters": (0.8, 0.52, 0.3, 1.0),
    "service": (0.85, 0.68, 0.25, 1.0),
    "security": (0.22, 0.25, 0.34, 1.0),
    "maint": (0.18, 0.19, 0.21, 1.0),
}


def floor_variant(tint):
    def make(origin):
        """Deck plating in a department colour; the top is Z = 0."""
        plate = material(f"floor_{tint}", FLOOR_TINTS[tint], metallic=0.3, roughness=0.65)
        groove = dark_steel()
        b = Builder(f"floor_deck_{tint}")
        b.box(0, 0, -FLOOR_THICKNESS, PANEL_SIZE, PANEL_SIZE, 0.0, plate)
        for p in (0.0, 0.5):
            b.box(p, 0, 0.0, p + 0.02, PANEL_SIZE, 0.008, groove)
            b.box(0, p, 0.0, PANEL_SIZE, p + 0.02, 0.008, groove)
        return b.finish(origin)

    return make


FLOOR_VARIANTS = tuple(floor_variant(tint) for tint in FLOOR_TINTS)


def decor_pillar(origin):
    """One metre of a square steel pillar (0.5 m across), stackable; the base is Z = 0."""
    column, band = steel(), gunmetal()
    b = Builder("decor_pillar")
    b.box(-0.25, -0.25, 0.0, 0.25, 0.25, 1.0, column)
    b.box(-0.27, -0.27, 0.0, 0.27, 0.27, 0.06, band)
    b.box(-0.27, -0.27, 0.94, 0.27, 0.27, 1.0, band)
    return b.finish(origin)


def decor_captain_chair(origin):
    """Captain's chair on a swivel pedestal with armrest controls; the seat looks towards -Y, base Z = 0."""
    base, seat, arm = gunmetal(), rubber(), steel()
    b = Builder("decor_captain_chair")
    b.box(-0.3, -0.3, 0.0, 0.3, 0.3, 0.06, base)
    b.box(-0.06, -0.06, 0.06, 0.06, 0.06, 0.4, base)
    b.box(-0.3, -0.28, 0.4, 0.3, 0.28, 0.5, seat)
    b.box(-0.3, 0.2, 0.5, 0.3, 0.3, 1.1, seat)
    for x in (-0.36, 0.3):
        b.box(x, -0.25, 0.6, x + 0.06, 0.2, 0.68, arm)
        b.box(x, -0.25, 0.4, x + 0.06, -0.2, 0.6, base)
        b.box(x + 0.01, -0.22, 0.68, x + 0.05, -0.12, 0.7, led(LED_GREEN, "led_green"))
    return b.finish(origin)


# ----- Frameless windshield, seating and garden -----

WINDSHIELD_HEIGHT = 3.0
WINDSHIELD_LEAN_DEGREES = 35.0
WINDSHIELD_LEAN = WINDSHIELD_HEIGHT * math.tan(math.radians(WINDSHIELD_LEAN_DEGREES))
GLASS_THICKNESS = 0.1
FLOWER_SEED = 13
FLOWERS_PER_PLANTER = 40


def wall_glass_slope(origin):
    """One metre of frameless windshield glass, leaning into the room (its top is towards -Y)."""
    b = Builder("wall_glass_slope")
    b.slanted_slab(0.0, PANEL_SIZE, 0.0, -WINDSHIELD_LEAN, GLASS_THICKNESS, 0.0, WINDSHIELD_HEIGHT, glass())
    return b.finish(origin)


def decor_crew_chair(origin):
    """Crew seat: a swivel base, a cushioned seat and backrest; it looks towards -Y, base Z = 0."""
    base, seat = gunmetal(), material("seat_fabric", (0.2, 0.3, 0.55, 1.0), roughness=0.9)
    b = Builder("decor_crew_chair")
    b.box(-0.22, -0.22, 0.0, 0.22, 0.22, 0.05, base)
    b.box(-0.04, -0.04, 0.05, 0.04, 0.04, 0.35, base)
    b.box(-0.22, -0.22, 0.35, 0.22, 0.2, 0.43, seat)
    b.box(-0.22, 0.14, 0.43, 0.22, 0.22, 0.85, seat)
    return b.finish(origin)


def decor_bench(origin):
    """Two-metre bench: cushioned seat and back on steel legs; it looks towards -Y, floor Z = 0."""
    frame, seat = steel(), material("seat_fabric", (0.2, 0.3, 0.55, 1.0), roughness=0.9)
    b = Builder("decor_bench")
    for x in (-0.9, 0.9):
        b.box(x - 0.04, -0.22, 0.0, x + 0.04, 0.22, 0.4, frame)
        b.box(x - 0.04, 0.16, 0.4, x + 0.04, 0.22, 0.8, frame)
    b.box(-1.0, -0.22, 0.4, 1.0, 0.2, 0.47, seat)
    b.box(-1.0, 0.16, 0.47, 1.0, 0.22, 0.8, seat)
    return b.finish(origin)


def decor_planter(origin):
    """Two-metre planter box with soil and flowers; the flowers are placed from a fixed seed, floor Z = 0."""
    import random

    box, soil = steel(), material("soil", (0.22, 0.14, 0.09, 1.0), roughness=1.0)
    leaf = material("leaf", (0.15, 0.6, 0.2, 1.0), roughness=0.8)
    heads = [
        material(f"flower_{name}", color, roughness=0.7)
        for name, color in (
            ("red", (0.9, 0.12, 0.15, 1.0)),
            ("yellow", (1.0, 0.85, 0.1, 1.0)),
            ("pink", (0.95, 0.4, 0.65, 1.0)),
            ("white", (0.98, 0.98, 0.95, 1.0)),
            ("violet", (0.55, 0.3, 0.85, 1.0)),
        )
    ]
    b = Builder("decor_planter")
    b.box(-1.0, -0.35, 0.0, 1.0, 0.35, 0.4, box)
    b.box(-0.94, -0.29, 0.38, 0.94, 0.29, 0.43, soil)
    rng = random.Random(FLOWER_SEED)
    for _ in range(FLOWERS_PER_PLANTER):
        x, y = rng.uniform(-0.88, 0.88), rng.uniform(-0.23, 0.23)
        height = rng.uniform(0.15, 0.45)
        b.box(x - 0.008, y - 0.008, 0.43, x + 0.008, y + 0.008, 0.43 + height, leaf)
        b.box(x - 0.05, y - 0.02, 0.43 + height * 0.4, x + 0.05, y + 0.02, 0.43 + height * 0.4 + 0.02, leaf)
        head = heads[rng.randrange(len(heads))]
        b.box(x - 0.035, y - 0.035, 0.43 + height, x + 0.035, y + 0.035, 0.43 + height + 0.05, head)
    return b.finish(origin)


# ----- Furniture and machinery for the full station -----

REACTOR_GLOW = (0.2, 0.55, 1.0, 1.0)
SHEET_WHITE = (0.95, 0.96, 0.98, 1.0)
CRATE_BAND_HEIGHT = 0.12


def decor_bed(origin):
    """Hospital bed 0.9 x 2.0 m, head towards +Y, centred on its footprint, floor Z = 0."""
    frame, sheet = steel(), material("sheet", SHEET_WHITE, roughness=0.9)
    b = Builder("decor_bed")
    for x in (-0.42, 0.42):
        for y in (-0.95, 0.95):
            b.box(x - 0.03, y - 0.03, 0.0, x + 0.03, y + 0.03, 0.3, frame)
    b.box(-0.45, -1.0, 0.3, 0.45, 1.0, 0.38, frame)
    b.box(-0.42, -0.95, 0.38, 0.42, 0.95, 0.5, sheet)
    b.box(-0.3, 0.6, 0.5, 0.3, 0.92, 0.58, sheet)
    b.box(-0.45, 0.97, 0.3, 0.45, 1.0, 0.75, frame)
    return b.finish(origin)


def decor_crate(origin):
    """One-metre cargo crate with hazard bands, centred on its footprint, floor Z = 0."""
    body, band = beige_dark(), hazard()
    b = Builder("decor_crate")
    b.box(-0.5, -0.5, 0.0, 0.5, 0.5, 1.0, body)
    for z in (0.12, 0.76):
        b.box(-0.51, -0.51, z, 0.51, 0.51, z + CRATE_BAND_HEIGHT, band)
    return b.finish(origin)


def decor_table(origin):
    """Table 1.6 x 0.8 m on four legs, centred on its footprint, floor Z = 0."""
    top, legs = beige(), steel()
    b = Builder("decor_table")
    for x in (-0.74, 0.74):
        for y in (-0.34, 0.34):
            b.box(x - 0.03, y - 0.03, 0.0, x + 0.03, y + 0.03, 0.72, legs)
    b.box(-0.8, -0.4, 0.72, 0.8, 0.4, 0.77, top)
    return b.finish(origin)


def decor_counter(origin):
    """Two-metre counter 0.6 m deep: a steel cabinet with a dark top; its front looks towards -Y, floor Z = 0."""
    body, top = gunmetal(), dark_steel()
    b = Builder("decor_counter")
    b.box(-1.0, -0.3, 0.0, 1.0, 0.3, 0.9, body)
    b.box(-1.02, -0.32, 0.9, 1.02, 0.32, 0.95, top)
    for x in (-0.5, 0.5):
        b.box(x - 0.2, -0.305, 0.15, x + 0.2, -0.3, 0.75, gunmetal())
    return b.finish(origin)


def decor_reactor(origin):
    """Reactor core: a steel drum with glowing rings between collars, 3.2 m across and 5 m tall, floor Z = 0."""
    drum, collar = steel(), gunmetal()
    glow = material("reactor_glow", REACTOR_GLOW, emission=10.0)
    b = Builder("decor_reactor")
    b.cylinder_z(0, 0, 0.0, 1.8, 0.3, collar, segments=24)
    b.cylinder_z(0, 0, 0.3, 1.5, 4.4, drum, segments=24)
    for z in (0.9, 1.9, 2.9, 3.9):
        b.cylinder_z(0, 0, z, 1.53, 0.18, glow, segments=24)
    b.cylinder_z(0, 0, 4.7, 1.8, 0.3, collar, segments=24)
    return b.finish(origin)


# ----- Departments' fittings: lockers, sofas, sanitary, cells, cryo, shelves -----

FABRIC_RED = (0.55, 0.15, 0.18, 1.0)
CRYO_GLASS = (0.5, 0.85, 1.0, 0.25)
CRYO_GLOW = (0.3, 0.8, 1.0, 1.0)
BOOK_COLORS = ((0.7, 0.15, 0.15, 1.0), (0.15, 0.35, 0.7, 1.0), (0.85, 0.7, 0.2, 1.0), (0.2, 0.55, 0.3, 1.0))
BAR_SPACING = 0.12


def fabric_red():
    return material("fabric_red", FABRIC_RED, roughness=0.9)


def decor_locker(origin):
    """Locker 0.6 m wide, 0.5 deep, 1.9 tall; back on Y = 0, floor Z = 0."""
    body, line = gunmetal(), dark_steel()
    b = Builder("decor_locker")
    b.box(-0.3, -0.5, 0.0, 0.3, 0.0, 1.9, body)
    b.box(-0.01, -0.505, 0.05, 0.01, -0.5, 1.85, line)
    b.box(-0.2, -0.505, 1.5, -0.05, -0.5, 1.6, line)
    b.box(0.05, -0.505, 1.5, 0.2, -0.5, 1.6, line)
    return b.finish(origin)


def decor_sofa(origin):
    """Two-metre sofa, centred on its footprint, looks towards -Y, floor Z = 0."""
    b = Builder("decor_sofa")
    red = fabric_red()
    b.box(-1.0, -0.45, 0.0, 1.0, 0.4, 0.42, red)
    b.box(-1.0, 0.25, 0.42, 1.0, 0.45, 0.95, red)
    for x in (-1.0, 0.85):
        b.box(x, -0.45, 0.42, x + 0.15, 0.4, 0.65, red)
    return b.finish(origin)


def decor_toilet(origin):
    """Wall-mounted toilet with a cistern; back on Y = 0, floor Z = 0."""
    ceramic = material("ceramic", SHEET_WHITE, roughness=0.25)
    b = Builder("decor_toilet")
    b.box(-0.2, -0.1, 0.5, 0.2, 0.0, 0.95, ceramic)
    b.box(-0.2, -0.6, 0.0, 0.2, -0.1, 0.4, ceramic)
    b.box(-0.18, -0.55, 0.4, 0.18, -0.1, 0.45, steel())
    return b.finish(origin)


def decor_shower(origin):
    """Shower stall 1 x 1 m: a steel tray, a head on the back wall and a glass screen; back on Y = 0."""
    tray, head = steel(), gunmetal()
    b = Builder("decor_shower")
    b.box(-0.5, -1.0, 0.0, 0.5, 0.0, 0.06, tray)
    b.box(-0.04, -0.1, 1.9, 0.04, 0.0, 2.1, head)
    b.box(-0.12, -0.3, 2.05, 0.12, -0.1, 2.1, head)
    b.box(-0.5, -1.0, 0.06, -0.46, -0.1, 2.1, glass())
    b.box(0.46, -1.0, 0.06, 0.5, -0.1, 2.1, glass())
    return b.finish(origin)


def decor_cryo(origin):
    """Cryosleep pod, 1 x 2.2 m, centred, head towards +Y: a steel bed under a blue glass lid, floor Z = 0."""
    base = steel()
    lid = material("cryo_glass", CRYO_GLASS, roughness=0.05)
    try:
        lid.surface_render_method = "BLENDED"
    except (AttributeError, TypeError):
        lid.blend_method = "BLEND"
    glow = material("cryo_glow", CRYO_GLOW, emission=6.0)
    b = Builder("decor_cryo")
    b.box(-0.5, -1.1, 0.0, 0.5, 1.1, 0.5, base)
    b.box(-0.45, -1.05, 0.5, 0.45, 1.05, 0.9, lid)
    b.box(-0.35, 0.95, 0.9, 0.35, 1.05, 1.0, glow)
    return b.finish(origin)


def decor_shelf(origin):
    """Two-metre bookshelf, 0.35 m deep, with coloured books; back on Y = 0, floor Z = 0."""
    import random

    wood = material("shelf_frame", (0.45, 0.32, 0.2, 1.0), roughness=0.8)
    books = [material(f"book_{i}", color, roughness=0.8) for i, color in enumerate(BOOK_COLORS)]
    rng = random.Random(FLOWER_SEED)
    b = Builder("decor_shelf")
    b.box(-1.0, -0.35, 0.0, 1.0, 0.0, 0.05, wood)
    b.box(-1.0, -0.35, 1.95, 1.0, 0.0, 2.0, wood)
    for x in (-1.0, 0.98):
        b.box(x, -0.35, 0.0, x + 0.02, 0.0, 2.0, wood)
    for z in (0.5, 1.0, 1.5):
        b.box(-1.0, -0.35, z, 1.0, 0.0, z + 0.03, wood)
    for z in (0.05, 0.53, 1.03, 1.53):
        x = -0.95
        while x < 0.9:
            width = rng.uniform(0.04, 0.09)
            b.box(x, -0.3, z, x + width, -0.05, z + rng.uniform(0.28, 0.4), books[rng.randrange(len(books))])
            x += width + 0.005
    return b.finish(origin)


def decor_vending(origin):
    """Vending machine 0.9 x 0.7 x 1.8 m with a lit panel; back on Y = 0, floor Z = 0."""
    body, panel = gunmetal(), material("vend_glow", (0.4, 0.9, 1.0, 1.0), emission=3.0)
    b = Builder("decor_vending")
    b.box(-0.45, -0.7, 0.0, 0.45, 0.0, 1.8, body)
    b.box(-0.35, -0.705, 0.6, 0.35, -0.7, 1.7, panel)
    b.box(-0.2, -0.75, 0.2, 0.2, -0.7, 0.4, dark_steel())
    return b.finish(origin)


def decor_morgue(origin):
    """Morgue wall: three columns of two body drawers, 2.1 m wide, 0.8 deep; back on Y = 0, floor Z = 0."""
    body, drawer, handle = steel(), gunmetal(), dark_steel()
    b = Builder("decor_morgue")
    b.box(-1.05, -0.8, 0.0, 1.05, 0.0, 2.0, body)
    for column in range(3):
        for row in range(2):
            x, z = -0.95 + column * 0.65, 0.1 + row * 0.95
            b.box(x, -0.82, z, x + 0.6, -0.8, z + 0.85, drawer)
            b.box(x + 0.2, -0.85, z + 0.4, x + 0.4, -0.82, z + 0.45, handle)
    return b.finish(origin)


def decor_pad(origin):
    """Teleporter pad 2 x 2 m: a steel plate with a glowing ring, centred, floor Z = 0."""
    plate, ring = gunmetal(), material("pad_glow", REACTOR_GLOW, emission=6.0)
    b = Builder("decor_pad")
    b.box(-1.0, -1.0, 0.0, 1.0, 1.0, 0.1, plate)
    b.box(-0.8, -0.8, 0.1, 0.8, -0.7, 0.12, ring)
    b.box(-0.8, 0.7, 0.1, 0.8, 0.8, 0.12, ring)
    b.box(-0.8, -0.7, 0.1, -0.7, 0.7, 0.12, ring)
    b.box(0.7, -0.7, 0.1, 0.8, 0.7, 0.12, ring)
    return b.finish(origin)


def wall_bars(origin):
    """One metre of cell bars, wall-height frame: top and bottom rails and vertical bars."""
    bars = dark_steel()
    b = Builder("wall_bars")
    b.box(0, 0.08, 0, PANEL_SIZE, 0.16, 0.06, bars)
    b.box(0, 0.08, PANEL_SIZE - 0.06, PANEL_SIZE, 0.16, PANEL_SIZE, bars)
    x = BAR_SPACING / 2
    while x < PANEL_SIZE:
        b.box(x - 0.015, 0.1, 0.06, x + 0.015, 0.14, PANEL_SIZE - 0.06, bars)
        x += BAR_SPACING
    return b.finish(origin)


WALLS = (wall_plate_riveted, wall_seamed, wall_corrugated, wall_vented, wall_hazard, wall_bulkhead)
STRUCTURE = (wall_window, wall_glass_slope, wall_door, wall_maint, wall_maint_door, wall_cell_door, wall_lintel, floor_deck, decor_pillar, wall_bars)
DECOR = (
    decor_lamp_ceiling,
    decor_lamp_tube,
    decor_lamp_beacon,
    decor_switch_panel,
    decor_pipe_panel,
    decor_wall_vent_grille,
    decor_monitor_crt,
    decor_monitor_crt_wall,
    decor_keyboard,
    decor_computer,
    decor_terminal,
    decor_mainframe,
    decor_captain_chair,
    decor_crew_chair,
    decor_bench,
    decor_planter,
    decor_bed,
    decor_crate,
    decor_table,
    decor_counter,
    decor_reactor,
    decor_locker,
    decor_sofa,
    decor_toilet,
    decor_shower,
    decor_cryo,
    decor_shelf,
    decor_vending,
    decor_morgue,
    decor_pad,
)
ROW_GAP = 2.5
DECOR_ROW_Y = 4.0
STRUCTURE_ROW_Y = 8.0


def build_all():
    clear_generated()
    objects = [make((i * ROW_GAP, 0, 0)) for i, make in enumerate(WALLS)]
    objects += [make((i * ROW_GAP, DECOR_ROW_Y, 0.5)) for i, make in enumerate(DECOR)]
    objects += [make((i * ROW_GAP, STRUCTURE_ROW_Y, 0)) for i, make in enumerate(STRUCTURE + FLOOR_VARIANTS)]
    return objects


def export(objects):
    EXPORT_DIR.mkdir(parents=True, exist_ok=True)
    bpy.ops.object.select_all(action="DESELECT")
    for obj in objects:
        obj.select_set(True)
    bpy.ops.export_scene.fbx(
        filepath=str(EXPORT_DIR / EXPORT_NAME), use_selection=True, apply_unit_scale=True, axis_forward="-Z", axis_up="Y"
    )


if __name__ == "__main__":
    export(build_all())
