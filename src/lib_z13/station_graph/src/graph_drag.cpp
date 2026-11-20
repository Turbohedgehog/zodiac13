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

#include "station_graph/graph_drag.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace z13::editor::graph {

namespace {

constexpr float kGripBandCells = 1.f;
constexpr int kMinSideCells = 1;

struct SideGrip {
  int axis {};
  bool high {};
};

}  // namespace

Grip GripAt(const RoomBox& box, const Eigen::Vector2f& floor_point) {
  constexpr std::array kSides = {SideGrip {0, false}, SideGrip {0, true}, SideGrip {1, false}, SideGrip {1, true}};
  const auto distance = [&](const SideGrip& side) {
    const float edge = static_cast<float>(box.origin[side.axis] + (side.high ? box.size[side.axis] : 0));
    return std::abs(floor_point[side.axis] - edge);
  };
  const auto nearest = std::ranges::min_element(kSides, {}, distance);
  if (!(distance(*nearest) < kGripBandCells)) {
    return {};
  }
  Grip grip {.low = Eigen::Vector2i::Zero(), .high = Eigen::Vector2i::Zero()};
  (nearest->high ? grip.high : grip.low)[nearest->axis] = 1;
  return grip;
}

RoomBox Dragged(const RoomBox& box, const Grip& grip, const Eigen::Vector2i& offset_cells) {
  const Eigen::Vector2i low = box.origin.head<2>();
  const Eigen::Vector2i high = low + box.size;
  const Eigen::Vector2i moved_low = low + grip.low.cwiseProduct(offset_cells);
  const Eigen::Vector2i moved_high = high + grip.high.cwiseProduct(offset_cells);
  const Eigen::Array2i one = Eigen::Array2i::Constant(kMinSideCells);
  const auto low_only = grip.low.array() != 0 && grip.high.array() == 0;
  const auto high_only = grip.high.array() != 0 && grip.low.array() == 0;
  // A pulled side stops a cell short of the one that stays put.
  const Eigen::Vector2i new_low = low_only.select(moved_low.array().min(high.array() - one), moved_low.array()).matrix();
  const Eigen::Vector2i new_high = high_only.select(moved_high.array().max(low.array() + one), moved_high.array()).matrix();
  RoomBox result = box;
  result.origin.head<2>() = new_low;
  result.size = new_high - new_low;
  return result;
}

}  // namespace z13::editor::graph
