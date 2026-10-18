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

#include <flecs.h>

#include <z13/components/gameplay.h>
#include <z13/components/wall.h>

namespace z13::building {

// A node at `cell` named by the next id of `counters`.
flecs::entity SpawnWallNode(flecs::world world, z13::gameplay::IdCounters& counters, const Eigen::Vector3i& cell);

// A segment between two nodes (by id); its panel block follows in WallPanelSystem.
flecs::entity SpawnWallSegment(
    flecs::world world, z13::gameplay::IdCounters& counters, const z13::station::WallSegment& segment);

std::optional<flecs::entity> FindWallNode(flecs::world world, uint32_t id);
std::optional<flecs::entity> FindWallSegment(flecs::world world, uint32_t id);

// Removes a segment, and the nodes no other segment uses.
void DestroyWallSegment(flecs::world world, uint32_t segment_id);

}  // namespace z13::building
