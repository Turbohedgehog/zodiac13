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
#include <limits>
#include <vector>

#include <Eigen/Dense>

namespace z13::gravity {

// The acceleration of the room a body is in, zero in vacuum. State: rooms are rebuilt after
// movement, so a replayed tick must not read those of a later one.
struct Gravity {
  using State = void;
  Eigen::Vector3f acceleration = Eigen::Vector3f::Zero();

  bool Pulls() const { return !acceleration.isZero(); }
};

// Which way is up for a player: the unit vector that follows the gravity a little each tick, so the camera turns
// smoothly when the player passes into a room whose gravity differs. +Z until some gravity has pulled otherwise.
struct PlayerUp {
  using State = void;
  Eigen::Vector3f up = Eigen::Vector3f::UnitZ();

  bool operator==(const PlayerUp&) const = default;
};

// How far a generator's field has risen, from nothing to kGravityFull; whole numbers all through so peers agree.
using GravityLevel = uint16_t;
inline constexpr GravityLevel kGravityFull = std::numeric_limits<GravityLevel>::max();

// On the block of a gravity generator, added when it appears.
struct GravityCharge {
  using State = void;
  GravityLevel level {};

  bool operator==(const GravityCharge&) const = default;
};

struct GravitySource {
  Eigen::Vector3i cell = Eigen::Vector3i::Zero();
  GravityLevel level {};

  bool operator==(const GravitySource&) const = default;
};

// Every generator of the main grid, by cell, set by gravity_module each tick; station_module turns them into the
// gravity of the rooms. Derived, never state. Without generators every room pulls.
struct GravitySources {
  using Singleton = void;
  std::vector<GravitySource> list;

  bool operator==(const GravitySources&) const = default;
};

}  // namespace z13::gravity
