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

#include <cstdint>
#include <string_view>

#include <Eigen/Dense>

namespace z13::station {

// The id of a grid: given by the simulation, the same for every participant.
using GridId = uint32_t;

// The grid the station is built on, always present.
inline constexpr GridId kMainGrid = 0;
inline constexpr std::string_view kMainGridName = "MainGrid";

// A grid of cells with its own pose: the cells of its blocks are in its own axes. The pose is set once
// when the grid is made and never integrated.
struct Grid {
  using State = void;
  GridId id = kMainGrid;
  // Where the grid's cell (0, 0, 0) stands in the world's cells, and its turn about the world's Z.
  Eigen::Vector3i origin = Eigen::Vector3i::Zero();
  float yaw_degrees {};

  bool operator==(const Grid&) const = default;
};

}  // namespace z13::station
