"""Staged build of the whole station in a live Blender (each stage is one MCP call, to stay under the call timeout).

    import sys; sys.path.insert(0, r"<repo>/blender/model_scripts")
    import station_build as b
    b.stage_geometry()   # templates, floors, walls, doors   (~40 s)
    b.stage_decor()      # per-room decor
    b.stage_extras()     # extras, derelict wing, gates, small items, signs
    b.stage_finish()     # ceilings, lamp lights, spawn points, export to blender/export

Fresh modules are loaded on the first stage only: never reload station_fill on a populated scene.
"""

import importlib
import sys

import bpy

MODULES = (
    "station_models", "station_items", "station_items2", "station_items3", "station_items4", "station_small",
    "station_scenes", "station_layout", "station_fill", "station_extras", "station_extras2", "station_wing",
    "station_finish",
)


def _load():
    for name in MODULES:
        if name in sys.modules:
            importlib.reload(sys.modules[name])
        else:
            importlib.import_module(name)
    return sys.modules["station_layout"]


def stage_geometry():
    sl = _load()
    ss = sys.modules["station_scenes"]
    templates = ss.prepare_templates()
    ss.clear_scene_collection(sl.COLLECTION)
    sl._layout = sl.Layout(sl.ROOMS, sl.LINKS, sl.EXTERIOR_DOORS)
    sl._s = ss.SceneBuilder(templates, sl.COLLECTION, visible=sl._layout.visible)
    sl.build_floors(sl._s, sl._layout)
    sl.build_walls(sl._s, sl._layout)
    sl.build_doors(sl._s, sl._layout)
    return f"geometry {sl._s.count} rooms {len(sl._layout.rooms)}"


def stage_decor():
    sl = sys.modules["station_layout"]
    for room in sl._layout.rooms:
        sl.DECOR[room.kind](sl._s, sl._layout, room)
    return f"decor {sl._s.count}"


def stage_extras():
    sl = sys.modules["station_layout"]
    for module, step in (
        ("station_extras", "add_extras"), ("station_extras2", "add_extras2"), ("station_wing", "add_wing"),
        ("station_small", "add_small"), ("station_fill", "add_signs"),
    ):
        getattr(sys.modules[module], step)(sl._s, sl._layout)
    return f"total {sl._s.count}"


def stage_finish():
    fin, layout = sys.modules["station_finish"], sys.modules["station_layout"]._layout
    counts = (fin.ceilings(layout), fin.lights(layout), fin.spawns(layout))
    return counts, fin.export_all(layout)
