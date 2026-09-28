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

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include <Eigen/Dense>

#include <lib_core/math.h>

#include <z13/components/building.h>
#include <z13/components/gameplay.h>
#include <z13_module/gameplay/gameplay_entities.h>
#include <z13_tests/test_time.h>

#include "../support/z13_test_world.h"

// Same-tick requests from several players must resolve in player_id order, not in the
// ECS order that follows each participant's own entity creation history.
namespace z13::building {
namespace {

using z13::testing::kTestDeltaTime;
using z13::testing::Z13TestWorld;

std::map<std::string, Eigen::Vector3f> BlocksByName(flecs::world world) {
  std::map<std::string, Eigen::Vector3f> blocks;
  world.query_builder<const BasicBlock, const Eigen::Matrix4f>().build().each(
      [&](flecs::entity e, const BasicBlock&, const Eigen::Matrix4f& transform) {
        blocks[e.name().c_str()] = z13::math::ExtractTranslation<float>(transform);
      });
  return blocks;
}

// Spawns the players in `spawn_order` and has them all build on one tick, adding the
// requests in that same order, as the input system would.
std::map<std::string, Eigen::Vector3f> BuildSimultaneously(const std::vector<uint32_t>& spawn_order) {
  Z13TestWorld test_world;
  flecs::world world = test_world.World();

  std::vector<flecs::entity> players;
  for (const uint32_t id : spawn_order) {
    players.push_back(z13::gameplay::SpawnPlayer(world, id).add<BuildingTool>());
  }
  world.progress(kTestDeltaTime);

  for (flecs::entity player : players) {
    player.add<RequestBuildBlock>();
  }
  world.progress(kTestDeltaTime);
  return BlocksByName(world);
}

TEST(PlayerOrderTest, SameTickBuildsNameBlocksByPlayerIdRegardlessOfSpawnOrder) {
  const auto ascending = BuildSimultaneously({1, 2});
  const auto descending = BuildSimultaneously({2, 1});

  ASSERT_EQ(ascending.size(), 2u);
  ASSERT_EQ(descending.size(), ascending.size());
  for (const auto& [name, position] : ascending) {
    ASSERT_TRUE(descending.contains(name)) << name;
    EXPECT_TRUE(descending.at(name).isApprox(position, z13::testing::kTestEpsilon)) << name;
  }
}

}  // namespace
}  // namespace z13::building
