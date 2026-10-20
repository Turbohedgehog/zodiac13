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

#include <flecs.h>

#include <z13/components/gameplay.h>
#include <z13/components/wall.h>
#include <primitives/wall_panel.h>

#include "wall_joints.h"

namespace z13::building {

// A node at `cell` named by the next id of `counters`.
flecs::entity SpawnWallNode(flecs::world world, z13::gameplay::IdCounters& counters, const Eigen::Vector3i& cell);

// A segment between two nodes (by id); its panel block follows in WallPanelSystem.
flecs::entity SpawnWallSegment(
    flecs::world world, z13::gameplay::IdCounters& counters, const z13::station::WallSegment& segment);

// A segment for `wall` between nodes at its two ends, existing ones where nodes already stand.
flecs::entity SpawnWall(
    flecs::world world, z13::gameplay::IdCounters& counters, const z13::building::primitives::WallGeometry& wall);

// The id of the node at `cell`, spawning one when there is none.
uint32_t WallNodeAt(flecs::world world, z13::gameplay::IdCounters& counters, const Eigen::Vector3i& cell);

std::optional<flecs::entity> FindWallNode(flecs::world world, uint32_t id);
std::optional<flecs::entity> FindWallSegment(flecs::world world, uint32_t id);

// Every segment with its nodes' cells, in segment id order.
std::vector<JointWall> JointWallsOf(flecs::world world);

// The same with `wall` added, joined to the nodes standing at its ends (ids of its own, for a
// wall that is not spawned yet, above all existing ones).
std::vector<JointWall> JointWallsWith(flecs::world world, const z13::building::primitives::WallGeometry& wall);

// Removes a segment, and the nodes no other segment uses.
void DestroyWallSegment(flecs::world world, uint32_t segment_id);

}  // namespace z13::building
