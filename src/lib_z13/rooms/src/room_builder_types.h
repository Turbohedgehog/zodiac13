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
#include <utility>

#include <Eigen/Dense>

#include <primitives/placement.h>
#include <rooms/room_id.h>

namespace z13::station::rooms {

// A room's extent while its leaves are gathered.
struct RoomExtent {
  Eigen::Vector3i anchor = Eigen::Vector3i::Zero();
  Eigen::Vector3i low = Eigen::Vector3i::Zero();
  Eigen::Vector3i high = Eigen::Vector3i::Zero();
  int64_t volume {};
};

// A leaf's part of one side of an opening, on the opening's face: the face's two other
// axes, in order.
struct FacePatch {
  Eigen::Vector2i min = Eigen::Vector2i::Zero();
  Eigen::Vector2i end = Eigen::Vector2i::Zero();
  RoomId room {};
};

// The cells of one opening's face between a pair of rooms.
struct OpeningShare {
  // Lower id first.
  std::pair<RoomId, RoomId> rooms;
  int area_cells {};
  RoomId low_side {};
};

}  // namespace z13::station::rooms
