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

#include <algorithm>
#include <set>
#include <string>
#include <vector>

#include <Eigen/Dense>

#include <bullet_module/bullet_components.h>

#include <lib_core/math.h>
#include <lib_core/world_json_store.h>
#include <lib_core/world_state_requests.h>

#include <z13/components/building.h>
#include <z13/components/gameplay.h>
#include <z13/components/input.h>
#include <z13_module/gameplay/camera_look.h>
#include <z13_module/gameplay/gameplay_entities.h>

#include "../support/building_test_helpers.h"
#include "../support/world_json_test_helpers.h"
#include "../support/z13_test_world.h"

// Saves and restores the real game state (blocks, camera, build mode) through
// WorldJsonStore, driving the world through the same input pipeline the game uses.
namespace z13::state {
namespace {

namespace ft = z13::flecs_tools;
using z13::building::BasicBlock;
using z13::building::Brush;
using z13::building::BuildingTool;
using z13::gameplay::LookAngles;
using z13::testing::Click;
using z13::testing::kTestDeltaTime;
using z13::testing::kTestEpsilon;
using z13::testing::Z13TestWorld;
using Keycode = z13::fbs::input::Keycode;

// Blocks are placed at the brush, which sits this far in front of the player (+X).
constexpr float kBrushDistance = 5.f;
constexpr float kBlockColumnX = 20.f;
constexpr float kBlockSpacing = 6.f;
// Yields yaw = -5 degrees (see input_pipeline_test).
constexpr float kMouseTestDeltaTime = 0.01f;
constexpr int kMouseTestDelta = 100;
constexpr float kMinAlignment = 0.999f;

Eigen::Vector3f BlockAt(int index) {
  return {kBlockColumnX, kBlockSpacing * static_cast<float>(index), 0.f};
}

Eigen::Matrix4f AtPosition(const Eigen::Vector3f& position) {
  Eigen::Matrix4f transform = Eigen::Matrix4f::Identity();
  z13::math::SetTranslation(position, transform);
  return transform;
}

void MovePlayerTo(Z13TestWorld& test_world, const Eigen::Vector3f& position) {
  test_world.Player().set(AtPosition(position));
  test_world.World().progress(kTestDeltaTime);
}

// Player faces +X with no look angles applied, so block/brush geometry is simple.
void BuildBlockAt(Z13TestWorld& test_world, const Eigen::Vector3f& position) {
  MovePlayerTo(test_world, position - Eigen::Vector3f(kBrushDistance, 0.f, 0.f));
  Click(test_world, Keycode::MOUSE_BUTTON_LEFT);
}

void DestroyBlockAt(Z13TestWorld& test_world, const Eigen::Vector3f& position) {
  MovePlayerTo(test_world, position - Eigen::Vector3f(kBrushDistance, 0.f, 0.f));
  Click(test_world, Keycode::MOUSE_BUTTON_RIGHT);
}

// Turns the camera by yaw -5 degrees through the mouse-look pipeline.
void LookRight(Z13TestWorld& test_world) {
  test_world.World().progress(kMouseTestDeltaTime);
  z13::input::MouseMoveEvent move_event;
  move_event.delta = {.x = kMouseTestDelta, .y = 0};
  test_world.EmitInput(move_event);
  test_world.World().progress(kMouseTestDeltaTime);
}

void ResetLook(Z13TestWorld& test_world) {
  test_world.Player().set(LookAngles{});
  test_world.Player().set(Eigen::Matrix4f(Eigen::Matrix4f::Identity()));
  test_world.World().progress(kTestDeltaTime);
}

std::vector<Eigen::Vector3f> BlockPositions(flecs::world& world) {
  std::vector<Eigen::Vector3f> positions;
  world.query_builder<const BasicBlock, const Eigen::Matrix4f>().build().each(
      [&](const BasicBlock&, const Eigen::Matrix4f& transform) {
        positions.push_back(z13::math::ExtractTranslation<float>(transform));
      });
  std::ranges::sort(positions, [](const Eigen::Vector3f& a, const Eigen::Vector3f& b) {
    return std::lexicographical_compare(a.data(), a.data() + 3, b.data(), b.data() + 3);
  });
  return positions;
}

std::set<std::string> BlockNames(flecs::world& world) {
  std::set<std::string> names;
  world.query_builder<const BasicBlock>().build().each(
      [&](flecs::entity e, const BasicBlock&) { names.insert(e.name().c_str()); });
  return names;
}

void ExpectSamePositions(
    const std::vector<Eigen::Vector3f>& expected, const std::vector<Eigen::Vector3f>& actual) {
  ASSERT_EQ(expected.size(), actual.size());
  for (size_t i = 0; i < expected.size(); ++i) {
    EXPECT_TRUE(expected[i].isApprox(actual[i], kTestEpsilon)) << "block " << i;
  }
}

std::string Save(Z13TestWorld& test_world) {
  const auto json = ft::WorldJsonStore::Save(test_world.World());
  EXPECT_TRUE(json.has_value()) << (json ? "" : json.error());
  return json.value_or("");
}

void Load(Z13TestWorld& test_world, const std::string& json) {
  const auto result = ft::WorldJsonStore::Load(test_world.World(), json);
  ASSERT_TRUE(result.has_value()) << result.error();
}

void Frames(Z13TestWorld& test_world, int count) {
  for (int i = 0; i < count; ++i) {
    test_world.World().progress(kTestDeltaTime);
  }
}

z13::gameplay::IdCounters Counters(Z13TestWorld& test_world) {
  return test_world.World().get<z13::gameplay::IdCounters>();
}

size_t Count(Z13TestWorld& test_world) {
  return static_cast<size_t>(test_world.World().count<BasicBlock>());
}

TEST(GameStateTest, RestoresBlocksCameraAndModeAfterFurtherEdits) {
  Z13TestWorld test_world;
  flecs::entity player = test_world.Player();
  z13::testing::EnterBuildMode(test_world);
  for (int i = 0; i < 3; ++i) {
    BuildBlockAt(test_world, BlockAt(i));
  }
  DestroyBlockAt(test_world, BlockAt(1));
  const auto saved_blocks = BlockPositions(test_world.World());
  ASSERT_EQ(saved_blocks.size(), 2u);

  LookRight(test_world);
  player.set(AtPosition({1.f, 2.f, 3.f}));
  test_world.World().progress(kMouseTestDeltaTime);
  const Eigen::Matrix4f saved_camera = player.get<Eigen::Matrix4f>();
  const LookAngles saved_look = player.get<LookAngles>();
  ASSERT_TRUE(player.has<BuildingTool>());
  const std::string json = Save(test_world);

  ResetLook(test_world);
  BuildBlockAt(test_world, BlockAt(3));
  BuildBlockAt(test_world, BlockAt(4));
  DestroyBlockAt(test_world, BlockAt(0));
  MovePlayerTo(test_world, {-7.f, 8.f, 9.f});
  z13::testing::ToggleBuildMode(test_world);
  ASSERT_FALSE(player.has<BuildingTool>());

  Load(test_world, json);

  ExpectSamePositions(saved_blocks, BlockPositions(test_world.World()));
  EXPECT_TRUE(player.get<Eigen::Matrix4f>().isApprox(saved_camera, kTestEpsilon));
  EXPECT_NEAR(player.get<LookAngles>().yaw_deg, saved_look.yaw_deg, kTestEpsilon);
  EXPECT_NEAR(player.get<LookAngles>().pitch_deg, saved_look.pitch_deg, kTestEpsilon);
  EXPECT_TRUE(player.has<BuildingTool>());
}

TEST(GameStateTest, SaveAfterLoadReproducesTheSameJson) {
  Z13TestWorld test_world;
  z13::testing::EnterBuildMode(test_world);
  BuildBlockAt(test_world, BlockAt(0));
  BuildBlockAt(test_world, BlockAt(1));
  LookRight(test_world);
  const std::string json = Save(test_world);

  Load(test_world, json);

  EXPECT_EQ(Save(test_world), json);
}

TEST(GameStateTest, RestoresBuildModeAndItsInputWhenSavedInBuildMode) {
  Z13TestWorld test_world;
  z13::testing::EnterBuildMode(test_world);
  const std::string json = Save(test_world);
  z13::testing::ToggleBuildMode(test_world);
  ASSERT_EQ(test_world.World().count<Brush>(), 0);

  Load(test_world, json);
  Frames(test_world, 2);

  EXPECT_TRUE(test_world.Player().has<BuildingTool>());
  EXPECT_EQ(test_world.World().count<Brush>(), 1);
  Click(test_world, Keycode::MOUSE_BUTTON_LEFT);
  EXPECT_EQ(Count(test_world), 1u);
}

TEST(GameStateTest, RestoresNormalModeAndItsInputWhenSavedOutsideBuildMode) {
  Z13TestWorld test_world;
  Frames(test_world, 1);
  const std::string json = Save(test_world);
  z13::testing::EnterBuildMode(test_world);
  ASSERT_EQ(test_world.World().count<Brush>(), 1);

  Load(test_world, json);
  Frames(test_world, 2);

  EXPECT_FALSE(test_world.Player().has<BuildingTool>());
  EXPECT_EQ(test_world.World().count<Brush>(), 0);
  Click(test_world, Keycode::MOUSE_BUTTON_LEFT);
  EXPECT_EQ(Count(test_world), 0u);
  z13::testing::ToggleBuildMode(test_world);
  EXPECT_TRUE(test_world.Player().has<BuildingTool>());
}

TEST(GameStateTest, RestoredOrientationDoesNotSnapBackOnTheNextMove) {
  Z13TestWorld test_world;
  LookRight(test_world);
  const Eigen::Vector3f forward = test_world.Player().get<Eigen::Matrix4f>().block<3, 3>(0, 0).col(0);
  const std::string json = Save(test_world);
  ResetLook(test_world);

  Load(test_world, json);
  const Eigen::Vector3f before =
      z13::math::ExtractTranslation<float>(test_world.Player().get<Eigen::Matrix4f>());
  test_world.EmitInput(z13::testing::KeyDown(Keycode::KEY_W));
  test_world.World().progress(kMouseTestDeltaTime);

  const Eigen::Vector3f moved =
      z13::math::ExtractTranslation<float>(test_world.Player().get<Eigen::Matrix4f>()) - before;
  ASSERT_GT(moved.norm(), kTestEpsilon);
  EXPECT_GT(moved.normalized().dot(forward), kMinAlignment);
}

TEST(GameStateTest, LoadRemovesLookAnglesTheSnapshotDoesNotHave) {
  Z13TestWorld test_world;
  const std::string json = Save(test_world);
  ASSERT_FALSE(test_world.Player().has<LookAngles>());
  LookRight(test_world);
  ASSERT_TRUE(test_world.Player().has<LookAngles>());

  Load(test_world, json);
  EXPECT_FALSE(test_world.Player().has<LookAngles>());

  Frames(test_world, 1);
  EXPECT_NEAR(test_world.Player().get<LookAngles>().yaw_deg, 0.f, kTestEpsilon);
}

TEST(GameStateTest, RestoredBlocksHavePhysicsAtTheRightPlace) {
  Z13TestWorld test_world;
  z13::testing::EnterBuildMode(test_world);
  BuildBlockAt(test_world, BlockAt(0));
  const std::string json = Save(test_world);
  DestroyBlockAt(test_world, BlockAt(0));
  ASSERT_EQ(Count(test_world), 0u);

  Load(test_world, json);
  Frames(test_world, 1);

  EXPECT_EQ(test_world.World().count<z13::bullet_module::RigidBody>(), 1);
  DestroyBlockAt(test_world, BlockAt(0));
  EXPECT_EQ(Count(test_world), 0u);
  EXPECT_EQ(test_world.World().count<z13::bullet_module::RigidBody>(), 0);
}

TEST(GameStateTest, BlockNamesStayUniqueAfterLoad) {
  Z13TestWorld test_world;
  z13::testing::EnterBuildMode(test_world);
  BuildBlockAt(test_world, BlockAt(0));
  BuildBlockAt(test_world, BlockAt(1));
  const std::string json = Save(test_world);
  BuildBlockAt(test_world, BlockAt(2));
  BuildBlockAt(test_world, BlockAt(3));

  Load(test_world, json);
  BuildBlockAt(test_world, BlockAt(4));
  BuildBlockAt(test_world, BlockAt(5));

  EXPECT_EQ(Count(test_world), 4u);
  EXPECT_EQ(BlockNames(test_world.World()).size(), 4u);
}

TEST(GameStateTest, NewBlocksSkipNamesTakenByALaggingCounter) {
  Z13TestWorld test_world;
  z13::testing::EnterBuildMode(test_world);
  BuildBlockAt(test_world, BlockAt(0));
  test_world.World().set<z13::gameplay::IdCounters>({});

  BuildBlockAt(test_world, BlockAt(1));

  EXPECT_EQ(Count(test_world), 2u);
  EXPECT_EQ(BlockNames(test_world.World()).size(), 2u);
}

TEST(GameStateTest, TransfersStateToAnotherWorld) {
  Z13TestWorld source;
  z13::testing::EnterBuildMode(source);
  BuildBlockAt(source, BlockAt(0));
  BuildBlockAt(source, BlockAt(1));
  LookRight(source);
  const std::string json = Save(source);

  Z13TestWorld target;
  Load(target, json);
  Frames(target, 1);

  ExpectSamePositions(BlockPositions(source.World()), BlockPositions(target.World()));
  EXPECT_EQ(BlockNames(source.World()), BlockNames(target.World()));
  EXPECT_TRUE(target.Player().get<Eigen::Matrix4f>().isApprox(
      source.Player().get<Eigen::Matrix4f>(), kTestEpsilon));
  EXPECT_TRUE(target.Player().has<BuildingTool>());
  EXPECT_EQ(Counters(target).last_block_id, Counters(source).last_block_id);
  EXPECT_EQ(Counters(target).last_player_id, Counters(source).last_player_id);
  // The settle frame above (Frames(target, 1)) advances SimulationClock.tick past what
  // was captured in `json`, same as any other state a live system could still touch
  // during that frame -- compare everything else byte-for-byte regardless.
  EXPECT_EQ(z13::testing::WithNormalizedSimulationTick(Save(target)), z13::testing::WithNormalizedSimulationTick(json));
}

// Only singletons that also have the State property are world state: Gameplay is not, IdCounters is.
TEST(GameStateTest, SingletonsAreNotSavedButCountersAre) {
  Z13TestWorld test_world;
  z13::testing::EnterBuildMode(test_world);
  BuildBlockAt(test_world, BlockAt(0));

  const std::string json = Save(test_world);

  EXPECT_EQ(json.find("Gameplay"), std::string::npos);
  EXPECT_NE(json.find("IdCounters"), std::string::npos);
  EXPECT_EQ(Counters(test_world).last_block_id, 1u);
}

TEST(GameStateTest, LoadingAnEmptySnapshotKeepsTheSingletons) {
  Z13TestWorld test_world;

  Load(test_world, R"({"version":1,"entities":[]})");
  Frames(test_world, 1);

  EXPECT_TRUE(test_world.World().has<z13::gameplay::Gameplay>());
  EXPECT_FALSE(test_world.Player());
}

TEST(GameStateTest, LoadRequestedThroughTheQueueIsAppliedNextFrame) {
  Z13TestWorld test_world;
  z13::testing::EnterBuildMode(test_world);
  BuildBlockAt(test_world, BlockAt(0));
  const std::string json = Save(test_world);
  DestroyBlockAt(test_world, BlockAt(0));

  ft::RequestLoadWorldState(test_world.World(), json);
  EXPECT_EQ(Count(test_world), 0u);
  test_world.World().progress(kTestDeltaTime);

  EXPECT_EQ(Count(test_world), 1u);
}

}  // namespace
}  // namespace z13::state
