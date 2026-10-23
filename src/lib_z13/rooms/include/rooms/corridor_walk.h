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
#include <vector>

#include <Eigen/Dense>

#include <rooms/room_graph.h>
#include <rooms/tour_viewpoints.h>

namespace z13::station::rooms {

// A walk through the rooms, the same for the same graph, from the room holding `start` (a point
// in cells) to the one farthest from it by portals (the vacuum is no room to cross; ties go to
// the first by anchor): a viewpoint in the first room looking at the way on, then one just
// inside each room entered, looking at the way on or, in the last, at the room's centre. At
// most `rooms` rooms are entered. Empty where `start` is in no room.
std::vector<Viewpoint> CorridorWalk(const RoomGraph& graph, const Eigen::Vector3f& start, size_t rooms);

}  // namespace z13::station::rooms
