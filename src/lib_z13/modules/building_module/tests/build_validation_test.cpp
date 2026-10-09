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

#include <gtest/gtest.h>

#include <span>
#include <vector>

#include <primitives/palette.h>
#include <z13_tests/shipped_station.h>

#include "../src/block_entities.h"
#include "../src/build_validation.h"

namespace z13::building {
namespace {

using z13::building::grid::BlockIndex;
using z13::station::Block;

// assets/station/palette.json
constexpr uint32_t kWallId = 2;
constexpr uint32_t kSlopeId = 5;
constexpr uint32_t kSpawnPointId = 8;
constexpr uint32_t kUnknownId = 999;

z13::building::primitives::Palette ShippedPalette() {
  return z13::testing::ShippedPalette().value();
}

Block Wall(const Eigen::Vector3i& cell) {
  return {.spec = {.type_id = kWallId, .size = {8, 1, 8}}, .cell = cell};
}

Status Validate(
    const Block& block, const z13::building::primitives::Palette& palette, const BlockIndex& index,
    std::span<const PlayerSphere> players, std::span<const z13::building::primitives::CellBox> spawn_clearances,
    z13::station::BrushPreview::Kind kind = z13::station::BrushPreview::Kind::kBuild) {
  const BuildingTuning tuning;
  return ValidateBuild(
      block,
      {.palette = palette, .index = index, .tuning = tuning, .players = players, .spawn_clearances = spawn_clearances},
      kind);
}

TEST(BuildValidationTest, AcceptsAPrimitiveOnFreeCells) {
  const auto palette = ShippedPalette();
  BlockIndex index;
  index.Insert(1, {.min = {0, 0, 0}, .extent = {8, 1, 8}});

  EXPECT_TRUE(Validate(Wall({0, 1, 0}), palette, index, {}, {}).has_value());
}

TEST(BuildValidationTest, ACutInBuildMayTakeCellsThatAreTaken) {
  const auto palette = ShippedPalette();
  BlockIndex index;
  index.Insert(1, {.min = {0, 0, 0}, .extent = {8, 1, 8}});

  EXPECT_TRUE(Validate(Wall({0, 0, 0}), palette, index, {}, {}, z13::station::BrushPreview::Kind::kCutIn).has_value());
  EXPECT_FALSE(Validate(Wall({0, 0, 0}), palette, index, {}, {}).has_value());
}

TEST(BuildValidationTest, RefusesTakenCells) {
  const auto palette = ShippedPalette();
  BlockIndex index;
  index.Insert(1, {.min = {0, 0, 0}, .extent = {8, 1, 8}});

  EXPECT_FALSE(Validate(Wall({7, 0, 7}), palette, index, {}, {}).has_value());
}

TEST(BuildValidationTest, RefusesUnknownTypesAndSizesOutsideTheLimits) {
  const auto palette = ShippedPalette();
  const BlockIndex index;

  EXPECT_FALSE(Validate({.spec = {.type_id = kUnknownId, .size = {1, 1, 1}}}, palette, index, {}, {})
                   .has_value());
  // A wall is one cell thick; a slope has a fixed size.
  EXPECT_FALSE(Validate({.spec = {.type_id = kWallId, .size = {8, 2, 8}}}, palette, index, {}, {})
                   .has_value());
  EXPECT_FALSE(Validate({.spec = {.type_id = kSlopeId, .size = {4, 4, 5}}}, palette, index, {}, {})
                   .has_value());
}

// A player's sphere touching the block's cells would get stuck inside it.
TEST(BuildValidationTest, RefusesBuildingIntoAPlayer) {
  const auto palette = ShippedPalette();
  const BlockIndex index;
  const std::vector<PlayerSphere> inside {{.center = {1.f, 0.1f, 1.f}, .radius = 0.4f}};
  const std::vector<PlayerSphere> clear {{.center = {1.f, 1.f, 1.f}, .radius = 0.4f}};

  EXPECT_FALSE(Validate(Wall({0, 0, 0}), palette, index, inside, {}).has_value());
  EXPECT_TRUE(Validate(Wall({0, 0, 0}), palette, index, clear, {}).has_value());
}

// Players appear a meter above a spawn marker; a block there would spawn them inside it.
TEST(BuildValidationTest, KeepsTheSpaceAboveSpawnPointsClear) {
  const auto palette = ShippedPalette();
  const Block marker {.spec = {.type_id = kSpawnPointId, .size = {4, 4, 1}}, .cell = {0, 0, 0}};
  BlockIndex index;
  index.Insert(1, {.min = marker.cell, .extent = marker.spec.size});
  const std::vector<z13::building::primitives::CellBox> clearances {SpawnClearance(marker, BuildingTuning {})};

  EXPECT_FALSE(Validate(Wall({0, 2, 1}), palette, index, {}, clearances).has_value());
  // Above the clearance, and beside the marker, are fine.
  EXPECT_TRUE(Validate(Wall({0, 2, 9}), palette, index, {}, clearances).has_value());
  EXPECT_TRUE(Validate(Wall({0, 4, 1}), palette, index, {}, clearances).has_value());
}

TEST(BuildValidationTest, ANewSpawnPointNeedsRoomAboveIt) {
  const auto palette = ShippedPalette();
  BlockIndex index;
  index.Insert(1, {.min = {0, 0, 5}, .extent = {8, 8, 1}});
  const Block under_a_ceiling {.spec = {.type_id = kSpawnPointId, .size = {4, 4, 1}}, .cell = {0, 0, 0}};
  const Block in_the_open {.spec = {.type_id = kSpawnPointId, .size = {4, 4, 1}}, .cell = {10, 0, 0}};

  EXPECT_FALSE(Validate(under_a_ceiling, palette, index, {}, {}).has_value());
  EXPECT_TRUE(Validate(in_the_open, palette, index, {}, {}).has_value());
}

TEST(BuildValidationTest, KeepsTheLastSpawnPoint) {
  const auto palette = ShippedPalette();
  const Block spawn_point {.spec = {.type_id = kSpawnPointId, .size = {4, 4, 1}}};

  EXPECT_FALSE(ValidateDestroy(spawn_point, palette, 1).has_value());
  EXPECT_TRUE(ValidateDestroy(spawn_point, palette, 2).has_value());
  EXPECT_TRUE(ValidateDestroy(Wall({0, 0, 0}), palette, 1).has_value());
}

}  // namespace
}  // namespace z13::building
