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

#include <rooms/light_reach.h>

#include <algorithm>
#include <cstddef>

namespace z13::station::rooms {

namespace {

using z13::building::primitives::CellBox;

Eigen::AlignedBox3f WithWalls(const CellBox& bounds) {
  return {(bounds.min - Eigen::Vector3i::Ones()).cast<float>(), (bounds.End() + Eigen::Vector3i::Ones()).cast<float>()};
}

Eigen::AlignedBox3f BoxOf(const CellBox& cells) {
  return {cells.min.cast<float>(), cells.End().cast<float>()};
}

// From `room`, through every visible portal whose opening the light reaches.
std::vector<RoomIndex> LitRooms(const RoomGraph& graph, RoomIndex room, const Eigen::Vector3f& light, float radius) {
  std::vector<RoomIndex> lit {room};
  for (size_t next = 0; next < lit.size(); ++next) {
    for (const size_t index : graph.room_portals[lit[next]]) {
      const Portal& portal = graph.portals[index];
      const RoomIndex other = portal.a == lit[next] ? portal.b : portal.a;
      if (!portal.visible || other == kVacuumRoom || std::ranges::contains(lit, other) ||
          BoxOf(portal.opening).exteriorDistance(light) >= radius) {
        continue;
      }
      lit.push_back(other);
    }
  }
  std::ranges::sort(lit);
  return lit;
}

}  // namespace

std::optional<LightReach> ReachOf(const z13::building::primitives::PointLight& light, OptionalRoomGraph graph) {
  const float radius = static_cast<float>(light.source.radius_cells);
  const Eigen::Vector3f extent = Eigen::Vector3f::Constant(radius);
  const Eigen::AlignedBox3f radius_box(light.position - extent, light.position + extent);
  if (!graph) {
    return LightReach {.reach = radius_box, .radius_box = radius_box};
  }
  const auto room = graph->get().RoomAt(light.cell);
  if (!room) {
    return std::nullopt;
  }
  if (*room == kVacuumRoom) {
    return LightReach {.reach = radius_box, .radius_box = radius_box};
  }
  return LightReach {
      .room = *room,
      .lit_rooms = LitRooms(*graph, *room, light.position, radius),
      .reach = radius_box.intersection(WithWalls(graph->get().rooms[*room].bounds)),
      .radius_box = radius_box,
  };
}

}  // namespace z13::station::rooms
