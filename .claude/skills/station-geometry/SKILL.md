---
name: station-geometry
description: Rules and checks for drawing station blueprints with wall segments (the hub's oblique districts, generate_hub.py): how wall ends meet, what makes chamfers, burrs and teeth, the panel size limit and the sealing tests. Use when adding or changing walls, floors or ceilings in a generator, or when the user reports chamfers, burrs, teeth, notches or gaps in a hub wall ("фаска", "заусенец", "зубцы", "засечки", "щель", "утечка").
---

# Drawing walls in a blueprint

Generator: `docs/station-layout/generate_hub.py` over `station_blueprint.py`; the plan is `docs/hub-station-plan.md`.

## Rules that keep the joints clean

- A wall segment's line is its inner face and its thickness goes outward along the minor axis of the plan (`plan_axes`); 2 cells hold air back at any slope while the thickness lies along the minor axis.
- Walls that meet share a node at the crossing of their inner-face lines (`meet`), with both bodies outward: the game stitches them into one mitred panel. A wall that ends against another wall's face without a shared node leaves a chamfer on one side and a burr on the other (a cut end is square to the major axis, a slanted face isn't). Run the checker below after every change.
- A T-joint at a wall's mid-line (a neck into a hall, a stub into the atrium) is solved by ending the branch walls on the other wall's far face and ending that wall's pieces on the branch walls' nodes, never by butting ends.
- Openings (`wall_opening`) take whole steps of the wall's lattice step (gcd of its span): a side with one step takes none. Doors on a slanted wall: the branch walls start where their inner lines cross the wall's outer face.
- A panel's box is at most 64 cells per axis (`max_block_cells`): `split_wall` cuts longer ones, with a half-limit margin for seams.
- Floors and ceilings: a strip along a lattice step for a long hall; where a strip can't follow (a neck meeting a hall at an angle) a mask of cells that strictly overlap the outline minus `ring_cells()`. Blocks and panels must not share cells ("cells are taken"); panels may overlap panels. A mask hugs the wall faces (outer lines), so no gap is left under a wall.
- Pitfall: a Python heredoc turns `\n` inside C++ string literals into real newlines; edit C++ with the Edit tool.

## Checks

1. `PYTHONUTF8=1 python .claude/skills/station-geometry/scripts/check_wall_joints.py` lists every free wall end within a wall's thickness of another wall (exit 1 when any). A new district must add none; the ones from the east spine's stub and rooms and the arrivals neck's start at the atrium side are known (the notches of the plan's "Открыто").
2. A sealed-room test per district: both cells non-vacuum rooms, different, joined by a portal (`HubFreightHangarTest`, `HubArrivalsHangarTest` in `room_system_test.cpp`).
3. Look at it: `python z13.py --release -- --station-scene hub`; move the spawns into the district in `bin-release/assets/station/blueprints/hub.json` (a copy, not tracked) and check each joint from inside, at the floor and under the ceiling; the plan lists what was only seen in Blender.

## When the user reports a defect

Name the class first (chamfer/burr = a joint without a shared node; teeth = a stepped edge where a strip meets a slanted wall or the F4 overlay's outline; gap/leak = a mask or strip short of a wall's outer face), fix the generator, add the check that would have caught it here, then regenerate (`generate_hub.py`), rebuild Release and run the hub tests.
