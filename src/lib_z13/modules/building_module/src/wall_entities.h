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
#include <set>
#include <vector>

#include <flecs.h>

#include <z13/components/station/spawn_stamp.h>
#include <z13/components/station/wall.h>
#include <primitives/wall_panel.h>

#include "wall_joints.h"

namespace z13::building {

flecs::entity SpawnWallNode(flecs::world world, z13::gameplay::SpawnNamer& namer, const Eigen::Vector3i& cell,
                            z13::station::GridId grid = z13::station::kMainGrid);

// A segment between two nodes (by id); its panel block follows in WallPanelSystem.
flecs::entity SpawnWallSegment(
    flecs::world world, z13::gameplay::SpawnNamer& namer, const z13::station::WallSegment& segment);

// A segment for `wall` (in the cells of `grid`) between nodes at its two ends, existing ones where nodes already stand.
flecs::entity SpawnWall(
    flecs::world world, z13::gameplay::SpawnNamer& namer, const z13::building::primitives::WallGeometry& wall,
    z13::station::GridId grid = z13::station::kMainGrid);

// Where a segment stands: the cell of its first node.
std::optional<Eigen::Vector3i> WallSegmentCell(flecs::world world, z13::station::WallId segment_id);
// The grid of the segment (that of its first node), the main one if it has none.
z13::station::GridId WallSegmentGrid(flecs::world world, z13::station::WallId segment_id);

// The id of the node at `cell` of `grid`, spawning one when there is none.
z13::station::WallId WallNodeAt(flecs::world world, z13::gameplay::SpawnNamer& namer, const Eigen::Vector3i& cell,
                                z13::station::GridId grid = z13::station::kMainGrid);

std::optional<flecs::entity> FindWallNode(flecs::world world, z13::station::WallId id);
std::optional<flecs::entity> FindWallSegment(flecs::world world, z13::station::WallId id);
std::optional<flecs::entity> FindWallOpening(flecs::world world, z13::station::WallId id);

// The wall of `segment` with its nodes at `cell_a` and `cell_b`, and its openings.
JointWall JointWallOf(
    const z13::station::WallSegment& segment, const Eigen::Vector3i& cell_a, const Eigen::Vector3i& cell_b,
    std::vector<z13::station::WallOpening> openings = {});

flecs::entity SpawnWallOpening(
    flecs::world world, z13::gameplay::SpawnNamer& namer, const z13::station::WallOpening& opening);

// The ids of the segments that have an opening.
std::set<z13::station::WallId> SegmentsWithOpenings(flecs::world world);

// Every segment with its nodes' cells, in segment id order.
std::vector<JointWall> JointWallsOf(flecs::world world);

// The same with `wall` added, joined to the nodes standing at its ends (ids of its own, for a
// wall that is not spawned yet, above all existing ones).
std::vector<JointWall> JointWallsWith(flecs::world world, const z13::building::primitives::WallGeometry& wall);

// Removes a segment with its openings, and the nodes no other segment uses.
void DestroyWallSegment(flecs::world world, z13::station::WallId segment_id);

}  // namespace z13::building
