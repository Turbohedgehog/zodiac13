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

namespace z13::station::rooms {

// Where a measuring camera stands and looks, in cells.
struct Viewpoint {
  RoomIndex room {};
  Eigen::Vector3f eye = Eigen::Vector3f::Zero();
  Eigen::Vector3f target = Eigen::Vector3f::Zero();
};

// Eye level above a room's floor, in cells.
inline constexpr int kViewpointEyeCells = 6;

// Viewpoints for measuring the renderer, the same for the same graph: `rooms` rooms spread
// from the smallest to the largest, each seen from a corner at eye level in four directions,
// then the whole station from outside.
std::vector<Viewpoint> TourViewpoints(const RoomGraph& graph, size_t rooms);

}  // namespace z13::station::rooms
