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

#include <functional>
#include <vector>

#include <Eigen/Dense>

#include <rooms/room_graph.h>
#include <z13/components/station/grid_id.h>

#include "airlock.h"
#include "door_at.h"
#include "room_index.h"

namespace z13::atmos_module {

// The room graph of one grid at hand: its chambers, which are numbered from `first` among the chambers of all grids,
// its doors, and the pose that takes the world's meters into its cells.
struct GridAir {
  z13::station::GridId grid {};
  std::reference_wrapper<const z13::station::rooms::RoomGraph> graph;
  RoomIndex index;
  std::vector<DoorAt> doors;
  size_t first {};
  Eigen::Isometry3f from_world = Eigen::Isometry3f::Identity();
};

// What the room graphs at hand give a tick: the grids, the main one first, and the airlocks, which are the main
// grid's.
struct RoomsNow {
  std::vector<GridAir> grids;
  std::vector<Airlock> airlocks;

  const GridAir& Main() const { return grids.front(); }
};

}  // namespace z13::atmos_module
