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
#include <unordered_map>
#include <vector>

#include <Eigen/Dense>

#include <z13/components/station.h>
#include <primitives/geometry.h>
#include <rooms/room_graph.h>

#include "lights.h"

namespace z13::raylib {

class ViewCulling;

// What StationLights was last synced with; other blocks also need a sync.
struct LightsSyncKey {
  std::optional<uint64_t> palette_hash;
  std::optional<uint64_t> rooms_fingerprint;

  bool operator==(const LightsSyncKey&) const = default;
};

// A lamp and the room it belongs to; nothing in the vacuum.
struct StationLight {
  FrameLight light;
  std::optional<z13::station::rooms::RoomIndex> room;
};

// The station's lamps, each kept to its room (rooms/light_reach.h). Singleton; never state.
class StationLights {
 public:
  using Singleton = void;
  using OptionalPalette = z13::building::primitives::OptionalPalette;

  bool SyncedWith(OptionalPalette palette, std::optional<uint64_t> rooms_fingerprint) const;
  void Sync(std::span<const z13::station::Block> blocks, OptionalPalette palette,
            z13::station::rooms::OptionalRoomGraph rooms, std::optional<uint64_t> rooms_fingerprint);

  // Up to `count` lamps, nearest first: those lighting a room the camera sees, and of the
  // lamps outside rooms those whose reach it sees.
  std::vector<FrameLight> Visible(const ViewCulling& culling, const Eigen::Vector3f& eye, size_t count) const;

 private:
  std::vector<StationLight> lights_;
  // Indices into lights_, per room they light.
  std::unordered_map<z13::station::rooms::RoomIndex, std::vector<size_t>> by_room_;
  std::optional<LightsSyncKey> synced_with_;
};

}  // namespace z13::raylib
