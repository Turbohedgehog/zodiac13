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
#include <functional>
#include <optional>
#include <string_view>

#include <Eigen/Dense>
#include <flecs.h>

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
  // Where the grid's cell (0, 0, 0) stands in the world's cells, its turn about the world's Z, and then its tilt about
  // its own Y (GridToWorld): a positive pitch raises the end along +X.
  Eigen::Vector3i origin = Eigen::Vector3i::Zero();
  float yaw_degrees {};
  float pitch_degrees {};

  bool operator==(const Grid&) const = default;
};

// The entity of the main grid; not valid while there is none (before the world data, after the station was left).
// What is derived from a grid's blocks (the block index, the rooms) is a component of its entity; for now every reader
// takes the main grid's, looked up each time since the entity is made anew with every station.
inline flecs::entity MainGridEntity(flecs::world world) {
  return world.lookup(kMainGridName.data());
}

// The main grid's component T, if it has one.
template <typename T>
std::optional<std::reference_wrapper<const T>> MainGridData(flecs::world world) {
  const flecs::entity grid = MainGridEntity(world);
  return grid.is_valid() && grid.has<T>() ? std::optional(std::cref(grid.get<T>())) : std::nullopt;
}

// The same, to change in place: for a system that declares it writes T.
template <typename T>
std::optional<std::reference_wrapper<T>> MutableMainGridData(flecs::world world) {
  flecs::entity grid = MainGridEntity(world);
  return grid.is_valid() && grid.has<T>() ? std::optional(std::ref(grid.get_mut<T>())) : std::nullopt;
}

}  // namespace z13::station
