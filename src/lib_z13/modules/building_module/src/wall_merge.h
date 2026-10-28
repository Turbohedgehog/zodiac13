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
#include <expected>
#include <optional>
#include <string>

#include <Eigen/Dense>
#include <flecs.h>

#include <z13/components/building.h>
#include <z13/components/spawn_stamp.h>
#include <primitives/palette.h>

#include "brush_aim.h"

namespace z13::building {

// Two wall nodes becoming one: `keep` takes `cell` and the walls of `drop`.
struct NodeMerge {
  z13::station::WallId keep {};
  z13::station::WallId drop {};
  Eigen::Vector3i cell = Eigen::Vector3i::Zero();
};

// The wall node nearest the aim within `snap_cells`; ties go to the lower id.
std::optional<z13::station::WallId> NearestWallNode(flecs::world world, const Aim& aim, float snap_cells);

std::optional<Eigen::Vector3i> WallNodeCell(flecs::world world, z13::station::WallId node_id);

// Merging `second` into `first`, or why it can't be: a wall joining them would collapse, every
// wall of the two must still make a panel at the new place, and no joint may become one that
// the walls' angle can't have (`joint_reach`, wall_joints.h).
std::expected<NodeMerge, std::string> PlanNodeMerge(
    flecs::world world, z13::station::WallId first, z13::station::WallId second, MergeAt at,
    const z13::building::primitives::Palette& palette, int joint_reach);

// Moves `keep` to the new cell, hands it the walls of `drop`, and splits the walls whose line
// the new cell lies on, so a node on a wall divides it.
void ApplyNodeMerge(flecs::world world, z13::gameplay::SpawnNamer& namer, const NodeMerge& merge);

}  // namespace z13::building
