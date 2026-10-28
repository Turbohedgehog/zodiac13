"""The hub station as the game builds it: the blocks of assets/station/blueprints/hub.json and the wall segments of
docs/station-layout/generate_hub.py, one mesh per material. Metres, z up, the plan's y mirrored (the plan's y
grows south). Run in Blender: `import hub_blueprint as h; h.build()`.
"""

import math
import sys
from pathlib import Path

import bmesh
import bpy

LAYOUT = Path(__file__).resolve().parents[2] / "docs" / "station-layout"
BLUEPRINT = Path(__file__).resolve().parents[2] / "assets" / "station" / "blueprints" / "hub.json"
CELL_M = 0.25
COLLECTION = "hub_blueprint"

# Material -> (rgba, emission strength).
MATERIALS = {
    "Floor": ((0.42, 0.44, 0.48, 1.0), 0.0),
    "Wall": ((0.80, 0.82, 0.86, 1.0), 0.0),
    "Window": ((0.30, 0.60, 0.90, 0.35), 0.0),
    "Door": ((0.95, 0.50, 0.12, 1.0), 0.0),
    "Panel": ((0.72, 0.76, 0.82, 1.0), 0.0),
    "Slope": ((0.40, 0.75, 0.45, 1.0), 0.0),
    "Lamp": ((1.0, 0.95, 0.6, 1.0), 4.0),
    "SpawnPoint": ((0.95, 0.2, 0.4, 1.0), 0.0),
    "Counter": ((0.6, 0.5, 0.4, 1.0), 0.0),
    "SpineWall": ((0.15, 0.75, 0.70, 1.0), 0.0),
    "SpineFloor": ((0.10, 0.45, 0.42, 1.0), 0.0),
    "Ring": ((0.55, 0.50, 0.40, 1.0), 0.0),
}


def _material(name):
    existing = bpy.data.materials.get(f"hub_{name}")
    if existing:
        return existing
    rgba, emission = MATERIALS[name]
    material = bpy.data.materials.new(f"hub_{name}")
    material.diffuse_color = rgba
    material.use_nodes = True
    shader = next(n for n in material.node_tree.nodes if n.type == "BSDF_PRINCIPLED")
    shader.inputs["Base Color"].default_value = rgba
    if emission:
        shader.inputs["Emission Color"].default_value = rgba
        shader.inputs["Emission Strength"].default_value = emission
    if rgba[3] < 1.0:
        shader.inputs["Alpha"].default_value = rgba[3]
    return material


def _point(p):
    return (p[0] * CELL_M, -p[1] * CELL_M, p[2] * CELL_M)


def _solid(bm, points):
    verts = [bm.verts.new(_point(p)) for p in points]
    bmesh.ops.convex_hull(bm, input=verts)


def _box_points(low, high):
    return [(x, y, z) for x in (low[0], high[0]) for y in (low[1], high[1]) for z in (low[2], high[2])]


def _prism_points(entry, orientations):
    """The 8 corners of a Panel block: its section in the local XZ plane, `slant` cells of rise, extruded along y."""
    size = [entry["size"][axis] for axis in "xyz"]
    cell = [entry["cell"][axis] for axis in "xyz"]
    name = entry.get("orientation") or "FacePosXUpPosZ"
    columns = dict(orientations)[name]
    rotated = [sum(columns[i][row] * size[i] for i in range(3)) for row in range(3)]
    origin = [cell[row] - min(rotated[row], 0) for row in range(3)]
    run, depth, slant = size[0], size[2] - entry.get("slant", 0), entry.get("slant", 0)
    section = ((0, 0), (run, slant), (run, size[2]), (0, depth))
    points = []
    for y in (0, size[1]):
        for x, z in section:
            local = (x, y, z)
            points.append(tuple(origin[row] + sum(columns[i][row] * local[i] for i in range(3)) for row in range(3)))
    return points


def build():
    sys.path.insert(0, str(LAYOUT))
    import json
    import importlib
    import station_blueprint as sb
    import generate_hub as gh
    importlib.reload(sb)
    importlib.reload(gh)

    blueprint = json.loads(BLUEPRINT.read_text(encoding="ascii"))
    for collection in bpy.data.collections:
        if collection.name == COLLECTION:
            for obj in list(collection.objects):
                bpy.data.objects.remove(obj, do_unlink=True)
            bpy.data.collections.remove(collection)
    collection = bpy.data.collections.new(COLLECTION)
    bpy.context.scene.collection.children.link(collection)
    for other in bpy.context.scene.collection.children:
        if other.name != COLLECTION:
            other.hide_viewport = True
            other.hide_render = True
    for obj in bpy.context.scene.collection.objects:
        obj.hide_set(True)

    meshes = {}

    def bm_of(material):
        if material not in meshes:
            meshes[material] = bmesh.new()
        return meshes[material]

    for entry in blueprint["blocks"]:
        primitive = entry["primitive"]
        material = primitive if primitive in MATERIALS else "Panel"
        if primitive == "Panel":
            _solid(bm_of(material), _prism_points(entry, sb.ORIENTATIONS))
        else:
            low, high = sb.occupied_box(entry)
            _solid(bm_of(material), _box_points(low, high))

    def wall_pieces(wall):
        a = tuple(wall["node_a"][k] for k in "xyz")
        b = tuple(wall["node_b"][k] for k in "xyz")
        thickness, stick = wall["thickness"], wall["length_a"]
        openings = wall.get("openings", [])
        if not openings:
            return [(a, b, thickness, stick)]
        steps = math.gcd(abs(b[0] - a[0]), abs(b[1] - a[1]))
        step = ((b[0] - a[0]) // steps, (b[1] - a[1]) // steps)
        at = lambda k, z=0: (a[0] + k * step[0], a[1] + k * step[1], a[2] + z)
        pieces = []
        cursor = 0
        for opening in sorted(openings, key=lambda o: o["offset"]):
            first, last = opening["offset"], opening["offset"] + opening["width"]
            pieces.append((at(cursor), at(first), thickness, stick))
            pieces.append((at(first, opening["height"]), at(last, opening["height"]), thickness,
                           stick - opening["height"]))
            cursor = last
        pieces.append((at(cursor), at(steps), thickness, stick))
        return pieces

    def add_walls(walls, wall_material, floor_material):
        for wall in walls:
            for a, b, thickness, stick in wall_pieces(wall):
                entry = sb.wall_panel_block(a, b, thickness, stick)
                material = floor_material if stick == sb.SLAB_CELLS else wall_material
                _solid(bm_of(material), _prism_points(entry, sb.ORIENTATIONS))

    add_walls(gh.ring_walls(), "Panel", "Ring")
    add_walls(gh.spine_b_walls(), "SpineWall", "SpineFloor")

    for material, bm in meshes.items():
        bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
        mesh = bpy.data.meshes.new(f"hub_{material}")
        bm.to_mesh(mesh)
        bm.free()
        mesh.materials.append(_material(material))
        obj = bpy.data.objects.new(f"hub_{material}", mesh)
        collection.objects.link(obj)
    return {name: len(m.vertices) for name, m in ((o.name, o.data) for o in collection.objects)}


def draw_dump(path):
    """The wall panels the game derives (seams and openings), as written by the DumpPanels test:
    `Z13_DUMP_PANELS=<path> z13_test_runner --gtest_filter=*DumpPanels`. One mesh, `hub_true_walls`."""
    import json
    name = "hub_true_walls"
    if name in bpy.data.objects:
        bpy.data.objects.remove(bpy.data.objects[name], do_unlink=True)
    bm = bmesh.new()
    for solid in json.loads(Path(path).read_text(encoding="ascii")):
        _solid(bm, solid)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    mesh = bpy.data.meshes.new(name)
    bm.to_mesh(mesh)
    bm.free()
    mesh.materials.append(_material("SpineWall"))
    collection = bpy.data.collections.get(COLLECTION) or bpy.context.scene.collection
    collection.objects.link(bpy.data.objects.new(name, mesh))
