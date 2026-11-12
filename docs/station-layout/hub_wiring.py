"""The hub's power cables (blueprint.fbs, BlueprintConduit): in each grid a minimum spanning tree over the grid's
consumers, the reactor and the cells its docks open at, every edge laid as an L of straight runs (along x, then y, then
z). Overlapping legs are merged into maximal runs; a bend is the cell two runs of different axes share. The docks'
links stitch the grids' trees into one network (docs/station-systems-plan.md, "Проводка районов")."""

from math import gcd

from station_blueprint import conduit_run, split_wall

CABLE = "Cable"
CONSUMERS = ("Lamp", "Door", "Reactor")
AXES = "XYZ"
MAIN_GRID = 0


def cell_of(entry):
    return tuple(entry["cell"][axis] for axis in "xyz")


def door_cells(walls):
    """A cell in the door panel of each Door opening of the walls: the wall panels are derived when the scene is
    placed (primitives/wall_opening.cpp), so the cell is worked out from the opening: its middle step along the wall,
    at the node's level (a cell under it when the stick hangs down), whose neighbour or itself the panel's box holds."""
    cells = []
    for wall in walls:
        node_a, node_b = (tuple(wall[node][axis] for axis in "xyz") for node in ("node_a", "node_b"))
        assert wall["axis_a"] == "Z", "the wiring knows vertical walls only"
        span = (node_b[0] - node_a[0], node_b[1] - node_a[1])
        steps = gcd(abs(span[0]), abs(span[1]))
        step = (span[0] // steps, span[1] // steps)
        level = node_a[2] if wall["length_a"] > 0 else node_a[2] - 1
        for opening in wall.get("openings", []):
            if opening["kind"] == "Door":
                middle = opening["offset"] + opening["width"] // 2
                cells.append((node_a[0] + middle * step[0], node_a[1] + middle * step[1], level))
    return cells


def terminals(blocks, extra_cells):
    """The cells the cables must reach: each consumer's or reactor's lowest cell, and the extra cells (docks, doors)."""
    cells = {cell_of(entry) for entry in blocks if entry["primitive"] in CONSUMERS}
    return sorted(cells | set(extra_cells))


def spanning_edges(points):
    """Prim's tree over Manhattan distance; ties go to the earlier point, so the result doesn't depend on hashing."""
    if len(points) < 2:
        return []
    distance = lambda a, b: sum(abs(p - q) for p, q in zip(a, b))
    joined = [points[0]]
    best = {p: (distance(points[0], p), points[0]) for p in points[1:]}
    edges = []
    while best:
        nearest = min(best, key=lambda p: (best[p][0], p))
        edges.append((best[nearest][1], nearest))
        del best[nearest]
        joined.append(nearest)
        for p in best:
            d = distance(nearest, p)
            if d < best[p][0]:
                best[p] = (d, nearest)
    return edges


def legs(a, b):
    """The cells of the L from a to b, as (cell, axis): along x, then y, then z."""
    cells = set()
    here = list(a)
    for axis in range(3):
        step = 1 if b[axis] >= here[axis] else -1
        while here[axis] != b[axis]:
            cells.add((tuple(here), axis))
            here[axis] += step
        cells.add((tuple(here), axis))
    return cells


def runs_of(cells, grid):
    """Maximal straight runs of the (cell, axis) pairs."""
    lines = {}
    for cell, axis in cells:
        key = (axis, tuple(c for i, c in enumerate(cell) if i != axis))
        lines.setdefault(key, []).append(cell[axis])
    runs = []
    for (axis, others), positions in sorted(lines.items()):
        positions.sort()
        start = previous = positions[0]
        for position in positions[1:] + [None]:
            if position is not None and position == previous + 1:
                previous = position
                continue
            cell = list(others)
            cell.insert(axis, start)
            runs.append(conduit_run(grid, CABLE, cell, AXES[axis], previous - start + 1))
            if position is not None:
                start = previous = position
    return runs


def wire_grid(grid, blocks, extra_cells):
    points = terminals(blocks, extra_cells)
    cells = {(point, 2) for point in points}
    for a, b in spanning_edges(points):
        cells |= legs(a, b)
    return runs_of(cells, grid)


def wire_hub(main_blocks, main_walls, grids, docks):
    """Every grid's cables: `main_walls` are wall_segment()s, `grids` grid_entry()s, `docks` dock_entry()s."""
    def dock_cells(grid):
        cells = []
        for dock in docks:
            for side in "ab":
                if dock[f"{side}_grid"] == grid:
                    cells.append(tuple(dock[f"{side}_cell"][axis] for axis in "xyz"))
        return cells

    conduits = wire_grid(MAIN_GRID, main_blocks,
                         dock_cells(MAIN_GRID) + door_cells([piece for wall in main_walls for piece in split_wall(wall)]))
    for entry in grids:
        conduits += wire_grid(entry["id"], entry["blocks"], dock_cells(entry["id"]) + door_cells(entry["walls"]))
    return conduits
