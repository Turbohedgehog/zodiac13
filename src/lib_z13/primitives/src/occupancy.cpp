/*
 * Copyright 2026 Ivan Kulenko / Zodiac13
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://apache.org
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */


#include <primitives/occupancy.h>

#include <algorithm>
#include <cstdint>

namespace z13::building::primitives {

namespace {

using Vec = Eigen::Vector3<int64_t>;

constexpr int kCubeCorners = 8;

// Integer coordinates throughout: a cell touching a face without volume behind it is
// decided exactly, the same on every platform.
Vec ToInteger(const Eigen::Vector3f& vertex) {
  return vertex.array().round().cast<int64_t>();
}

struct Projection {
  int64_t low {};
  int64_t high {};
};

Projection Project(std::span<const Vec> points, const Vec& axis) {
  Projection range {.low = points.front().dot(axis), .high = points.front().dot(axis)};
  for (const Vec& point : points) {
    range.low = std::min(range.low, point.dot(axis));
    range.high = std::max(range.high, point.dot(axis));
  }
  return range;
}

// Two convex solids share volume unless some axis (a face normal, or a cross product of
// one edge of each) separates them, touching included.
bool ReachesWithVolume(const ConvexSolid& solid, const Eigen::Vector3i& cell) {
  std::vector<Vec> points;
  std::ranges::transform(solid.vertices, std::back_inserter(points), ToInteger);
  std::vector<Vec> corners;
  for (int i = 0; i < kCubeCorners; ++i) {
    corners.emplace_back(cell.x() + (i & 1), cell.y() + ((i >> 1) & 1), cell.z() + ((i >> 2) & 1));
  }

  std::vector<Vec> axes {Vec::UnitX(), Vec::UnitY(), Vec::UnitZ()};
  for (const auto& face : solid.faces) {
    const Vec origin = points[face[0]];
    axes.push_back((points[face[1]] - origin).cross(points[face[2]] - origin));
    for (size_t i = 0; i < face.size(); ++i) {
      const Vec edge = points[face[(i + 1) % face.size()]] - points[face[i]];
      for (const Vec& cube_edge : {Vec(Vec::UnitX()), Vec(Vec::UnitY()), Vec(Vec::UnitZ())}) {
        axes.push_back(edge.cross(cube_edge));
      }
    }
  }
  return std::ranges::none_of(axes, [&](const Vec& axis) {
    if (axis.isZero()) {
      return false;
    }
    const Projection a = Project(points, axis);
    const Projection b = Project(corners, axis);
    return a.high <= b.low || b.high <= a.low;
  });
}

size_t IndexIn(const CellBox& box, const Eigen::Vector3i& cell) {
  const Eigen::Vector3i local = cell - box.min;
  return static_cast<size_t>(local.x() + box.extent.x() * (local.y() + box.extent.y() * local.z()));
}

template <class Visit>
void ForEachCell(const CellBox& box, Visit visit) {
  for (int z = 0; z < box.extent.z(); ++z) {
    for (int y = 0; y < box.extent.y(); ++y) {
      for (int x = 0; x < box.extent.x(); ++x) {
        visit(Eigen::Vector3i(box.min + Eigen::Vector3i(x, y, z)));
      }
    }
  }
}

}  // namespace

bool CellShape::Contains(const Eigen::Vector3i& cell) const {
  return box.Contains(cell) && (!filled || (*filled)[IndexIn(box, cell)]);
}

bool CellShape::Overlaps(const CellBox& cells) const {
  if (!box.Overlaps(cells)) {
    return false;
  }
  if (!filled) {
    return true;
  }
  bool overlaps = false;
  ForEachCell(box.Intersection(cells), [&](const Eigen::Vector3i& cell) { overlaps = overlaps || Contains(cell); });
  return overlaps;
}

bool CellShape::Overlaps(const CellShape& other) const {
  if (!box.Overlaps(other.box)) {
    return false;
  }
  if (!filled && !other.filled) {
    return true;
  }
  bool overlaps = false;
  ForEachCell(box.Intersection(other.box), [&](const Eigen::Vector3i& cell) {
    overlaps = overlaps || (Contains(cell) && other.Contains(cell));
  });
  return overlaps;
}

std::vector<bool> FilledCells(std::span<const ConvexSolid> solids, const Eigen::Vector3i& size) {
  const CellBox frame {.min = Eigen::Vector3i::Zero(), .extent = size};
  std::vector<bool> filled(static_cast<size_t>(size.prod()));
  ForEachCell(frame, [&](const Eigen::Vector3i& cell) {
    filled[IndexIn(frame, cell)] =
        std::ranges::any_of(solids, [&cell](const ConvexSolid& solid) { return ReachesWithVolume(solid, cell); });
  });
  return filled;
}

namespace {

std::vector<bool> WorldMask(const z13::station::Block& block, const std::vector<bool>& own) {
  const CellPose pose = PoseOf(block);
  const CellBox frame {.min = Eigen::Vector3i::Zero(), .extent = block.spec.size};
  const CellBox box = OccupiedCells(block);
  std::vector<bool> mask(own.size());
  ForEachCell(frame, [&](const Eigen::Vector3i& cell) {
    // The cell's two opposite corners in the world; the lower one on each axis is its cell.
    const Eigen::Vector3i corner = pose.origin + pose.rotation * cell;
    const Eigen::Vector3i opposite = pose.origin + pose.rotation * (cell + Eigen::Vector3i::Ones());
    mask[IndexIn(box, corner.cwiseMin(opposite))] = own[IndexIn(frame, cell)];
  });
  return mask;
}

bool FillsWholeBox(OptionalPalette palette, const z13::station::Block& block) {
  const auto primitive = palette ? palette->get().Find(block.spec.type_id) : std::nullopt;
  return !primitive || primitive->get().shape.kind == ShapeKind::kBox ||
         primitive->get().shape.kind == ShapeKind::kDoorFrame ||
         (primitive->get().shape.kind == ShapeKind::kPanel && block.spec.slant == 0 && !block.spec.HasCorners() &&
          !block.spec.HasTaper());
}

// A cell's centre lies in the solid when it is on the inner side of every face; doubling
// the coordinates keeps the centre (2c + 1) and the vertices integer.
bool CentreInside(const ConvexSolid& solid, const Eigen::Vector3i& cell) {
  const Vec centre = (cell * 2 + Eigen::Vector3i::Ones()).cast<int64_t>();
  return std::ranges::all_of(solid.faces, [&](const std::vector<uint32_t>& face) {
    const Vec origin = ToInteger(solid.vertices[face[0]]) * 2;
    const Vec normal = (ToInteger(solid.vertices[face[1]]) * 2 - origin).cross(ToInteger(solid.vertices[face[2]]) * 2 - origin);
    return normal.dot(centre - origin) <= 0;
  });
}

// Greedy: from the first free cell, grow along x, then y, then z while every cell is filled and unused.
std::vector<CellBox> BoxesOf(const CellBox& box, std::vector<bool> filled) {
  std::vector<CellBox> boxes;
  const auto free_cell = [&](const Eigen::Vector3i& cell) { return filled[IndexIn(box, cell)]; };
  const auto all_free = [&](const CellBox& part) {
    bool all = true;
    ForEachCell(part, [&](const Eigen::Vector3i& cell) { all = all && box.Contains(cell) && free_cell(cell); });
    return all;
  };
  ForEachCell(box, [&](const Eigen::Vector3i& start) {
    if (!free_cell(start)) {
      return;
    }
    CellBox part {.min = start, .extent = Eigen::Vector3i::Ones()};
    for (int axis = 0; axis < kCellAxes; ++axis) {
      for (;;) {
        CellBox next = part;
        next.min[axis] += part.extent[axis];
        next.extent[axis] = 1;
        if (!all_free(next)) {
          break;
        }
        ++part.extent[axis];
      }
    }
    ForEachCell(part, [&](const Eigen::Vector3i& cell) { filled[IndexIn(box, cell)] = false; });
    boxes.push_back(part);
  });
  return boxes;
}

}  // namespace

CellShape ShapeOfBlock(const z13::station::Block& block, OptionalPalette palette) {
  CellShape shape = CellShape::Full(OccupiedCells(block));
  if (!FillsWholeBox(palette, block)) {
    shape.filled = WorldMask(block, FilledCells(BlockSolids(palette, ShapeKeyOf(block.spec)), block.spec.size));
  }
  return shape;
}

std::vector<CellBox> SealedBoxes(const z13::station::Block& block, OptionalPalette palette) {
  const CellBox box = OccupiedCells(block);
  if (FillsWholeBox(palette, block)) {
    return {box};
  }
  const std::vector<ConvexSolid> solids = BlockSolids(palette, ShapeKeyOf(block.spec));
  const CellBox frame {.min = Eigen::Vector3i::Zero(), .extent = block.spec.size};
  std::vector<bool> own(static_cast<size_t>(frame.extent.prod()));
  ForEachCell(frame, [&](const Eigen::Vector3i& cell) {
    own[IndexIn(frame, cell)] =
        std::ranges::any_of(solids, [&cell](const ConvexSolid& solid) { return CentreInside(solid, cell); });
  });
  return BoxesOf(box, WorldMask(block, own));
}

}  // namespace z13::building::primitives
