"""The hub's tunnels between the tech trunk and the districts' lower decks, in the main grid's cells: a path of
axis-aligned legs, each with its own width, drawn as two walls along it (wall segments sharing the corners' nodes), a
floor and a ceiling where the deck above has none, and lamps; one end meets the trunk's wall, the other a cap the caller
draws (a slanted wall on the district's near plane)."""

from typing import NamedTuple

from station_blueprint import FLOOR, LAMP, LAMP_CELLS, block, mask_blocks, wall_opening, wall_segment


class Leg(NamedTuple):
    """The interior's extent either side of the path's line: left of the direction of travel, and right."""
    left: int
    right: int


def _sign(value):
    return (value > 0) - (value < 0)


def _directions(path):
    for a, b in zip(path, path[1:]):
        assert (a[0] == b[0]) != (a[1] == b[1]), "a leg is along one axis"
    return [(_sign(b[0] - a[0]), _sign(b[1] - a[1])) for a, b in zip(path, path[1:])]


def _left(direction):
    return (-direction[1], direction[0])


def _offset(path, legs, side):
    """The polyline `side` (+1 left, -1 right) of the path, with the legs' own offsets: a corner is where the two
    offset lines meet."""
    directions = _directions(path)
    reach = [leg.left if side > 0 else leg.right for leg in legs]
    points = []
    for i, point in enumerate(path):
        x, y = point
        for leg in (i - 1, i):
            if 0 <= leg < len(directions):
                normal = _left(directions[leg])
                x += side * normal[0] * reach[leg]
                y += side * normal[1] * reach[leg]
        points.append((x, y))
    return points


def tunnel_walls(path, legs, z, height, thickness, ends=None, bulkheads=(), door=None):
    """The two walls along the path. `ends`: the last node of the left wall and of the right wall, in place of the
    ones the path's end gives (where a slanted cap closes the tunnel). `bulkheads`: (leg, distance from the leg's start)
    of a cross wall with `door` (offset, width, height in the wall's steps and cells) where the side walls end on its
    nodes."""
    left, right = _offset(path, legs, +1), _offset(path, legs, -1)
    if ends:
        left[-1], right[-1] = ends
    directions = _directions(path)
    walls = []
    for line, sign in ((left, thickness), (right, -thickness)):
        for leg, (a, b) in enumerate(zip(line, line[1:])):
            if a == b:
                continue
            cuts = sorted(distance for at, distance in bulkheads if at == leg)
            d = directions[leg]
            points = [a] + [(a[0] + d[0] * distance, a[1] + d[1] * distance) for distance in cuts] + [b]
            walls += [wall_segment((*p, z), (*q, z), sign, height) for p, q in zip(points, points[1:])]
    for leg, distance in bulkheads:
        d = directions[leg]
        ends_of = [(line[leg][0] + d[0] * distance, line[leg][1] + d[1] * distance) for line in (left, right)]
        walls.append(wall_segment((*ends_of[1], z), (*ends_of[0], z), thickness, height,
                                  openings=[wall_opening(*door)] if door else ()))
    return walls


def _cells(box):
    (x0, y0), (x1, y1) = box
    return {(x, y) for x in range(min(x0, x1), max(x0, x1)) for y in range(min(y0, y1), max(y0, y1))}


def footprint(path, legs, thickness):
    """The cells under the tunnel and its walls: a box per leg and one per corner."""
    directions = _directions(path)
    left, right = _offset(path, legs, +1), _offset(path, legs, -1)
    cells = set()
    for i, (a, b) in enumerate(zip(path, path[1:])):
        normal = _left(directions[i])
        leg = legs[i]
        corners = [(a[0] + normal[0] * (leg.left + thickness), a[1] + normal[1] * (leg.left + thickness)),
                   (b[0] - normal[0] * (leg.right + thickness), b[1] - normal[1] * (leg.right + thickness))]
        cells |= _cells(corners)
    for i in range(1, len(path) - 1):
        grow = lambda c, sx, sy: (c[0] + sx * thickness, c[1] + sy * thickness)
        a, b = left[i], right[i]
        sx, sy = _sign(a[0] - b[0]), _sign(a[1] - b[1])
        cells |= _cells((grow(b, -sx, -sy), grow(a, sx, sy)))
    return cells


def slab_blocks(cells, z, skip=frozenset()):
    """A slab of Floor over the `cells` that are not in `skip`."""
    cells = cells - skip
    if not cells:
        return []
    xs, ys = [c[0] for c in cells], [c[1] for c in cells]
    left, top = min(xs), min(ys)
    mask = [[False] * (max(xs) + 1 - left) for _ in range(max(ys) + 1 - top)]
    for x, y in cells:
        mask[y - top][x - left] = True
    blocks = mask_blocks(mask, z, FLOOR)
    for entry in blocks:
        entry["cell"]["x"] += left
        entry["cell"]["y"] += top
    return blocks


def lamps(path, z, spacing, extra=()):
    """A lamp every `spacing` cells along the path's middle, from half a spacing in, and one at each (leg, distance)
    of `extra`, so a room cut off by a bulkhead is never left dark."""
    blocks = []
    for leg, distance in extra:
        a, b = path[leg], path[leg + 1]
        x, y = a[0] + _sign(b[0] - a[0]) * distance, a[1] + _sign(b[1] - a[1]) * distance
        blocks.append(block(LAMP, (x - 1, y - 1, z), (LAMP_CELLS, LAMP_CELLS, 1)))
    for a, b in zip(path, path[1:]):
        length = abs(b[0] - a[0]) + abs(b[1] - a[1])
        d = (_sign(b[0] - a[0]), _sign(b[1] - a[1]))
        along = spacing // 2
        while along < length:
            x, y = a[0] + d[0] * along, a[1] + d[1] * along
            blocks.append(block(LAMP, (x - 1, y - 1, z), (LAMP_CELLS, LAMP_CELLS, 1)))
            along += spacing
    return blocks
