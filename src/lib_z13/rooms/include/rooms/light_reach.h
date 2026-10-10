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

#include <optional>
#include <vector>

#include <Eigen/Dense>

#include <primitives/lights.h>
#include <rooms/room_graph.h>

namespace z13::station::rooms {

// The room a light belongs to and what it can light, in cells.
struct LightReach {
  // Nothing in the vacuum or without a graph.
  std::optional<RoomIndex> room;
  // Its room and those it shines into through visible portals (windows, open doors) within
  // its radius, sorted; the vacuum isn't one.
  std::vector<RoomIndex> lit_rooms;
  // For a light without shadows: radius_box cut to its room and the room's walls, so it
  // doesn't light the next room through a wall; all of radius_box outside rooms.
  Eigen::AlignedBox3f reach;
  // The whole box around its radius, for a light whose shadows stop it at the walls.
  Eigen::AlignedBox3f radius_box;
};

// Nothing for a light inside a block, e.g. a lamp turned to face a wall: it lights nothing.
std::optional<LightReach> ReachOf(const z13::building::primitives::PointLight& light, OptionalRoomGraph graph);

}  // namespace z13::station::rooms
