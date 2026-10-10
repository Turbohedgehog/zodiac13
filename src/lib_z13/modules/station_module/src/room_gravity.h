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
#include <optional>
#include <vector>

#include <Eigen/Dense>

namespace z13::station {

// What RoomGravity was built from: the room graph's fingerprint and physics.gravity.
struct RoomGravitySource {
  uint64_t fingerprint {};
  float gravity {};

  bool operator==(const RoomGravitySource&) const = default;
};

// The gravity of each room of the current room graph, zero in vacuum. For now every room
// pulls down with physics.gravity; gravity generators (f/gravity) will set it per room or
// over an area. Derived, never state.
struct RoomGravity {
  using Singleton = void;
  std::optional<RoomGravitySource> source;
  std::vector<Eigen::Vector3f> by_room;
};

}  // namespace z13::station
