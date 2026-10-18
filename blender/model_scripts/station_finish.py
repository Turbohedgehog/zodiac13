"""Finishing pass for the full station: ceilings, ceiling lights, spawn points and the export of the map and its library.

Run in Blender after the station is built: exec the module, then call ceilings(layout), lights(layout), spawns(layout)
and export_all(layout). The library .blend holds every template with its procedural materials, so a station can be
assembled later by linking it and placing the instances listed in the map json.
"""

import json
import math
import pathlib

import bpy

import station_models as sm

COLLECTION = "station_full"
EXPORT_DIR = pathlib.Path(__file__).resolve().parents[1] / "export"
LIBRARY_NAME = "station_library.blend"
MAP_NAME = "station_map.json"
MATERIALS_NAME = "station_materials.json"
MAP_BLEND_NAME = "station_map.blend"

CEILING_COLLECTION = "station_ceilings"
LIGHT_COLLECTION = "station_lights"
SPAWN_COLLECTION = "spawn_points"
CEILING_THICKNESS = 0.04
CEILING_COLOR = (0.86, 0.87, 0.9, 1.0)
CEILING_TEMPLATES = ("wall_", "floor_", "ceiling")

LAMPS = {
    "decor_lamp_tube": ("AREA", 0.9, 0.12, 130.0, (1.0, 0.96, 0.88)),
    "decor_lamp_beacon": ("POINT", 0.1, 0.1, 25.0, (1.0, 0.6, 0.15)),
    "decor_grow_lamp": ("AREA", 0.8, 0.5, 90.0, (0.8, 0.4, 1.0)),
    "decor_floor_lamp": ("POINT", 0.15, 0.0, 40.0, (1.0, 0.85, 0.6)),
    "decor_small_desk_lamp": ("POINT", 0.08, 0.0, 12.0, (1.0, 0.85, 0.6)),
    "decor_surgery_lamp": ("AREA", 0.6, -0.1, 300.0, (0.95, 0.98, 1.0)),
}
RUIN_SCALE = 0.35
RUIN_COLOR = (1.0, 0.45, 0.3)
INWARD_OFFSET = 0.25
DIM_KINDS = ("tunnel", "maint_room")

NO_SPAWN_KINDS = ("corridor", "tunnel", "maint_room", "airlock", "airlock_prep", "dock")
BIG_ROOM_CELLS = 120
SPAWN_CLEAR = 0.9
SPAWN_WALL_MARGIN = 1
SPAWN_APART = 3.0
SPAWN_ZONE_LOW = 1.0
FACING_Z = {"N": math.pi, "S": 0.0, "E": math.pi / 2, "W": -math.pi / 2}


def _link(name):
    old = bpy.data.collections.get(name)
    if old is not None:
        for obj in list(old.objects):
            bpy.data.objects.remove(obj, do_unlink=True)
        bpy.data.collections.remove(old)
    coll = bpy.data.collections.new(name)
    bpy.context.scene.collection.children.link(coll)
    return coll


def _is_ruin(room):
    return room.name.startswith("old_")


def ceilings(layout):
    """One thin slab per room, flat over its cells at the room's height."""
    coll = _link(CEILING_COLLECTION)
    mat = sm.material("ceiling", CEILING_COLOR, metallic=0.2, roughness=0.55)
    for room in layout.rooms:
        cells = room.cells()
        if not cells:
            continue
        top, bottom = float(room.height), room.height - CEILING_THICKNESS
        verts, faces = [], []
        for x, y in cells:
            base = len(verts)
            verts += [(x, y, bottom), (x + 1, y, bottom), (x + 1, y + 1, bottom), (x, y + 1, bottom)]
            verts += [(x, y, top), (x + 1, y, top), (x + 1, y + 1, top), (x, y + 1, top)]
            faces += [(base, base + 3, base + 2, base + 1), (base + 4, base + 5, base + 6, base + 7)]
        mesh = bpy.data.meshes.new(f"ceiling_{room.name}")
        mesh.from_pydata(verts, [], faces)
        mesh.materials.append(mat)
        mesh.update()
        obj = bpy.data.objects.new(f"ceiling_{room.name}", mesh)
        obj["room"] = room.name
        coll.objects.link(obj)
    return len(coll.objects)


def _light_data(template, ruin):
    kind, size, _, energy, color = LAMPS[template]
    name = f"{template}_light" + ("_ruin" if ruin else "")
    data = bpy.data.lights.get(name) or bpy.data.lights.new(name, kind)
    if kind == "AREA":
        data.shape = "SQUARE"
        data.size = size
    else:
        data.shadow_soft_size = size
    data.energy = energy * (RUIN_SCALE if ruin else 1.0)
    data.color = RUIN_COLOR if ruin else color
    data.use_shadow = False
    return data


def lights(layout):
    """A light at every working lamp item (tubes, beacons, grow, floor, desk and surgery lamps); red and weak in the ruins."""
    coll = _link(LIGHT_COLLECTION)
    for lamp in list(bpy.data.collections[COLLECTION].objects):
        template = lamp.name.rsplit(".", 1)[0]
        if template not in LAMPS:
            continue
        room = layout.room_at((int(lamp.location.x), int(lamp.location.y)))
        ruin = room is not None and _is_ruin(room)
        yaw = lamp.rotation_euler.z
        obj = bpy.data.objects.new(f"light_{lamp.name}", _light_data(template, ruin))
        obj.location = (
            lamp.location.x + math.sin(yaw) * INWARD_OFFSET,
            lamp.location.y - math.cos(yaw) * INWARD_OFFSET,
            lamp.location.z + LAMPS[template][2],
        )
        obj["room"] = room.name if room else ""
        obj["lamp"] = lamp.name
        coll.objects.link(obj)
    return len(coll.objects)


def _clutter():
    """Positions of the standing items, to keep spawn points off the furniture."""
    spots = {}
    for obj in bpy.data.collections[COLLECTION].objects:
        if obj.name.startswith(CEILING_TEMPLATES) or obj.location.z > SPAWN_ZONE_LOW:
            continue
        spots.setdefault((int(obj.location.x), int(obj.location.y)), []).append(obj.location)
    return spots


def _clear(spots, x, y):
    cx, cy = int(x), int(y)
    return all(
        math.hypot(p.x - x, p.y - y) >= SPAWN_CLEAR
        for dx in (-1, 0, 1)
        for dy in (-1, 0, 1)
        for p in spots.get((cx + dx, cy + dy), ())
    )


def _door_points(layout, room):
    points = []
    for edge in layout.door_edges(room):
        for start in layout.door_spans[(room.index, edge)]:
            along = start + sm.DOOR_WIDTH / 2
            points.append({"N": (along, room.y1), "S": (along, room.y0), "E": (room.x1, along), "W": (room.x0, along)}[edge])
    return points


def _facing(layout, room, x, y):
    doors = _door_points(layout, room)
    if not doors:
        return 0.0
    dx, dy = min(doors, key=lambda p: math.hypot(p[0] - x, p[1] - y))
    dx, dy = dx - x, dy - y
    return math.atan2(dx, -dy)


def spawns(layout):
    """Empties named SPAWN_<room>_<n> in every cabin and department, on a free spot, facing the nearest door."""
    coll = _link(SPAWN_COLLECTION)
    spots = _clutter()
    placed = 0
    for room in layout.rooms:
        if room.kind in NO_SPAWN_KINDS or _is_ruin(room) or room.outside:
            continue
        cells = [
            (x + 0.5, y + 0.5)
            for x, y in room.cells()
            if room.x0 + SPAWN_WALL_MARGIN <= x < room.x1 - SPAWN_WALL_MARGIN
            and room.y0 + SPAWN_WALL_MARGIN <= y < room.y1 - SPAWN_WALL_MARGIN
        ]
        cells.sort(key=lambda p: math.hypot(p[0] - room.cx, p[1] - room.cy))
        want = 2 if len(room.cells()) >= BIG_ROOM_CELLS else 1
        chosen = []
        for x, y in cells:
            if len(chosen) == want:
                break
            if not layout.point_free(room, x, y) or not _clear(spots, x, y):
                continue
            if any(math.hypot(x - ox, y - oy) < SPAWN_APART for ox, oy in chosen):
                continue
            chosen.append((x, y))
        for n, (x, y) in enumerate(chosen):
            obj = bpy.data.objects.new(f"SPAWN_{room.name}_{n}", None)
            obj.empty_display_type = "SINGLE_ARROW"
            obj.empty_display_size = 0.8
            obj.location = (x, y, 0.0)
            obj.rotation_euler = (0.0, 0.0, _facing(layout, room, x, y))
            obj["room"] = room.name
            obj["kind"] = room.kind
            coll.objects.link(obj)
            placed += 1
    return placed


def _material_record(mat):
    bsdf = next((n for n in mat.node_tree.nodes if n.type == "BSDF_PRINCIPLED"), None) if mat.use_nodes else None
    if bsdf is None:
        return {"name": mat.name}
    base = bsdf.inputs["Base Color"]
    tint = next((l.from_node for l in base.links if l.from_node.type == "MIX"), None)
    color = tint.inputs["A"].default_value if tint is not None else base.default_value
    return {
        "name": mat.name,
        "base_color": list(color),
        "metallic": bsdf.inputs["Metallic"].default_value,
        "roughness": mat.roughness,
        "emission_strength": bsdf.inputs["Emission Strength"].default_value,
        "alpha": bsdf.inputs["Alpha"].default_value,
        "procedural_wear": any(n.name == "wear_grain" for n in mat.node_tree.nodes),
    }


def export_all(layout):
    """Library .blend (templates, procedural materials), materials json, the map json and the map .blend."""
    EXPORT_DIR.mkdir(parents=True, exist_ok=True)
    templates = bpy.data.collections["templates"]
    used = {slot.material for obj in templates.objects for slot in obj.material_slots if slot.material}
    bpy.data.libraries.write(str(EXPORT_DIR / LIBRARY_NAME), {templates} | used, fake_user=True)
    materials = [_material_record(mat) for mat in sorted(used, key=lambda m: m.name)]
    (EXPORT_DIR / MATERIALS_NAME).write_text(json.dumps(materials, indent=1), encoding="utf-8")

    def pose(obj):
        return {"at": [round(v, 3) for v in obj.location], "yaw": round(obj.rotation_euler.z, 4)}

    station = {
        "library": LIBRARY_NAME,
        "materials": MATERIALS_NAME,
        "rooms": [
            {"name": r.name, "kind": r.kind, "style": r.style, "height": r.height, "box": [r.x0, r.y0, r.x1, r.y1]}
            for r in layout.rooms
        ],
        "instances": [{"template": o.name.rsplit(".", 1)[0], **pose(o)} for o in bpy.data.collections[COLLECTION].objects],
        "spawn_points": [{"room": o["room"], "kind": o["kind"], **pose(o)} for o in bpy.data.collections[SPAWN_COLLECTION].objects],
        "lights": [
            {"room": o["room"], "lamp": o["lamp"], "kind": o.data.type, **pose(o)}
            for o in bpy.data.collections[LIGHT_COLLECTION].objects
        ],
        "ceilings": [o["room"] for o in bpy.data.collections[CEILING_COLLECTION].objects],
    }
    (EXPORT_DIR / MAP_NAME).write_text(json.dumps(station, separators=(",", ":")), encoding="utf-8")
    bpy.ops.wm.save_as_mainfile(filepath=str(EXPORT_DIR / MAP_BLEND_NAME), copy=True)
    return {"library": len(used), "materials": len(materials), "instances": len(station["instances"])}
