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
#include <array>
#include <cstdint>
#include <iterator>
#include <vector>

#include <primitives/cell_test.h>
#include <primitives/placement.h>
#include <primitives/palette.h>

namespace z13::building::primitives {

namespace {

using Vec = IntegerPoint;

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
  std::vector<SolidCellTest> tests;
  std::ranges::transform(solids, std::back_inserter(tests), [](const ConvexSolid& solid) { return SolidCellTest(solid); });
  ForEachCell(frame, [&](const Eigen::Vector3i& cell) {
    filled[IndexIn(frame, cell)] =
        std::ranges::any_of(tests, [&cell](const SolidCellTest& test) { return test.ReachesWithVolume(cell); });
  });
  return filled;
}

namespace {

// The block's own cells (in its frame) laid out in the world box. A cell's world position is affine in the cell, so
// each axis moves the index by a fixed step.
template <class Cells>
Cells WorldMask(const z13::station::Block& block, const Cells& own) {
  const CellPose pose = PoseOf(block);
  const CellBox box = OccupiedCells(block);
  // Of a cell's two opposite corners in the world, the lower one on each axis is its cell.
  const Eigen::Vector3i lower = (pose.rotation * Eigen::Vector3i::Ones()).cwiseMin(0);
  const Eigen::Vector3i box_stride(1, box.extent.x(), box.extent.x() * box.extent.y());
  const Eigen::Vector3i step = pose.rotation.transpose() * box_stride;
  const int first = box_stride.dot(pose.origin + lower - box.min);
  const Eigen::Vector3i size = block.spec.size;
  Cells mask(own.size());
  size_t from = 0;
  for (int z = 0; z < size.z(); ++z) {
    for (int y = 0; y < size.y(); ++y) {
      const int row = first + (step.y() * y) + (step.z() * z);
      for (int x = 0; x < size.x(); ++x) {
        mask[static_cast<size_t>(row + (step.x() * x))] = own[from++];
      }
    }
  }
  return mask;
}

bool FillsWholeBox(OptionalPalette palette, const z13::station::Block& block) {
  const auto primitive = palette ? palette->get().Find(block.spec.type_id) : std::nullopt;
  return !primitive || primitive->get().shape.kind == ShapeKind::kBox ||
         primitive->get().shape.kind == ShapeKind::kDoorFrame ||
         (primitive->get().shape.kind == ShapeKind::kPanel && block.spec.slant == 0 && !block.spec.HasCorners() &&
          !block.spec.HasFaceCuts());
}

// A face's plane with the coordinates doubled, so the centre of a cell (2c + 1) and the vertices stay integer.
struct DoubledFace {
  Vec normal;
  int64_t limit {};  // normal . origin: a centre is inside when normal . centre does not pass it
};

std::vector<DoubledFace> DoubledFacesOf(const ConvexSolid& solid) {
  std::vector<DoubledFace> faces;
  std::ranges::transform(solid.faces, std::back_inserter(faces), [&solid](const std::vector<uint32_t>& face) {
    const Vec origin = ToInteger(solid.vertices[face[0]]) * 2;
    const Vec normal =
        (ToInteger(solid.vertices[face[1]]) * 2 - origin).cross(ToInteger(solid.vertices[face[2]]) * 2 - origin);
    return DoubledFace {.normal = normal, .limit = normal.dot(origin)};
  });
  return faces;
}

// A cell's centre lies in the solid when it is on the inner side of every face.
bool CentreInside(const std::vector<DoubledFace>& faces, const Eigen::Vector3i& cell) {
  const Vec centre = (cell * 2 + Eigen::Vector3i::Ones()).cast<int64_t>();
  return std::ranges::all_of(faces, [&centre](const DoubledFace& face) {
    return face.normal.dot(centre) <= face.limit;
  });
}

// Greedy: from the first free cell, grow along x, then y, then z while every cell is filled and unused. Plain ints:
// this runs over every cell of every block of a scene.
std::vector<CellBox> BoxesOf(const CellBox& box, std::vector<uint8_t> filled) {
  std::vector<CellBox> boxes;
  const std::array<int, kCellAxes> size = {box.extent.x(), box.extent.y(), box.extent.z()};
  const std::array<size_t, kCellAxes> stride = {1, static_cast<size_t>(size[0]), static_cast<size_t>(size[0] * size[1])};
  using Corner = std::array<int, kCellAxes>;
  const auto slab_free = [&](const Corner& low, const Corner& high) {
    for (int z = low[2]; z < high[2]; ++z) {
      for (int y = low[1]; y < high[1]; ++y) {
        const size_t row = (stride[1] * static_cast<size_t>(y)) + (stride[2] * static_cast<size_t>(z));
        for (int x = low[0]; x < high[0]; ++x) {
          if (!filled[row + static_cast<size_t>(x)]) {
            return false;
          }
        }
      }
    }
    return true;
  };
  for (int z = 0; z < size[2]; ++z) {
    for (int y = 0; y < size[1]; ++y) {
      for (int x = 0; x < size[0]; ++x) {
        if (!filled[static_cast<size_t>(x) + (stride[1] * static_cast<size_t>(y)) + (stride[2] * static_cast<size_t>(z))]) {
          continue;
        }
        const Corner start = {x, y, z};
        Corner end = {x + 1, y + 1, z + 1};
        for (int axis = 0; axis < kCellAxes; ++axis) {
          while (end[axis] < size[axis]) {
            Corner low = start;
            Corner high = end;
            low[axis] = end[axis];
            high[axis] = end[axis] + 1;
            if (!slab_free(low, high)) {
              break;
            }
            ++end[axis];
          }
        }
        for (int cz = start[2]; cz < end[2]; ++cz) {
          for (int cy = start[1]; cy < end[1]; ++cy) {
            for (int cx = start[0]; cx < end[0]; ++cx) {
              filled[static_cast<size_t>(cx) + (stride[1] * static_cast<size_t>(cy)) + (stride[2] * static_cast<size_t>(cz))] = 0;
            }
          }
        }
        boxes.push_back({.min = box.min + Eigen::Vector3i(start[0], start[1], start[2]),
                         .extent = Eigen::Vector3i(end[0] - x, end[1] - y, end[2] - z)});
      }
    }
  }
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
  std::vector<std::vector<DoubledFace>> solid_faces;
  std::ranges::transform(solids, std::back_inserter(solid_faces), DoubledFacesOf);
  // Bytes, not vector<bool>: this runs over every cell of every block of a scene.
  std::vector<uint8_t> own(static_cast<size_t>(frame.extent.prod()));
  size_t at = 0;
  for (int z = 0; z < frame.extent.z(); ++z) {
    for (int y = 0; y < frame.extent.y(); ++y) {
      for (int x = 0; x < frame.extent.x(); ++x) {
        const Eigen::Vector3i cell(x, y, z);
        own[at++] = std::ranges::any_of(solid_faces, [&cell](const auto& faces) { return CentreInside(faces, cell); });
      }
    }
  }
  return BoxesOf(box, WorldMask(block, own));
}

}  // namespace z13::building::primitives
