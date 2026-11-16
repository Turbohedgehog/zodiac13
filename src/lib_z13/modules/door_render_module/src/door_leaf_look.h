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

#include <z13/components/station/block.h>
#include <z13/components/station/door.h>

#include "door_lamp.h"

namespace z13::door_render {

// What a door's leaf SceneMesh was built from; derived, never state.
struct DoorLeafLook {
  z13::station::Block block;
  int position {};
  // Of the door's grid: the leaf is built in its meters and moved into the world.
  Eigen::Matrix4f pose = Eigen::Matrix4f::Identity();

  bool operator==(const DoorLeafLook&) const = default;
};

// What a door's lamp mesh was built from; derived, never state.
struct DoorLampLook {
  z13::station::Block block;
  DoorSignal signal {};
  Eigen::Matrix4f pose = Eigen::Matrix4f::Identity();

  bool operator==(const DoorLampLook&) const = default;
};

// On the lamp entity of a door.
struct DoorLampOf {
  flecs::entity_t door {};
};

}  // namespace z13::door_render
