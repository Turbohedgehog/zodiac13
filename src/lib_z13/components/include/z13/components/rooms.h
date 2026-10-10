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

// What the room overlay draws: the rooms the renderer drew, in one colour, or every room
// and portal; F4 steps through them in this order.
enum class RoomOverlayMode : uint8_t { kOff, kSeen, kAll };

inline RoomOverlayMode NextRoomOverlayMode(RoomOverlayMode mode) {
  switch (mode) {
    case RoomOverlayMode::kOff:
      return RoomOverlayMode::kSeen;
    case RoomOverlayMode::kSeen:
      return RoomOverlayMode::kAll;
    case RoomOverlayMode::kAll:
      return RoomOverlayMode::kOff;
  }
  return RoomOverlayMode::kOff;
}

// What the overlay's boxes and label were made for, so they are rebuilt only when it changes.
struct RoomOverlayShown {
  RoomOverlayMode mode {};
  uint64_t fingerprint {};
  std::optional<uint32_t> player_room;
  std::optional<std::vector<uint32_t>> seen_rooms;

  bool operator==(const RoomOverlayShown&) const = default;
};

// The rooms the renderer drew, sorted, numbered as in the graph of `fingerprint`.
struct RoomsDrawn {
  uint64_t fingerprint {};
  std::vector<uint32_t> rooms;
};

// Debug view of the rooms, with a label for the local player's room; empty while off.
struct RoomOverlay {
  using Singleton = void;
  RoomOverlayMode mode {};
  // Set by the renderer each frame in kSeen; nothing while rooms don't cull.
  std::optional<RoomsDrawn> drawn;
  std::vector<OverlayBox> boxes;
  std::string label;
  std::optional<RoomOverlayShown> shown;
};

}  // namespace z13::station
