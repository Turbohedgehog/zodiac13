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
#include <unordered_map>
#include <vector>

#include <Eigen/Dense>

#include <z13/components/gravity.h>

#include <rooms/room_id.h>

namespace z13::station {

struct RoomGravitySource {
  // RoomCache::Switches() when by_room was last brought up to date.
  uint64_t switches {};
  float gravity {};
  uint32_t generator_radius_cells {};
  std::vector<z13::gravity::GravitySource> sources;

  bool operator==(const RoomGravitySource&) const = default;
};

// Each room's gravity: physics.gravity down, zero in vacuum, and where the station has gravity generators the share
// of the strongest one within reach. Derived, never state.
struct RoomGravity {
  using Singleton = void;
  std::optional<RoomGravitySource> source;
  std::unordered_map<rooms::RoomId, Eigen::Vector3f> by_room;
};

}  // namespace z13::station
