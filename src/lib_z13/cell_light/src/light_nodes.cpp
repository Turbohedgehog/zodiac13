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

#include "light_nodes.h"

#include <algorithm>
#include <array>
#include <limits>

namespace z13::station::light {

namespace {

using z13::building::primitives::CellBox;

static_assert(kCubeSides * kPiecesPerNode <= std::numeric_limits<uint32_t>::digits);

// A free cell no piece has taken yet, while a cube is labelled.
constexpr uint8_t kUnlabelled = 0xFE;
constexpr int kCubeCellCount = kNodeCells * kNodeCells * kNodeCells;
constexpr float kHalfCell = 0.5f;
constexpr int kAxes = 3;

bool Empty(const CellBox& box) {
  return (box.extent.array() <= 0).any();
}

// A cell of a cube from its index there (x first, then y, then z), along one axis.
int CubeCellAxis(int cell, int axis) {
  const int step = axis == 0 ? 1 : axis == 1 ? kNodeCells : kNodeCells * kNodeCells;
  return (cell / step) % kNodeCells;
}

}  // namespace

size_t LightLayout::CubeIndex(const Eigen::Vector3i& cube) const {
  return static_cast<size_t>(((static_cast<int64_t>(cube.z()) * cubes.y()) + cube.y()) * cubes.x() + cube.x());
}

size_t LightLayout::CellIndex(const Eigen::Vector3i& cell) const {
  const Eigen::Vector3i local = cell - origin;
  const Eigen::Vector3i size = Cells();
  return static_cast<size_t>(((static_cast<int64_t>(local.z()) * size.y()) + local.y()) * size.x() + local.x());
}

// The loops here and in the floods work on plain numbers: Eigen's expressions cost many times
// more in Debug.
LightNodes::LightNodes(const LightLayout& layout)
    : layout_(layout),
      labels_(layout.CellCount(), kUnlabelled),
      pieces_(layout.CubeCount()),
      kinds_(layout.CubeCount(), CubeKind::kMixed),
      centres_(layout.CubeCount() * kPiecesPerNode, Eigen::Vector3f::Zero()),
      links_(layout.CubeCount() * kPiecesPerNode) {
  const Eigen::Vector3i cells = layout.Cells();
  cell_strides_ = {1, cells.x(), static_cast<int64_t>(cells.x()) * cells.y()};
  cube_strides_ = {1, layout.cubes.x(), static_cast<int64_t>(layout.cubes.x()) * layout.cubes.y()};
  for (int cell = 0; cell < kCubeCellCount; ++cell) {
    cube_cell_offsets_[static_cast<size_t>(cell)] = CubeCellAxis(cell, 0) + (CubeCellAxis(cell, 1) * cell_strides_[1]) +
                                                    (CubeCellAxis(cell, 2) * cell_strides_[2]);
  }
}

void LightNodes::CopyOpaque(const LightNodes& other) {
  const CellBox shared = other.layout_.CellBounds().Intersection(layout_.CellBounds());
  for (int z = 0; z < shared.extent.z(); ++z) {
    for (int y = 0; y < shared.extent.y(); ++y) {
      const Eigen::Vector3i row = shared.min + Eigen::Vector3i(0, y, z);
      const size_t from = other.layout_.CellIndex(row);
      const size_t to = layout_.CellIndex(row);
      for (int x = 0; x < shared.extent.x(); ++x) {
        if (other.labels_[from + x] == kOpaqueLabel) {
          labels_[to + x] = kOpaqueLabel;
        }
      }
    }
  }
}

void LightNodes::SetOpaque(const CellBox& cells, bool opaque) {
  const CellBox clipped = cells.Intersection(layout_.CellBounds());
  if (Empty(clipped)) {
    return;
  }
  const uint8_t label = opaque ? kOpaqueLabel : kUnlabelled;
  for (int z = 0; z < clipped.extent.z(); ++z) {
    for (int y = 0; y < clipped.extent.y(); ++y) {
      const size_t row = layout_.CellIndex(clipped.min + Eigen::Vector3i(0, y, z));
      std::fill_n(labels_.begin() + static_cast<std::ptrdiff_t>(row), clipped.extent.x(), label);
    }
  }
}

void LightNodes::Relabel(const CellBox& cubes) {
  const CellBox all {.min = Eigen::Vector3i::Zero(), .extent = layout_.cubes};
  const CellBox labelled = cubes.Intersection(all);
  if (Empty(labelled)) {
    return;
  }
  const CellBox linked = labelled.Grown(Eigen::Vector3i::Ones()).Intersection(all);
  for (const bool linking : {false, true}) {
    const CellBox& box = linking ? linked : labelled;
    const std::array low {box.min.x(), box.min.y(), box.min.z()};
    const std::array high {box.End().x(), box.End().y(), box.End().z()};
    for (int z = low[2]; z < high[2]; ++z) {
      for (int y = low[1]; y < high[1]; ++y) {
        for (int x = low[0]; x < high[0]; ++x) {
          const std::array cube {x, y, z};
          if (linking) {
            LinkCube(cube);
          } else {
            LabelCube(cube);
          }
        }
      }
    }
  }
}

int64_t LightNodes::CubeIndex(const std::array<int, 3>& cube) const {
  return cube[0] + (cube[1] * cube_strides_[1]) + (cube[2] * cube_strides_[2]);
}

int64_t LightNodes::CornerCell(const std::array<int, 3>& cube) const {
  return kNodeCells * (cube[0] + (cube[1] * cell_strides_[1]) + (cube[2] * cell_strides_[2]));
}

void LightNodes::LabelCube(const std::array<int, 3>& cube) {
  const int64_t corner = CornerCell(cube);
  const auto cube_index = static_cast<size_t>(CubeIndex(cube));
  int opaque = 0;
  for (int cell = 0; cell < kCubeCellCount; ++cell) {
    uint8_t& label = labels_[static_cast<size_t>(corner + CubeCellOffset(cell))];
    if (label == kOpaqueLabel) {
      ++opaque;
    } else {
      label = kUnlabelled;
    }
  }
  // Most cubes are all air or all metal: one piece or none, without searching.
  if (opaque == 0 || opaque == kCubeCellCount) {
    const bool free = opaque == 0;
    for (int cell = 0; free && cell < kCubeCellCount; ++cell) {
      labels_[static_cast<size_t>(corner + CubeCellOffset(cell))] = 0;
    }
    kinds_[cube_index] = free ? CubeKind::kFree : CubeKind::kOpaque;
    pieces_[cube_index] = free ? 1 : 0;
    const float half_cube = static_cast<float>(kNodeCells) / 2.f;
    centres_[LightLayout::NodeIndex(cube_index, 0)] =
        Eigen::Vector3f(static_cast<float>(layout_.origin.x() + (cube[0] * kNodeCells)) + half_cube,
                        static_cast<float>(layout_.origin.y() + (cube[1] * kNodeCells)) + half_cube,
                        static_cast<float>(layout_.origin.z() + (cube[2] * kNodeCells)) + half_cube);
    return;
  }
  kinds_[cube_index] = CubeKind::kMixed;
  std::array<std::array<float, kAxes>, kPiecesPerNode> sums {};
  std::array<int, kPiecesPerNode> counts {};
  std::array<int, kCubeCellCount> queue {};
  int pieces = 0;
  for (int seed = 0; seed < kCubeCellCount; ++seed) {
    if (labels_[static_cast<size_t>(corner + CubeCellOffset(seed))] != kUnlabelled) {
      continue;
    }
    const auto piece = static_cast<uint8_t>(std::min(pieces, kPiecesPerNode - 1));
    ++pieces;
    int tail = 0;
    queue[tail++] = seed;
    labels_[static_cast<size_t>(corner + CubeCellOffset(seed))] = piece;
    for (int head = 0; head < tail; ++head) {
      const int cell = queue[head];
      ++counts[piece];
      for (int axis = 0; axis < kAxes; ++axis) {
        const int along = CubeCellAxis(cell, axis);
        sums[piece][axis] += static_cast<float>(along) + kHalfCell;
        const int axis_step = axis == 0 ? 1 : axis == 1 ? kNodeCells : kNodeCells * kNodeCells;
        for (const int direction : {-1, 1}) {
          if (along + direction < 0 || along + direction >= kNodeCells) {
            continue;
          }
          const int next = cell + (direction * axis_step);
          uint8_t& label = labels_[static_cast<size_t>(corner + CubeCellOffset(next))];
          if (label == kUnlabelled) {
            label = piece;
            queue[tail++] = next;
          }
        }
      }
    }
  }
  pieces_[cube_index] = static_cast<uint8_t>(std::min(pieces, static_cast<int>(UINT8_MAX)));
  for (int piece = 0; piece < kPiecesPerNode; ++piece) {
    if (counts[piece] == 0) {
      continue;
    }
    const auto count = static_cast<float>(counts[piece]);
    centres_[LightLayout::NodeIndex(cube_index, static_cast<uint8_t>(piece))] =
        Eigen::Vector3f(static_cast<float>(layout_.origin.x() + (cube[0] * kNodeCells)) + (sums[piece][0] / count),
                        static_cast<float>(layout_.origin.y() + (cube[1] * kNodeCells)) + (sums[piece][1] / count),
                        static_cast<float>(layout_.origin.z() + (cube[2] * kNodeCells)) + (sums[piece][2] / count));
  }
}

void LightNodes::LinkCube(const std::array<int, 3>& cube) {
  const auto cube_index = static_cast<size_t>(CubeIndex(cube));
  for (int piece = 0; piece < kPiecesPerNode; ++piece) {
    links_[LightLayout::NodeIndex(cube_index, static_cast<uint8_t>(piece))] = 0;
  }
  const int64_t corner = CornerCell(cube);
  const std::array cubes {layout_.cubes.x(), layout_.cubes.y(), layout_.cubes.z()};
  for (size_t side = 0; side < kCubeSides; ++side) {
    const auto axis = static_cast<int>(side / 2);
    const int direction = side % 2 == 0 ? 1 : -1;
    const int next = cube[axis] + direction;
    if (next < 0 || next >= cubes[axis]) {
      continue;
    }
    std::array next_cube = cube;
    next_cube[axis] = next;
    const CubeKind here_kind = kinds_[cube_index];
    const CubeKind there_kind = kinds_[static_cast<size_t>(CubeIndex(next_cube))];
    if (here_kind == CubeKind::kOpaque || there_kind == CubeKind::kOpaque) {
      continue;
    }
    if (here_kind == CubeKind::kFree && there_kind == CubeKind::kFree) {
      links_[LightLayout::NodeIndex(cube_index, 0)] |= 1u << (side * kPiecesPerNode);
      continue;
    }
    // The layer of cells on this side, and the step into the next cube.
    const int layer = direction > 0 ? kNodeCells - 1 : 0;
    const int64_t step = direction * cell_strides_[axis];
    for (int cell = 0; cell < kCubeCellCount; ++cell) {
      if (CubeCellAxis(cell, axis) != layer) {
        continue;
      }
      const int64_t here_index = corner + CubeCellOffset(cell);
      const uint8_t here = labels_[static_cast<size_t>(here_index)];
      const uint8_t there = labels_[static_cast<size_t>(here_index + step)];
      if (here >= kPiecesPerNode || there >= kPiecesPerNode) {
        continue;
      }
      links_[LightLayout::NodeIndex(cube_index, here)] |= 1u << ((side * kPiecesPerNode) + there);
    }
  }
}

std::optional<size_t> LightNodes::NodeAt(const Eigen::Vector3i& cell) const {
  if (!layout_.CellBounds().Contains(cell)) {
    return std::nullopt;
  }
  const uint8_t label = labels_[layout_.CellIndex(cell)];
  if (label >= kPiecesPerNode) {
    return std::nullopt;
  }
  const Eigen::Vector3i local = cell - layout_.origin;
  const Eigen::Vector3i cube(local.x() / kNodeCells, local.y() / kNodeCells, local.z() / kNodeCells);
  return LightLayout::NodeIndex(layout_.CubeIndex(cube), label);
}

std::array<int, 3> LightNodes::CubeCoordinates(size_t node) const {
  const auto cube = static_cast<int64_t>(node / kPiecesPerNode);
  return {static_cast<int>(cube % cube_strides_[1]), static_cast<int>((cube % cube_strides_[2]) / cube_strides_[1]),
          static_cast<int>(cube / cube_strides_[2])};
}

bool LightNodes::InCubes(size_t node, const CellBox& cubes) const {
  const std::array coordinates = CubeCoordinates(node);
  for (int axis = 0; axis < kAxes; ++axis) {
    if (coordinates[axis] < cubes.min[axis] || coordinates[axis] >= cubes.min[axis] + cubes.extent[axis]) {
      return false;
    }
  }
  return true;
}

size_t LightNodes::JoinedCubes() const {
  return static_cast<size_t>(std::ranges::count_if(pieces_, [](uint8_t pieces) { return pieces > kPiecesPerNode; }));
}

}  // namespace z13::station::light
