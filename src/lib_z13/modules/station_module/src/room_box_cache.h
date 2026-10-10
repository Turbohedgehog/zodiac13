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

#include <z13/components/rooms.h>
#include <rooms/room_graph.h>

namespace z13::station {

// Each room's merged overlay boxes, made once per room graph. Derived, never state.
class RoomBoxCache {
 public:
  using Singleton = void;

  const std::vector<OverlayBox>& BoxesOf(
      const z13::station::rooms::RoomGraph& graph, uint64_t fingerprint, z13::station::rooms::RoomIndex room);

 private:
  std::optional<uint64_t> fingerprint_;
  std::unordered_map<z13::station::rooms::RoomIndex, std::vector<OverlayBox>> boxes_;
};

}  // namespace z13::station
