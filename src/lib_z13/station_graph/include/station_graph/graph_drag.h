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

#include <Eigen/Dense>

#include "station_graph/graph.h"

namespace z13::editor::graph {

// What a drag pulls: per axis, 1 where the low side (origin) or the high side (origin + size) follows the pointer.
// A move pulls both sides, one edge pulls only its own.
struct Grip {
  Eigen::Vector2i low = Eigen::Vector2i::Ones();
  Eigen::Vector2i high = Eigen::Vector2i::Ones();
};

// The grip a floor point takes: the nearest side of the footprint within one cell, else the whole room.
Grip GripAt(const RoomBox& box, const Eigen::Vector2f& floor_point);

// The box after the grip moves by `offset_cells`; a side never crosses the opposite one (a room stays one cell wide).
RoomBox Dragged(const RoomBox& box, const Grip& grip, const Eigen::Vector2i& offset_cells);

}  // namespace z13::editor::graph
