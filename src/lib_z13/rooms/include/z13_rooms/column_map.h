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

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include <Eigen/Dense>

namespace z13::station::rooms {

// Index into RoomGraph::rooms; the vacuum outside the station is always room 0.
using RoomIndex = uint32_t;
inline constexpr RoomIndex kVacuumRoom = 0;

// The cells (x, y, z) of one column with z in [begin, end) that no sealing block occupies.
struct FreeInterval {
  int begin {};
  int end {};
  RoomIndex room {};
};

// The free space of the station as intervals along z for each column (x, y) of a dense
// grid, in compressed rows: 7 million cells become ~600 thousand intervals.
class ColumnMap {
 public:
  ColumnMap() = default;
  // The grid spans `heights` (begin, end) along z too; everything outside it is the vacuum.
  ColumnMap(Eigen::Vector2i origin, Eigen::Vector2i size, Eigen::Vector2i heights, std::vector<size_t> offsets,
            std::vector<FreeInterval> intervals);

  const Eigen::Vector2i& Origin() const { return origin_; }
  const Eigen::Vector2i& Size() const { return size_; }
  // The z range (begin, end) the grid spans.
  const Eigen::Vector2i& Heights() const { return heights_; }
  size_t ColumnCount() const { return static_cast<size_t>(size_.x()) * static_cast<size_t>(size_.y()); }

  // Empty outside the grid.
  std::span<const FreeInterval> Column(const Eigen::Vector2i& xy) const;
  std::span<const FreeInterval> ColumnAt(size_t column) const;
  // The interval indices [FirstInterval(column), FirstInterval(column + 1)) are a column's.
  size_t FirstInterval(size_t column) const { return offsets_[column]; }
  std::span<const FreeInterval> Intervals() const { return intervals_; }
  std::span<FreeInterval> MutableIntervals() { return intervals_; }

  // Nothing for a sealed cell; the vacuum outside the grid.
  std::optional<RoomIndex> RoomAt(const Eigen::Vector3i& cell) const { return RoomAt(cell.x(), cell.y(), cell.z()); }
  std::optional<RoomIndex> RoomAt(int x, int y, int z) const;

 private:
  Eigen::Vector2i origin_ = Eigen::Vector2i::Zero();
  Eigen::Vector2i size_ = Eigen::Vector2i::Zero();
  Eigen::Vector2i heights_ = Eigen::Vector2i::Zero();
  std::vector<size_t> offsets_ {0};
  std::vector<FreeInterval> intervals_;
};

}  // namespace z13::station::rooms
