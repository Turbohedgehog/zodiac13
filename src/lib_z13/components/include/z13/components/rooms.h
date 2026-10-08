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
#include <string>
#include <vector>

#include <Eigen/Dense>

#include <z13/components/color.h>

namespace z13::station {

// Where the rooms and portals are updated, after building.
struct StationTopologyPhase {};

// Counts every change to the blocks that shape the rooms. `fingerprint` is what the blocks
// hashed to at the last change, kept so a restored snapshot continues the count the way
// the original run did.
struct TopologyVersion {
  using State = void;
  using Singleton = void;
  uint64_t version {};
  uint64_t fingerprint {};
};

inline constexpr Rgba kRoomOverlayColor {80, 200, 120, 255};
inline constexpr Rgba kWindowOverlayColor {80, 160, 255, 255};
inline constexpr Rgba kDoorOverlayColor {255, 160, 60, 255};

struct OverlayBox {
  Eigen::Vector3i min = Eigen::Vector3i::Zero();
  Eigen::Vector3i extent = Eigen::Vector3i::Zero();
  Rgba color {};

  bool operator==(const OverlayBox&) const = default;
};

// Debug view of the local player's room and its portals, filled only while `enabled`.
struct RoomOverlay {
  using Singleton = void;
  bool enabled {};
  std::vector<OverlayBox> boxes;
  std::string label;
  // What boxes and label were made for, so they are rebuilt only when it changes.
  std::optional<uint64_t> shown_fingerprint;
  std::optional<uint32_t> shown_room;
};

}  // namespace z13::station
