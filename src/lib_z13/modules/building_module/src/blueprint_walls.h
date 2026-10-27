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

#include <expected>
#include <span>
#include <string>
#include <vector>

#include <flecs.h>

#include <z13/components/block.h>
#include <z13/components/gameplay.h>
#include <primitives/palette.h>
#include <primitives/wall_panel.h>
#include <z13_settings/building_tuning.h>

namespace z13::building {

// The panels the walls of a blueprint stand as: nodes at one cell are one node, and walls meeting
// at one are stitched along a seam, as WallPanelSystem will derive them.
std::expected<std::vector<z13::station::Block>, std::string> BlueprintPanels(
    std::span<const z13::building::primitives::WallGeometry> walls, const z13::building::primitives::Palette& palette,
    const BuildingTuning& tuning);

// Nodes and segments for the walls of a blueprint; their panels follow in WallPanelSystem.
void SpawnBlueprintWalls(
    flecs::world world, z13::gameplay::IdCounters& counters,
    std::span<const z13::building::primitives::WallGeometry> walls);

}  // namespace z13::building
