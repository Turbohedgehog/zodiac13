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

#include <z13_rooms/column_map.h>

#include <algorithm>
#include <utility>

namespace z13::station::rooms {

ColumnMap::ColumnMap(Eigen::Vector2i origin, Eigen::Vector2i size, Eigen::Vector2i heights,
                     std::vector<size_t> offsets, std::vector<FreeInterval> intervals)
    : origin_(std::move(origin)),
      size_(std::move(size)),
      heights_(std::move(heights)),
      offsets_(std::move(offsets)),
      intervals_(std::move(intervals)) {}

std::span<const FreeInterval> ColumnMap::ColumnAt(size_t column) const {
  return std::span(intervals_).subspan(offsets_[column], offsets_[column + 1] - offsets_[column]);
}

std::span<const FreeInterval> ColumnMap::Column(const Eigen::Vector2i& xy) const {
  const int x = xy.x() - origin_.x();
  const int y = xy.y() - origin_.y();
  if (x < 0 || y < 0 || x >= size_.x() || y >= size_.y()) {
    return {};
  }
  return ColumnAt(static_cast<size_t>(y) * static_cast<size_t>(size_.x()) + static_cast<size_t>(x));
}

std::optional<RoomIndex> ColumnMap::RoomAt(int x, int y, int z) const {
  const bool outside = x < origin_.x() || y < origin_.y() || x >= origin_.x() + size_.x() ||
                       y >= origin_.y() + size_.y() || z < heights_.x() || z >= heights_.y();
  if (outside) {
    return kVacuumRoom;
  }
  const std::span<const FreeInterval> column = Column({x, y});
  // The first interval ending above the cell is the only one that can hold it.
  const auto it = std::ranges::upper_bound(column, z, {}, &FreeInterval::end);
  if (it == column.end() || it->begin > z) {
    return std::nullopt;
  }
  return it->room;
}

}  // namespace z13::station::rooms
