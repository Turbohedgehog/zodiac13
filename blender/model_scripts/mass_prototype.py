"""Massing of the multi-level station: a bent spine around a tall atrium, districts on different
levels and at angles, ramp cores (1:4) and the service network.

Metres, z up. Run in Blender: `import mass_prototype as m; m.build()`.
Public ramps are green, service ramps and tunnels dark, shafts (ladder/lift) yellow.
"""

import math

import bpy
import bmesh
from mathutils import Matrix, Vector
from mathutils.bvhtree import BVHTree

LEVEL_M = 3.0
RAMP_RUN_PER_RISE = 4.0  # Slope.max_run_per_rise
RAMP_RUN_M = LEVEL_M * RAMP_RUN_PER_RISE
RAMP_THICKNESS_M = 0.4

# Role -> (rgb, alpha).
COLORS = {
    "spine": ((0.55, 0.85, 0.95), 0.3),
    "atrium": ((0.6, 0.9, 1.0), 0.2),
    "science": ((0.88, 0.9, 0.95), 0.55),
    "habitat": ((0.85, 0.8, 0.7), 0.55),
    "greenhouse": ((0.4, 0.8, 0.45), 0.4),
    "medical": ((0.95, 0.75, 0.8), 0.55),
    "command": ((0.3, 0.5, 1.0), 0.85),
    "hangar": ((0.5, 0.55, 0.6), 0.55),
    "reactor": ((0.6, 0.2, 0.1), 0.9),
    "tech": ((0.12, 0.1, 0.1), 1.0),
    "tech_hall": ((0.3, 0.22, 0.2), 1.0),
    "freight": ((0.9, 0.55, 0.1), 1.0),
    "shaft": ((0.95, 0.75, 0.1), 1.0),
    "ramp": ((0.2, 0.85, 0.4), 1.0),
    "ramp_tech": ((1.0, 0.4, 0.0), 1.0),
    "abandoned": ((0.35, 0.3, 0.28), 0.85),
    "security": ((0.75, 0.35, 0.35), 0.6),
    "command_wing": ((0.3, 0.5, 1.0), 0.8),
    "arrivals": ((0.5, 0.8, 0.95), 0.6),
    "departure": ((0.95, 0.65, 0.3), 0.6),
    "mining_dock": ((0.65, 0.6, 0.5), 0.6),
    "dock_port": ((0.2, 0.85, 0.9), 1.0),
    "gallery": ((0.85, 0.92, 1.0), 0.85),
    "portal": ((0.2, 1.0, 0.5), 0.5),
    "agora": ((0.9, 0.3, 0.3), 0.9),
    "podium": ((1.0, 0.85, 0.2), 1.0),
}

SPINE_B_ANGLE = 22.0


def _rot(angle_deg, x, y):
    a = math.radians(angle_deg)
    return (x * math.cos(a) - y * math.sin(a), x * math.sin(a) + y * math.cos(a))


def _spine_b(x, y):
    """World point of the east spine segment's local (x, y)."""
    return _rot(SPINE_B_ANGLE, x, y)


SPINE_BOXES = [
    ("spine_a", (-45, -9, -8, 9, 3, 15), (0, 0), 0),
    ("spine_b", (8, -9, 50, 9, 3, 15), (0, 0), SPINE_B_ANGLE),
]
ATRIUM = {"center": (0, 0), "radius": 18.0, "z": (3.0, 30.0)}
REACTOR = {"center": (-55, 0), "radius": 10.0, "z": (-9.0, 24.0)}
# The bridge: a gallery inside the atrium's top level and a glazed prow out into space.
BRIDGE_HINGE = (0, 14)
BRIDGE_ANGLE = 90.0
BRIDGE_FOOTPRINT = [(-6, -8), (14, -10), (30, -4), (34, 0), (30, 4), (14, 10), (-6, 8)]
BRIDGE_Z = (24.0, 30.0)

# Districts in their own frame (x leaves the spine, y across): hinge, angle, convex parts
# (footprint polygon, z0, z1), ramp-core levels and its first floor, service tunnel length.
# A part is a prism, so ends can be slanted; parts of one district step down away from the spine.
DISTRICTS = {
    "science": {
        "hinge": (-34, 9), "angle": 118,
        "parts": [
            ([(0, -12), (36, -8), (42, 6), (0, 12)], 9, 18),
            ([(4, -9), (28, -7), (34, 5), (4, 9)], 18, 27),
            ([(41, -2), (48, -2), (48, 2), (41, 2)], 12, 15),  # neck to the toxins test chamber
            ([(48, -6), (58, -6), (58, 6), (48, 6)], 12, 18),
        ],
        "core": (6, 9.0), "tunnel": 40,
    },
    "greenhouse": {
        "hinge": (-28, -9), "angle": -118,
        "parts": [([(0, -9), (26, -5), (26, 5), (0, 9)], 6, 15)],
        "core": (3, 6.0), "tunnel": 24,
    },
    "habitat": {
        "hinge": _spine_b(26, -9), "angle": -35,
        "parts": [
            ([(0, -8), (22, -8), (22, 8), (0, 8)], 3, 18),
            ([(22, -8), (40, -9), (40, 7), (22, 8)], 3, 12),
            ([(40, -9), (54, -5), (54, 3), (40, 7)], 3, 9),
        ],
        "abandoned": [2],
        "core": (5, 3.0), "tunnel": 50,
    },
    "medical": {
        "hinge": _spine_b(32, 9), "angle": SPINE_B_ANGLE + 72,
        "parts": [([(0, -11), (24, -12), (28, 6), (0, 10)], 6, 15), ([(2, -8), (20, -8), (22, 4), (2, 6)], 15, 21)],
        "core": (5, 6.0), "tunnel": 26,
    },
    "security": {
        "hinge": (0, -18), "angle": -90,
        "parts": [
            ([(0, -12), (18, -12), (22, -8), (22, 8), (18, 12), (0, 12)], 3, 12),
            ([(22, -8), (32, -7), (32, 6), (22, 8)], 3, 9),
        ],
        "core": (3, 3.0), "tunnel": 30,
    },
    "command_wing": {
        "hinge": (0, 18), "angle": 90,
        "parts": [([(0, -8), (18, -8), (22, -4), (22, 4), (18, 8), (0, 8)], 18, 24)],
        "core": (2, 18.0), "tunnel": None,
    },
    # The old wing, sealed behind a wide corridor (Octant's old_* rooms): three decks and a collapsed upper part.
    "old_wing": {
        "hinge": (-65, 0), "angle": 190,
        "parts": [
            ([(0, -3), (10, -3), (10, 3), (0, 3)], 0, 6),
            ([(10, -11), (34, -14), (50, -8), (50, 8), (34, 14), (10, 11)], 0, 9),
            ([(14, -6), (30, -6), (30, 8), (14, 8)], 9, 15),
        ],
        "abandoned": [0, 1, 2],
        "core": (3, 0.0), "core_origin": (12, 0), "tunnel": 42,
    },
    # Docks: arrivals near the agora, departure behind the security wing, mining beside the cargo hangar.
    "arrivals": {
        "hinge": (14, -12), "angle": -50,
        "parts": [
            ([(0, -3), (14, -3), (14, 3), (0, 3)], 3, 6),
            ([(14, -11), (34, -13), (40, -5), (40, 5), (34, 13), (14, 11)], 3, 9),
        ],
        "core": (2, 3.0), "core_origin": (16, 0), "tunnel": 36,
    },
    "departure": {
        "hinge": (0, -50), "angle": -90,
        "parts": [([(0, -14), (24, -14), (30, -6), (30, 6), (24, 14), (0, 14)], 3, 9)],
        "core": (2, 3.0), "tunnel": 26,
    },
    "mining_dock": {
        "hinge": (_spine_b(50, 0)[0] + _rot(SPINE_B_ANGLE - 12, 16, 18)[0],
                  _spine_b(50, 0)[1] + _rot(SPINE_B_ANGLE - 12, 16, 18)[1]),
        "angle": SPINE_B_ANGLE - 12 + 90,
        "parts": [([(0, -11), (26, -11), (32, -4), (32, 4), (26, 11), (0, 11)], 0, 9)],
        "core": (3, 0.0), "tunnel": 30,
    },
    "hangar": {
        "hinge": _spine_b(50, 0), "angle": SPINE_B_ANGLE - 12,
        "parts": [
            ([(0, -14), (30, -18), (40, -6), (40, 6), (30, 18), (0, 14)], 0, 9),
            ([(4, -10), (24, -12), (24, 12), (4, 10)], 9, 12),
        ],
        "core": (3, 0.0), "tunnel": 38,
    },
}
DISTRICT_CORE_ORIGIN = (3, 0)

# Pitch of a district about its hinge, degrees: positive raises the far end. Neighbours on LINKS alternate;
# the command wing and the bridge stay level.
TILT_DEG = {
    "hangar": -1, "medical": -1, "mining_dock": 1, "habitat": 1, "science": -1, "arrivals": -1,
    "security": 1, "departure": -1, "greenhouse": -1, "old_wing": 1,
}
# Docking ports ride with the district they hang on.
PORT_HOSTS = {"port_medical": "medical", "port_cargo": "hangar", "port_departure": "departure",
              "port_old_wing": "old_wing"}

# Small-craft docking ports: an airlock sleeve and a clamp ring at its tip. Host frame (hinge, angle),
# point in that frame, direction relative to it, sleeve length, floor z.
PORT_SLEEVE_W = 5.0
PORT_CLAMP_LEN = 3.0
PORT_CLAMP_W = 8.0
PORT_H = 3.0
_REACTOR_FRAME = (REACTOR["center"], 0.0)
_BRIDGE_FRAME = (BRIDGE_HINGE, BRIDGE_ANGLE)


def _frame(name):
    spec = DISTRICTS[name]
    return spec["hinge"], spec["angle"]


DOCK_PORTS = [
    ("port_bridge", _BRIDGE_FRAME, (24, 8.5), 90, 6, 24.0),
    ("port_medical", _frame("medical"), (27.5, 0), 0, 6, 9.0),
    ("port_cargo", _frame("hangar"), (22, -17.4), -90, 8, 0.0),
    ("port_reactor_s", _REACTOR_FRAME, (0, -10), -90, 6, 0.0),
    ("port_reactor_n", _REACTOR_FRAME, (-5, 8.66), 120, 6, 0.0),
    ("port_departure", _frame("departure"), (14, 14.05), 90, 6, 3.0),
    ("port_old_wing", _frame("old_wing"), (24, -14), -90, 6, 0.0),
]


def _port_parts(host_hinge, host_angle, local, rel_angle, length):
    """Hinge, angle and the two convex parts (sleeve, clamp) in the port's own frame."""
    dx, dy = _rot(host_angle, *local)
    hinge = (host_hinge[0] + dx, host_hinge[1] + dy)
    half, clamp_half = PORT_SLEEVE_W / 2, PORT_CLAMP_W / 2
    sleeve = [(0, -half), (length, -half), (length, half), (0, half)]
    clamp = [(length, -clamp_half), (length + PORT_CLAMP_LEN, -half), (length + PORT_CLAMP_LEN, half),
             (length, clamp_half)]
    return hinge, host_angle + rel_angle, [sleeve, clamp]

TUNNEL_W, TUNNEL_H = 3.0, 2.5
BYPASS_W = 2.2
TUNNEL_ROOT_X = -3.0  # tunnels start inside the host's volume so they meet its risers and base
# Districts on the spine whose tunnel runs above the spine base and needs a root riser down to it.
ROOT_RISERS = ("science", "greenhouse", "medical")
TECH_TOUCH_M = 0.3
# Service links between neighbouring districts: straight corridors between their risers.
LINKS = [
    ("science", "medical"), ("medical", "hangar"), ("hangar", "mining_dock"), ("hangar", "habitat"),
    ("habitat", "arrivals"), ("arrivals", "security"), ("security", "departure"),
    ("security", "greenhouse"), ("greenhouse", "old_wing"),
]
RING_TUNNEL = {"radius": 24.0, "segments": 12}
# Bypass corridors inside a district, along its lowest floor: name -> (offset from the axis, length).
BYPASS = {
    "science": (10, 36), "greenhouse": (6, 22), "habitat": (6, 36), "medical": (8, 22),
    "security": (10, 26), "hangar": (12, 30), "old_wing": (9, 38),
}
# Big technical halls under the trunk tunnels: name, local box (x0, y0, x1, y1), hinge, angle.
HALL_Z = (-8.0, -2.5)
HALLS = [
    ("hall_atmos", (-44, -10, -22, 10), (0, 0), 0),
    ("hall_power", (14, -10, 40, 10), (0, 0), SPINE_B_ANGLE),
    ("hall_cargo", (4, -12, 34, 12), _spine_b(50, 0), SPINE_B_ANGLE - 12),
    ("hall_telecom", (2, -8, 24, 8), (0, -18), -90),
]
FREIGHT_SIZE = 4.0
FREIGHT_LIFTS = [("freight_hangar", (8, -10), _spine_b(50, 0), SPINE_B_ANGLE - 12, 12.0)]
TRUNK_TUNNELS = [
    ("tunnel_trunk_a", (-65, 0, 0), 0.0, (0, 0), 0),
    ("tunnel_trunk_b", (0, 50, 0), 0.0, (0, 0), SPINE_B_ANGLE),  # ends where the hangar tunnel takes over
]
SHAFT_SIZE = 2.5
SHAFT_BOTTOM = -3.0
SHAFT_TOP = 30.0
# The captain's lift ends at the bridge floor.
SHAFTS = [(*(-42, 11), SHAFT_TOP), (*(-42, -11), SHAFT_TOP), (0, 22, BRIDGE_Z[0]), (-16, -22, SHAFT_TOP),
          (*_spine_b(46, 7.5), SHAFT_TOP), (*_spine_b(46, -7.5), SHAFT_TOP)]

# Ramp cores (a stack of 12 m flights turning back and forth in a 16x7 m footprint):
# name, role, hinge, angle, local origin (x, y), levels, first floor z.
RAMP_CORE_LEN_M = RAMP_RUN_M + 4.0
RAMP_LANE_W = 3.0
RAMP_LANE_OFFSET = 2.0
TECH_RAMP_CORES = [
    ("ramps_tech_a", "ramp_tech", (-30, 0), 0, (0, -5), 1, 0.0),
    ("ramps_tech_b", "ramp_tech", _spine_b(25, 0), SPINE_B_ANGLE, (0, -5), 1, 0.0),
]
# Its top end meets the bridge floor at the north.
SPIRAL = {"r_in": 14.0, "r_out": 17.5, "z": (3.0, 24.0), "start_deg": 145.0, "step_m": 1.0}

# The agora: the atrium floor inside the ramp ring, with a podium; the forecourt hangs before the bridge.
AGORA_RADIUS = 14.0
AGORA_FLOOR_Z = 3.0
PODIUM = {"center": (0, 0), "radius": 3.0, "z": (3.0, 4.2)}
FORECOURT_FOOTPRINT = [(-16, -14), (-6, -10), (-6, 10), (-16, 14)]
FORECOURT_Z = (23.6, 24.0)

# Galleries: rings inside the ramp ring, one per floor above the agora, open in the middle.
GALLERY_LEVELS_Z = (6.0, 9.0, 12.0, 15.0, 18.0, 21.0)
GALLERY_R_IN = 10.5
GALLERY_R_OUT = 14.0
GALLERY_THICKNESS_M = 0.4
GALLERY_SEGMENTS = 48

# Entrances in the atrium wall: name, direction in degrees, floor z, width, height.
# The spine mouths open every spine floor (L1-L4) onto the galleries; the others are single doorways.
ENTRANCES = [
    ("entrance_west_spine", 180.0, 3.0, 9.0, 12.0),
    ("entrance_east_spine", SPINE_B_ANGLE, 3.0, 9.0, 12.0),
    ("entrance_security", 270.0, 3.0, 8.0, 6.0),
    ("entrance_arrivals", math.degrees(math.atan2(-12, 14)), 3.0, 6.0, 6.0),
    ("entrance_command", 90.0, 18.0, 8.0, 6.0),
]
ENTRANCE_DEPTH_M = 6.0


def _material(role):
    rgb, alpha = COLORS[role]
    mat = bpy.data.materials.get(f"mass_{role}") or bpy.data.materials.new(f"mass_{role}")
    mat.use_nodes = True
    bsdf = next(n for n in mat.node_tree.nodes if n.type == "BSDF_PRINCIPLED")
    bsdf.inputs["Base Color"].default_value = (*rgb, 1)
    bsdf.inputs["Alpha"].default_value = alpha
    mat.diffuse_color = (*rgb, alpha)
    return mat


def _link(obj, collection):
    coll = bpy.data.collections.get(collection) or bpy.data.collections.new(collection)
    if coll.name not in bpy.context.scene.collection.children:
        bpy.context.scene.collection.children.link(coll)
    for old in obj.users_collection:
        old.objects.unlink(obj)
    coll.objects.link(obj)


def _mesh_object(name, verts, faces, role, collection, location=(0, 0, 0), rotation_z=0.0):
    mesh = bpy.data.meshes.new(name)
    mesh.from_pydata(verts, [], faces)
    mesh.update()
    bm = bmesh.new()
    bm.from_mesh(mesh)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    bm.to_mesh(mesh)
    bm.free()
    obj = bpy.data.objects.new(name, mesh)
    obj.location = location
    obj.rotation_euler = (0, 0, rotation_z)
    obj.data.materials.append(_material(role))
    bpy.context.scene.collection.objects.link(obj)
    _link(obj, collection)
    return obj


def _box(name, role, collection, box, hinge, angle_deg):
    x0, y0, x1, y1, z0, z1 = box
    dx, dy = _rot(angle_deg, (x0 + x1) / 2, (y0 + y1) / 2)
    bpy.ops.mesh.primitive_cube_add(size=1, location=(hinge[0] + dx, hinge[1] + dy, (z0 + z1) / 2))
    obj = bpy.context.active_object
    obj.name = name
    obj.scale = (x1 - x0, y1 - y0, z1 - z0)
    obj.rotation_euler = (0, 0, math.radians(angle_deg))
    obj.data.materials.append(_material(role))
    _link(obj, collection)


def _cylinder(name, role, collection, spec):
    z0, z1 = spec["z"]
    bpy.ops.mesh.primitive_cylinder_add(
        vertices=32, radius=spec["radius"], depth=z1 - z0, location=(*spec["center"], (z0 + z1) / 2))
    obj = bpy.context.active_object
    obj.name = name
    obj.data.materials.append(_material(role))
    _link(obj, collection)


def _flight(name, role, location, rotation_z, rise_from_z):
    """A straight 1:4 flight rising LEVEL_M over RAMP_RUN_M along local +x, centred on y."""
    half = RAMP_LANE_W / 2
    profile = [(0, 0), (RAMP_RUN_M, LEVEL_M), (RAMP_RUN_M, LEVEL_M - RAMP_THICKNESS_M), (0, -RAMP_THICKNESS_M)]
    verts = [(u, side * half, z) for side in (-1, 1) for u, z in profile]
    faces = [(0, 1, 2, 3), (7, 6, 5, 4), (0, 4, 5, 1), (1, 5, 6, 2), (2, 6, 7, 3), (3, 7, 4, 0)]
    _mesh_object(name, verts, faces, role, "ramps", (*location, rise_from_z), rotation_z)


def _ramp_core(name, role, hinge, angle_deg, origin, levels, z0):
    for k in range(levels):
        forward = k % 2 == 0
        lane_y = -RAMP_LANE_OFFSET if forward else RAMP_LANE_OFFSET
        start_x = origin[0] if forward else origin[0] + RAMP_CORE_LEN_M - 2.0
        dx, dy = _rot(angle_deg, start_x, origin[1] + lane_y)
        turn = 0.0 if forward else 180.0
        _flight(f"{name}_{k}", role, (hinge[0] + dx, hinge[1] + dy), math.radians(angle_deg + turn),
                z0 + k * LEVEL_M)


def _spiral(name):
    r_in, r_out = SPIRAL["r_in"], SPIRAL["r_out"]
    z_start, z_end = SPIRAL["z"]
    mid_r = (r_in + r_out) / 2
    total_run = (z_end - z_start) * RAMP_RUN_PER_RISE
    steps = int(total_run / SPIRAL["step_m"])
    phi0 = math.radians(SPIRAL["start_deg"])
    verts, faces = [], []
    for i in range(steps + 1):
        s = i * total_run / steps
        phi = phi0 + s / mid_r
        z = z_start + s / RAMP_RUN_PER_RISE
        c, sn = math.cos(phi), math.sin(phi)
        verts += [(r_in * c, r_in * sn, z), (r_out * c, r_out * sn, z),
                  (r_out * c, r_out * sn, z - RAMP_THICKNESS_M), (r_in * c, r_in * sn, z - RAMP_THICKNESS_M)]
    for i in range(steps):
        a, b = 4 * i, 4 * (i + 1)
        faces += [(a, a + 1, b + 1, b), (a + 1, a + 2, b + 2, b + 1), (a + 2, a + 3, b + 3, b + 2),
                  (a + 3, a, b, b + 3)]
    last = 4 * steps
    faces += [(0, 3, 2, 1), (last, last + 1, last + 2, last + 3)]
    _mesh_object(name, verts, faces, "ramp", "ramps")


def _annulus(name, role, collection, r_in, r_out, z0, z1):
    count = GALLERY_SEGMENTS
    verts = []
    for z in (z0, z1):
        for r in (r_in, r_out):
            verts += [(r * math.cos(2 * math.pi * i / count), r * math.sin(2 * math.pi * i / count), z)
                      for i in range(count)]
    inner_bottom, outer_bottom, inner_top, outer_top = (k * count for k in range(4))
    faces = []
    for i in range(count):
        j = (i + 1) % count
        faces += [(inner_top + i, outer_top + i, outer_top + j, inner_top + j),
                  (outer_bottom + j, outer_bottom + i, inner_bottom + i, inner_bottom + j),
                  (outer_bottom + i, outer_bottom + j, outer_top + j, outer_top + i),
                  (inner_bottom + j, inner_bottom + i, inner_top + i, inner_top + j)]
    _mesh_object(name, verts, faces, role, collection)


def _clear():
    bpy.data.batch_remove([o for o in bpy.data.objects if o.type == "MESH"])
    bpy.data.batch_remove(list(bpy.data.collections))


def _world_polygon(footprint, hinge, angle_deg):
    return [(hinge[0] + dx, hinge[1] + dy) for dx, dy in (_rot(angle_deg, x, y) for x, y in footprint)]


def _prism(name, role, collection, footprint, z0, z1, hinge, angle_deg):
    ring = _world_polygon(footprint, hinge, angle_deg)
    count = len(ring)
    verts = [(x, y, z) for z in (z0, z1) for x, y in ring]
    faces = [tuple(reversed(range(count))), tuple(range(count, 2 * count))]
    faces += [(i, (i + 1) % count, count + (i + 1) % count, count + i) for i in range(count)]
    _mesh_object(name, verts, faces, role, collection)


TOUCH_TOLERANCE_M = 1e-6


def _separated(a, b):
    """Separating-axis test for two convex polygons."""
    for poly in (a, b):
        for i, (x0, y0) in enumerate(poly):
            x1, y1 = poly[(i + 1) % len(poly)]
            nx, ny = y1 - y0, x0 - x1
            pa = [nx * x + ny * y for x, y in a]
            pb = [nx * x + ny * y for x, y in b]
            if max(pa) <= min(pb) + TOUCH_TOLERANCE_M or max(pb) <= min(pa) + TOUCH_TOLERANCE_M:
                return True
    return False


def overlaps():
    """Pairs of parts that share floor area and height: districts, the atrium and the reactor."""
    solids = []
    for name, spec in DISTRICTS.items():
        for i, (footprint, z0, z1) in enumerate(spec["parts"]):
            solids.append((f"{name}.{i}", _world_polygon(footprint, spec["hinge"], spec["angle"]), z0, z1))
    for name, spec in (("atrium", ATRIUM), ("reactor", REACTOR)):
        cx, cy = spec["center"]
        ring = [(cx + spec["radius"] * math.cos(t * math.pi / 8), cy + spec["radius"] * math.sin(t * math.pi / 8))
                for t in range(16)]
        solids.append((name, ring, *spec["z"]))
    for name, host, local, rel, length, z0 in DOCK_PORTS:
        hinge, angle, parts = _port_parts(*host, local, rel, length)
        for i, footprint in enumerate(parts):
            solids.append((f"{name}.{i}", _world_polygon(footprint, hinge, angle), z0, z0 + PORT_H))
    for name, (x0, y0, x1, y1), hinge, angle in HALLS:
        solids.append((name, _world_polygon([(x0, y0), (x1, y0), (x1, y1), (x0, y1)], hinge, angle), *HALL_Z))
    solids.append(("bridge", _world_polygon(BRIDGE_FOOTPRINT, BRIDGE_HINGE, BRIDGE_ANGLE), *BRIDGE_Z))
    found = []
    for i, (na, pa, a0, a1) in enumerate(solids):
        for nb, pb, b0, b1 in solids[i + 1:]:
            if {na, nb} == {"bridge", "atrium"}:
                continue  # the bridge enters the atrium on purpose
            if a0 < b1 and b0 < a1 and not _separated(pa, pb):
                found.append((na, nb))
    return found


LINK_SAMPLES = 40


def _tunnel_segment(spec):
    """World ends of a district's service tunnel, the heights its riser spans, and the tunnel's low z."""
    floor_z = min(z0 for _, z0, _ in spec["parts"])
    top = min(z1 for _, z0, z1 in spec["parts"] if z0 == floor_z)
    near = _rot(spec["angle"], 2, 0)
    far = _rot(spec["angle"], spec["tunnel"] - SHAFT_SIZE / 2, 0)
    hinge = spec["hinge"]
    return ((hinge[0] + near[0], hinge[1] + near[1]), (hinge[0] + far[0], hinge[1] + far[1])), floor_z - TUNNEL_H, top


def _closest_points(first, second):
    """The pair of points, one on each segment, that are nearest to each other."""
    def along(segment):
        (x0, y0), (x1, y1) = segment
        return [(x0 + (x1 - x0) * i / LINK_SAMPLES, y0 + (y1 - y0) * i / LINK_SAMPLES) for i in range(LINK_SAMPLES + 1)]
    return min(((a, b) for a in along(first) for b in along(second)), key=lambda pair: math.dist(*pair))


def _link_corridor(first, second):
    """Both ends, the floor and the lowest tunnel z of a corridor between two districts' tunnels, or None."""
    (seg_a, lo_a, hi_a), (seg_b, lo_b, hi_b) = _tunnel_segment(DISTRICTS[first]), _tunnel_segment(DISTRICTS[second])
    floor_z = max(lo_a, lo_b)
    if floor_z + TUNNEL_H > min(hi_a, hi_b):
        return None
    a, b = _closest_points(seg_a, seg_b)
    return a, b, floor_z, min(lo_a, lo_b)


def tech_components():
    """Groups of touching objects in the 'tech' collection, the biggest first."""
    bpy.context.view_layer.update()  # fresh objects have no world matrix until then
    objs = list(bpy.data.collections["tech"].objects)
    trees = []
    for obj in objs:
        verts = [obj.matrix_world @ v.co for v in obj.data.vertices]
        trees.append((BVHTree.FromPolygons([tuple(v) for v in verts], [tuple(p.vertices) for p in obj.data.polygons]),
                      verts))

    def touches(i, j):
        tree_i, verts_i = trees[i]
        tree_j, verts_j = trees[j]
        if tree_i.overlap(tree_j):
            return True
        return (any(tree_j.find_nearest(v)[3] <= TECH_TOUCH_M for v in verts_i)
                or any(tree_i.find_nearest(v)[3] <= TECH_TOUCH_M for v in verts_j))

    parent = list(range(len(objs)))

    def root(i):
        while parent[i] != i:
            parent[i] = parent[parent[i]]
            i = parent[i]
        return i

    for i in range(len(objs)):
        for j in range(i + 1, len(objs)):
            if root(i) != root(j) and touches(i, j):
                parent[root(j)] = root(i)
    groups = {}
    for i, obj in enumerate(objs):
        groups.setdefault(root(i), []).append(obj.name)
    return sorted(groups.values(), key=len, reverse=True)


def _district_of(obj_name):
    """The district a mass object belongs to, or None (spine, atrium, links, shared tunnels)."""
    for district in DISTRICTS:
        prefixes = (f"{district}_", f"ramps_{district}_", f"tunnel_{district}", f"bypass_{district}_",
                    f"bypass_cross_{district}_", f"root_riser_{district}", f"riser_{district}")
        if obj_name.startswith(prefixes):
            return district
    for port, district in PORT_HOSTS.items():
        if obj_name.startswith(f"{port}_"):
            return district
    return None


def apply_tilts(amplitude_deg):
    """Pitches every tilted district about its hinge by TILT_DEG * amplitude_deg."""
    bpy.context.view_layer.update()
    for obj in bpy.data.objects:
        district = _district_of(obj.name)
        sign = TILT_DEG.get(district)
        if not sign:
            continue
        spec = DISTRICTS[district]
        angle = math.radians(spec["angle"])
        axis = Vector((-math.sin(angle), math.cos(angle), 0.0))
        pivot = Vector((*spec["hinge"], 0.0))
        rotation = Matrix.Rotation(-math.radians(sign * amplitude_deg), 4, axis)
        obj.matrix_world = Matrix.Translation(pivot) @ rotation @ Matrix.Translation(-pivot) @ obj.matrix_world


def build(tilt_deg=0.0):
    _clear()
    for name, box, hinge, angle in SPINE_BOXES:
        _box(name, "spine", "front", box, hinge, angle)
    _box("spine_base", "tech", "tech", (-45, -9, 0, 9, 0, 3), (0, 0), 0)
    _box("spine_base_b", "tech", "tech", (0, -9, 50, 9, 0, 3), (0, 0), SPINE_B_ANGLE)
    _prism("bridge", "command", "front", BRIDGE_FOOTPRINT, *BRIDGE_Z, BRIDGE_HINGE, BRIDGE_ANGLE)
    _cylinder("atrium", "atrium", "front", ATRIUM)
    _cylinder("reactor", "reactor", "front", REACTOR)
    _cylinder("agora", "agora", "front", {"center": (0, 0), "radius": AGORA_RADIUS,
                                          "z": (AGORA_FLOOR_Z, AGORA_FLOOR_Z + 0.3)})
    _cylinder("podium", "podium", "front", PODIUM)
    for z in GALLERY_LEVELS_Z:
        _annulus(f"gallery_{int(z)}", "gallery", "front", GALLERY_R_IN, GALLERY_R_OUT, z - GALLERY_THICKNESS_M, z)
    for name, direction, z0, width, height in ENTRANCES:
        rim = _rot(direction, ATRIUM["radius"], 0)
        _box(name, "portal", "front", (-ENTRANCE_DEPTH_M / 2, -width / 2, ENTRANCE_DEPTH_M / 2, width / 2,
                                       z0, z0 + height), rim, direction)
    _prism("forecourt", "command", "front", FORECOURT_FOOTPRINT, *FORECOURT_Z, BRIDGE_HINGE, BRIDGE_ANGLE)
    half = TUNNEL_W / 2
    for name, spec in DISTRICTS.items():
        for i, (footprint, z0, z1) in enumerate(spec["parts"]):
            role = "abandoned" if i in spec.get("abandoned", ()) else name
            _prism(f"{name}_{i}", role, "front", footprint, z0, z1, spec["hinge"], spec["angle"])
        floor_z = min(z0 for _, z0, _ in spec["parts"])
        if spec["tunnel"]:
            _box(f"tunnel_{name}", "tech", "tech",
                 (TUNNEL_ROOT_X, -half, spec["tunnel"], half, floor_z - TUNNEL_H, floor_z), spec["hinge"], spec["angle"])
        levels, core_z0 = spec["core"]
        _ramp_core(f"ramps_{name}", "ramp", spec["hinge"], spec["angle"], spec.get("core_origin", DISTRICT_CORE_ORIGIN),
                   levels, core_z0)
    for name, host, local, rel, length, z0 in DOCK_PORTS:
        hinge, angle, parts = _port_parts(*host, local, rel, length)
        for i, footprint in enumerate(parts):
            _prism(f"{name}_{i}", "dock_port", "front", footprint, z0, z0 + PORT_H, hinge, angle)
    for name, (x0, y0, x1, y1), hinge, angle in HALLS:
        _box(name, "tech_hall", "tech", (x0, y0, x1, y1, *HALL_Z), hinge, angle)
    for name, (x, y), hinge, angle, top in FREIGHT_LIFTS:
        h = FREIGHT_SIZE / 2
        _box(name, "freight", "tech", (x - h, y - h, x + h, y + h, HALL_Z[0], top), hinge, angle)
    for name, spec in DISTRICTS.items():
        floor_z = min(z0 for _, z0, _ in spec["parts"])
        if name in BYPASS:
            offset, length = BYPASS[name]
            for side in (-1, 1):
                _box(f"bypass_{name}_{side}", "tech", "tech",
                     (2, side * offset - BYPASS_W / 2, 2 + length, side * offset + BYPASS_W / 2,
                      floor_z, floor_z + TUNNEL_H), spec["hinge"], spec["angle"])
            for k, x in enumerate((2, 2 + length - BYPASS_W)):
                _box(f"bypass_cross_{name}_{k}", "tech", "tech",
                     (x, -offset, x + BYPASS_W, offset, floor_z - TUNNEL_H, floor_z + TUNNEL_H),
                     spec["hinge"], spec["angle"])
        if name in ROOT_RISERS:
            h = SHAFT_SIZE / 2
            _box(f"root_riser_{name}", "shaft", "tech",
                 (TUNNEL_ROOT_X, -h, TUNNEL_ROOT_X + SHAFT_SIZE, h, 0.0, floor_z), spec["hinge"], spec["angle"])
        if spec["tunnel"]:
            h = SHAFT_SIZE / 2
            lowest_top = min(z1 for _, z0, z1 in spec["parts"] if z0 == floor_z)
            _box(f"riser_{name}", "shaft", "tech",
                 (spec["tunnel"] - SHAFT_SIZE, -h, spec["tunnel"], h, floor_z - TUNNEL_H, lowest_top),
                 spec["hinge"], spec["angle"])
    for first, second in LINKS:
        link = _link_corridor(first, second)
        if link is None:
            print(f"no common height for the link {first} - {second}")
            continue
        (x0, y0), (x1, y1), floor_z, low_z = link
        _box(f"link_{first}_{second}", "tech", "tech",
             (0, -TUNNEL_W / 2, math.hypot(x1 - x0, y1 - y0), TUNNEL_W / 2, floor_z, floor_z + TUNNEL_H),
             (x0, y0), math.degrees(math.atan2(y1 - y0, x1 - x0)))
        h = SHAFT_SIZE / 2
        for k, (x, y) in enumerate(((x0, y0), (x1, y1))):
            _box(f"link_{first}_{second}_riser_{k}", "shaft", "tech", (x - h, y - h, x + h, y + h, low_z, floor_z + TUNNEL_H),
                 (0, 0), 0)
    ring_r, segments = RING_TUNNEL["radius"], RING_TUNNEL["segments"]
    seg_len = 2 * math.pi * ring_r / segments
    for i in range(segments):
        a = 360.0 * i / segments
        cx, cy = _rot(a, ring_r, 0)
        _box(f"tunnel_ring_{i}", "tech", "tech",
             (-seg_len / 2, -TUNNEL_W / 2, seg_len / 2, TUNNEL_W / 2, -TUNNEL_H, 0), (cx, cy), a + 90)
    for name, (x0, x1, y), floor_z, hinge, angle in TRUNK_TUNNELS:
        _box(name, "tech", "tech", (x0, y - half, x1, y + half, floor_z - TUNNEL_H, floor_z), hinge, angle)
    for i, (x, y, top) in enumerate(SHAFTS):
        h = SHAFT_SIZE / 2
        _box(f"shaft_{i}", "shaft", "tech", (x - h, y - h, x + h, y + h, SHAFT_BOTTOM, top), (0, 0), 0)
    for core in TECH_RAMP_CORES:
        _ramp_core(*core)
    _spiral("ramp_atrium_spiral")
    if tilt_deg:
        apply_tilts(tilt_deg)
