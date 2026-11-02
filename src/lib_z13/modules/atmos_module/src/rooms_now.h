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

#include <rooms/room_graph.h>

#include "airlock.h"
#include "door_at.h"
#include "room_index.h"

namespace z13::atmos_module {

// What the room graph at hand gives a tick: the chambers, the doors and the airlocks.
struct RoomsNow {
  std::reference_wrapper<const z13::station::rooms::RoomGraph> graph;
  RoomIndex index;
  std::vector<DoorAt> doors;
  std::vector<Airlock> airlocks;
};

}  // namespace z13::atmos_module
