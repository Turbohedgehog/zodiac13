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
#include <string>
#include <string_view>
#include <vector>

#include <z13/components/block.h>
#include <z13/components/wall.h>
#include <primitives/palette.h>
#include <primitives/wall_panel.h>

namespace z13::building::primitives {

// Parses blueprint JSON (blueprint.fbs) into blocks of `palette`, naming the first block
// whose primitive the palette lacks. Placement isn't checked here: that's ValidateBuild's.
std::expected<std::vector<z13::station::Block>, std::string> ParseBlueprint(
    std::string_view json, const Palette& palette);

// The wall segments of a blueprint, apart from its blocks: each between nodes at two cells, which
// walls meeting at one cell share. Their panels are derived from them when the scene is placed.
std::expected<std::vector<WallGeometry>, std::string> ParseBlueprintWalls(std::string_view json);

// The openings cut in each wall of ParseBlueprintWalls, in the same order (an empty list for a wall without).
// Their ids and segment ids are left for the scene to give.
std::expected<std::vector<std::vector<z13::station::WallOpening>>, std::string> ParseBlueprintOpenings(
    std::string_view json);

}  // namespace z13::building::primitives
