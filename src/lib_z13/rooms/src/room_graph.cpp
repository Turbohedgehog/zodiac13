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

#include <rooms/room_graph.h>

#include <algorithm>
#include <tuple>

namespace z13::station::rooms {

bool AnchorBefore(const Eigen::Vector3i& first, const Eigen::Vector3i& second) {
  return std::tuple(first.z(), first.y(), first.x()) < std::tuple(second.z(), second.y(), second.x());
}

std::vector<RoomId> RoomGraph::ByAnchor() const {
  std::vector<RoomId> ids;
  ids.reserve(rooms.size());
  for (const auto& [id, room] : rooms) {
    if (id != kVacuumRoom) {
      ids.push_back(id);
    }
  }
  std::ranges::sort(ids, [this](RoomId a, RoomId b) { return AnchorBefore(rooms.at(a).anchor, rooms.at(b).anchor); });
  return ids;
}

std::span<const size_t> RoomGraph::PortalsOf(RoomId room) const {
  const auto found = room_portals.find(room);
  return found == room_portals.end() ? std::span<const size_t> {} : std::span<const size_t>(found->second);
}

}  // namespace z13::station::rooms
