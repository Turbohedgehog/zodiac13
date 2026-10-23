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

#include <rooms/corridor_walk.h>

#include <algorithm>
#include <deque>
#include <optional>
#include <unordered_map>

namespace z13::station::rooms {

namespace {

using z13::building::primitives::CellBox;

// How far inside a room the eye stands from the portal it came by, in cells.
constexpr int kEntryDepthCells = 2;

Eigen::Vector3f CentreOf(const CellBox& box) {
  return (box.min.cast<float>() + box.End().cast<float>()) / 2.f;
}

// How a room was reached: through which portal, from which room.
struct Arrival {
  size_t portal {};
  RoomId from {};
  size_t hops {};
};

std::unordered_map<RoomId, Arrival> ReachFrom(const RoomGraph& graph, RoomId start) {
  std::unordered_map<RoomId, Arrival> reached {{start, Arrival {.portal = 0, .from = start, .hops = 0}}};
  std::deque<RoomId> pending {start};
  while (!pending.empty()) {
    const RoomId room = pending.front();
    pending.pop_front();
    for (const size_t index : graph.PortalsOf(room)) {
      const Portal& portal = graph.portals[index];
      const RoomId other = portal.a == room ? portal.b : portal.a;
      if (other == kVacuumRoom || reached.contains(other)) {
        continue;
      }
      reached.emplace(other, Arrival {.portal = index, .from = room, .hops = reached.at(room).hops + 1});
      pending.push_back(other);
    }
  }
  return reached;
}

// Eye level above the room's floor.
float EyeHeight(const RoomGraph& graph, RoomId room) {
  return static_cast<float>(graph.rooms.at(room).anchor.z() + kViewpointEyeCells) + 0.5f;
}

// Just inside `room`, past `portal`, at eye level; nothing if that cell is not the room's.
std::optional<Eigen::Vector3f> EntryInto(const RoomGraph& graph, const Portal& portal, RoomId room) {
  const auto axis = static_cast<Eigen::Index>(portal.axis);
  Eigen::Vector3f eye = CentreOf(portal.opening);
  eye.z() = EyeHeight(graph, room);
  const bool low_side = room == portal.low_side;
  eye[axis] = low_side ? static_cast<float>(portal.opening.min[axis] - kEntryDepthCells) + 0.5f
                       : static_cast<float>(portal.opening.End()[axis] + kEntryDepthCells) - 0.5f;
  const Eigen::Vector3i cell = eye.array().floor().cast<int>();
  return graph.RoomAt(cell) == room ? std::optional(eye) : std::nullopt;
}

}  // namespace

std::vector<Viewpoint> CorridorWalk(const RoomGraph& graph, const Eigen::Vector3f& start, size_t rooms) {
  const auto first = graph.RoomAt(start.array().floor().cast<int>());
  if (!first || *first == kVacuumRoom) {
    return {};
  }
  const std::unordered_map<RoomId, Arrival> reached = ReachFrom(graph, *first);
  RoomId farthest = *first;
  for (const RoomId room : graph.ByAnchor()) {
    if (reached.contains(room) && reached.at(room).hops > reached.at(farthest).hops) {
      farthest = room;
    }
  }
  std::vector<Arrival> way;
  for (RoomId room = farthest; room != *first; room = reached.at(room).from) {
    way.push_back(reached.at(room));
  }
  std::ranges::reverse(way);
  way.resize(std::min(way.size(), rooms));

  std::vector<Viewpoint> viewpoints;
  const auto look_from = [&](RoomId room, const Eigen::Vector3f& eye, const Eigen::Vector3f& toward) {
    viewpoints.push_back({.room = room, .eye = eye, .target = {toward.x(), toward.y(), eye.z()}});
  };
  RoomId in = *first;
  Eigen::Vector3f eye = start;
  for (const Arrival& step : way) {
    const Portal& portal = graph.portals[step.portal];
    look_from(in, eye, CentreOf(portal.opening));
    const RoomId next = portal.a == in ? portal.b : portal.a;
    const auto entry = EntryInto(graph, portal, next);
    if (!entry) {
      return viewpoints;
    }
    in = next;
    eye = *entry;
  }
  look_from(in, eye, CentreOf(graph.rooms.at(in).bounds));
  return viewpoints;
}

}  // namespace z13::station::rooms
