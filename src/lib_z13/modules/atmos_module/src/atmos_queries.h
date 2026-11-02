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
#include <flecs.h>

#include <z13/components/player.h>

#include "airlock.h"
#include "door_at.h"
#include "room_air_store.h"

namespace z13::atmos_module {

using PlayerQuery = flecs::query<const z13::gameplay::Player, const Eigen::Matrix4f>;

// The queries AtmosSystem runs on its ticks, built once with the system.
struct AtmosQueries {
  DoorQuery doors;
  PlayerQuery players;
  RoomAirQuery stored;
  AirlockQuery airlocks;
  InterlockQuery interlocks;
};

}  // namespace z13::atmos_module
