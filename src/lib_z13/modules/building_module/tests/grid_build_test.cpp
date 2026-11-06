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

#include <optional>

#include <Eigen/Dense>

#include <input_config_generated.h>
#include <lib_core/state/world_state.h>
#include <lib_core/utils/math.h>
#include <grid/block_index.h>
#include <z13/components/gameplay/player.h>
#include <z13/components/station/block.h>
#include <z13/components/station/block_changes.h>
#include <z13/components/station/building.h>
#include <z13/components/station/grid.h>
#include <z13/components/station/grid_pose.h>

#include "../../z13_module/tests/support/building_test_helpers.h"
#include "../../z13_module/tests/support/z13_test_world.h"

// Building and destroying in a posed grid: the ray and the brush work in the cells of the grid the ray meets first.
namespace z13::building {
namespace {

namespace ft = z13::flecs_tools;
using z13::station::Block;
using z13::station::Grid;
using z13::station::GridId;
using z13::testing::Click;
using z13::testing::EnterBuildMode;

constexpr GridId kDistrict = 1;
// A quarter turn swaps the axes, so the entry face is easy to name; the tilt makes the pose not a pure swap.
constexpr float kQuarterTurnDegrees = 90.f;
constexpr float kSmallTiltDegrees = 3.f;
// How far along the player's ray (0 at the eye, 1 at the brush) the district block is put.
constexpr float kBlockAlongRay = 0.5f;

struct Ray {
  Eigen::Vector3f eye = Eigen::Vector3f::Zero();
  Eigen::Vector3f direction = Eigen::Vector3f::UnitX();
  float length {};
};

Ray PlayersRay(flecs::world world) {
  Ray ray;
  world.query_builder<const z13::gameplay::Player, const Eigen::Matrix4f>().build().each(
      [&ray](const z13::gameplay::Player&, const Eigen::Matrix4f& transform) {
        ray.eye = z13::math::ExtractTranslation<float>(transform);
      });
  world.query_builder<const Brush, const Eigen::Matrix4f>().build().each(
      [&ray](const Brush&, const Eigen::Matrix4f& transform) {
        const Eigen::Vector3f toward = z13::math::ExtractTranslation<float>(transform) - ray.eye;
        ray.length = toward.norm();
        ray.direction = toward / ray.length;
      });
  return ray;
}

size_t IndexedIn(flecs::world world, const char* grid_name) {
  return world.lookup(grid_name).get<grid::BlockIndex>().Size();
}

class GridBuildTest : public ::testing::Test {
 protected:
  void SetUp() override {
    EnterBuildMode(world_);
    ray_ = PlayersRay(world_.World());
    ASSERT_GT(ray_.length, 0.f);
  }

  // A district posed so that the middle of the ray is in its returned cell.
  Block MakeDistrict(float yaw, float pitch) {
    flecs::world world = world_.World();
    Grid pose {.id = kDistrict, .yaw_degrees = yaw, .pitch_degrees = pitch};
    const Eigen::Vector3f target = ray_.eye + ray_.direction * (ray_.length * kBlockAlongRay);
    pose.origin = (target / z13::station::kCellSize).array().floor().cast<int>();
    world.entity("District").add<ft::StateEntity>().set(pose);
    const Eigen::Vector3f local = z13::station::GridToWorld(pose).inverse() * target / z13::station::kCellSize;
    return {.spec = z13::station::CubeSpec(), .cell = local.array().floor().cast<int>(), .grid = kDistrict};
  }

  flecs::entity AddDistrictBlock(const Block& block) {
    const flecs::entity entity = z13::station::SpawnBlock(world_.World().entity("DistrictBlock"), block);
    world_.Tick();
    return entity;
  }

  std::optional<Block> Newest(GridId grid, const flecs::entity except) {
    std::optional<Block> found;
    world_.World().query_builder<const Block>().build().each([&](flecs::entity e, const Block& block) {
      if (block.grid == grid && e != except) {
        found = block;
      }
    });
    return found;
  }

  z13::testing::Z13TestWorld world_;
  Ray ray_;
};

TEST_F(GridBuildTest, ABlockIsBuiltAgainstTheFaceOfAPosedGridTheRayMeets) {
  const flecs::entity placed = AddDistrictBlock(MakeDistrict(kQuarterTurnDegrees, 0.f));
  const Block district_block = placed.get<Block>();
  ASSERT_EQ(IndexedIn(world_.World(), "District"), 1u);

  Click(world_, z13::fbs::input::Keycode::MOUSE_BUTTON_LEFT);

  const std::optional<Block> built = Newest(kDistrict, placed);
  ASSERT_TRUE(built.has_value());
  // The ray runs along the world's +X, which is the grid's -Y: it enters the grid block by its +Y face.
  EXPECT_EQ(built->cell, district_block.cell + Eigen::Vector3i::UnitY());
  EXPECT_EQ(IndexedIn(world_.World(), "District"), 2u);
  EXPECT_EQ(z13::station::MainGridEntity(world_.World()).get<grid::BlockIndex>().Size(), 0u);
}

TEST_F(GridBuildTest, ABlockBuiltInATiltedGridIsOnTheFreeCellNextToWhatTheRayHit) {
  const flecs::entity placed = AddDistrictBlock(MakeDistrict(kQuarterTurnDegrees, kSmallTiltDegrees));

  Click(world_, z13::fbs::input::Keycode::MOUSE_BUTTON_LEFT);

  const std::optional<Block> built = Newest(kDistrict, placed);
  ASSERT_TRUE(built.has_value());
  EXPECT_EQ((built->cell - placed.get<Block>().cell).cwiseAbs().sum(), 1);
}

TEST_F(GridBuildTest, TheNearerOfTwoGridsTheRayMeetsTakesTheBuild) {
  const Block district = MakeDistrict(kQuarterTurnDegrees, 0.f);
  // A main grid block behind the district one, on the same ray.
  const Eigen::Vector3f behind = ray_.eye + ray_.direction * (ray_.length * (kBlockAlongRay + 0.25f));
  const Eigen::Vector3i behind_cell = (behind / z13::station::kCellSize).array().floor().cast<int>();
  z13::station::SpawnBlock(world_.World().entity("MainBlock"), {.spec = z13::station::CubeSpec(), .cell = behind_cell});
  const flecs::entity placed = AddDistrictBlock(district);

  Click(world_, z13::fbs::input::Keycode::MOUSE_BUTTON_LEFT);

  EXPECT_TRUE(Newest(kDistrict, placed).has_value());
  EXPECT_EQ(IndexedIn(world_.World(), "District"), 2u);
  EXPECT_EQ(z13::station::MainGridEntity(world_.World()).get<grid::BlockIndex>().Size(), 1u);
}

TEST_F(GridBuildTest, DestroyingTakesTheBlockOfThePosedGridTheRayMeets) {
  const flecs::entity placed = AddDistrictBlock(MakeDistrict(kQuarterTurnDegrees, kSmallTiltDegrees));

  Click(world_, z13::fbs::input::Keycode::MOUSE_BUTTON_RIGHT);

  EXPECT_FALSE(placed.is_alive());
  EXPECT_EQ(IndexedIn(world_.World(), "District"), 0u);
}

TEST_F(GridBuildTest, ABlockCutOutOfAPosedGridLeavesRemaindersInThatGrid) {
  const Block district = MakeDistrict(kQuarterTurnDegrees, 0.f);
  Block wide = district;
  wide.spec.size = {3, 3, 3};
  wide.cell -= Eigen::Vector3i::Ones();
  const flecs::entity placed = AddDistrictBlock(wide);

  world_.World().query_builder<const z13::gameplay::Player>().build().each(
      [](flecs::entity player, const z13::gameplay::Player&) { player.add<CutModifier>(); });
  Click(world_, z13::fbs::input::Keycode::MOUSE_BUTTON_LEFT);

  size_t remainders {};
  world_.World().query_builder<const Block>().build().each([&](flecs::entity e, const Block& block) {
    remainders += block.grid == kDistrict && e != placed ? 1 : 0;
    EXPECT_NE(block.grid, z13::station::kMainGrid);
  });
  EXPECT_GT(remainders, 0u);
}

}  // namespace
}  // namespace z13::building
