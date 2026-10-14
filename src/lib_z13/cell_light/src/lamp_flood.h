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
#include <optional>
#include <vector>

#include <Eigen/Dense>

#include <cell_light/lamp.h>

#include "light_nodes.h"

namespace z13::station::light {

// How much of a lamp's color reaches a node.
struct Contribution {
  uint32_t node {};
  float weight {};
};

// A lamp and what it adds to each node it reaches, to take back when it changes.
struct LampState {
  Lamp lamp;
  // Where its light starts (EmitterOf); nothing while it is walled in.
  std::optional<Eigen::Vector3f> emitter;
  std::vector<Contribution> contributions;
  // The cubes holding those nodes: an edit elsewhere can't change its light.
  z13::building::primitives::CellBox reached;
};

// The nodes per flood, so floods share one array without clearing it.
struct FloodMarks {
  std::vector<uint32_t> flood_of_node;
  uint32_t last_flood {};
};

// Where the lamp's light starts: its position, or behind it when that lies in a block.
std::optional<Eigen::Vector3f> EmitterOf(const LightNodes& nodes, const Lamp& lamp);

// Floods the lamp's light from `emitter`'s node through the linked nodes within its radius. It fades
// as (1 - d²/r²)² with d the longer of the straight distance and the path's (light bending
// round a wall reaches less far).
std::vector<Contribution> FloodLamp(const LightNodes& nodes, const Lamp& lamp, const Eigen::Vector3f& emitter,
                                    FloodMarks& marks);

}  // namespace z13::station::light
