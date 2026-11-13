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

#include <z13/components/station/block.h>
#include <z13/components/station/conduit.h>
#include <z13/components/station/dock.h>
#include <z13/components/station/grid.h>
#include <z13/components/station/wall.h>
#include <primitives/palette_fwd.h>
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

// A grid of a blueprint beside the main one: its pose, and its blocks and walls in its own cells (the blocks name it in
// `Block::grid`).
struct BlueprintGridPart {
  z13::station::Grid grid;
  std::vector<z13::station::Block> blocks;
  std::vector<WallGeometry> walls;
  std::vector<std::vector<z13::station::WallOpening>> openings;
};

// The grids of a blueprint in the order it lists them, naming the first whose id is the main grid's or repeats, or
// whose block lacks a palette primitive.
std::expected<std::vector<BlueprintGridPart>, std::string> ParseBlueprintGrids(
    std::string_view json, const Palette& palette);

// The docks of a blueprint, naming the first whose grid is neither the main grid nor one of ParseBlueprintGrids's.
std::expected<std::vector<z13::station::Dock>, std::string> ParseBlueprintDocks(std::string_view json);

// A conduit run of a blueprint, its kind still a name: the kinds are the conduit module's.
struct BlueprintConduit {
  std::string kind;
  z13::station::GridId grid = z13::station::kMainGrid;
  Eigen::Vector3i cell = Eigen::Vector3i::Zero();
  z13::conduit::ConduitAxis axis {};
  int length = 1;
};

// The conduit runs of a blueprint, naming the first whose grid is neither the main grid nor one of ParseBlueprintGrids's,
// or whose length is not positive.
std::expected<std::vector<BlueprintConduit>, std::string> ParseBlueprintConduits(std::string_view json);

// A lift of a blueprint, its cabin's primitive still a name.
struct BlueprintLift {
  std::string primitive;
  Eigen::Vector3i size = Eigen::Vector3i::Zero();
  Eigen::Vector3i lower = Eigen::Vector3i::Zero();
  Eigen::Vector3i upper = Eigen::Vector3i::Zero();
  Eigen::Vector3i lower_button = Eigen::Vector3i::Zero();
  Eigen::Vector3i upper_button = Eigen::Vector3i::Zero();
  uint32_t trip_ticks {};
};

// The lifts of a blueprint, naming the first whose trip takes no ticks.
std::expected<std::vector<BlueprintLift>, std::string> ParseBlueprintLifts(std::string_view json);

std::expected<bool, std::string> ParseBlueprintDoorsOpen(std::string_view json);

}  // namespace z13::building::primitives
