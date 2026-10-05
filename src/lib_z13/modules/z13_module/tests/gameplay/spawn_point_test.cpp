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

#include <Eigen/Dense>

#include <lib_core/state/world_state.h>
#include <lib_core/utils/math.h>

#include <z13/components/station.h>
#include <z13_module/gameplay/gameplay_entities.h>

#include "../support/z13_test_world.h"

namespace z13::gameplay {
namespace {

using z13::testing::Z13TestWorld;

const Eigen::Vector3f kFirstPoint {10.f, 0.f, 0.f};
const Eigen::Vector3f kSecondPoint {20.f, 0.f, 0.f};

void AddSpawnPoint(flecs::world world, const Eigen::Vector3f& position) {
  Eigen::Matrix4f transform = Eigen::Matrix4f::Identity();
  z13::math::SetTranslation(position, transform);
  world.entity().add<z13::flecs_tools::StateEntity>().set(transform).add<z13::station::SpawnPoint>();
}

Eigen::Vector3f PositionOf(flecs::entity player) {
  return z13::math::ExtractTranslation<float>(player.get<Eigen::Matrix4f>());
}

TEST(SpawnPointTest, PlayersTakeFreeSpawnPointsInOrderThenShareThemById) {
  Z13TestWorld test_world;
  test_world.Tick();
  AddSpawnPoint(test_world.World(), kFirstPoint);
  AddSpawnPoint(test_world.World(), kSecondPoint);

  EXPECT_TRUE(PositionOf(SpawnPlayer(test_world.World(), 1)).isApprox(kFirstPoint));
  EXPECT_TRUE(PositionOf(SpawnPlayer(test_world.World(), 2)).isApprox(kSecondPoint));
  // Both taken: the id modulo the point count picks one.
  EXPECT_TRUE(PositionOf(SpawnPlayer(test_world.World(), 3)).isApprox(kSecondPoint));
  EXPECT_TRUE(PositionOf(SpawnPlayer(test_world.World(), 4)).isApprox(kFirstPoint));
}

TEST(SpawnPointTest, WithoutSpawnPointsPlayersAreSpacedAlongX) {
  Z13TestWorld test_world;
  test_world.Tick();

  EXPECT_FLOAT_EQ(PositionOf(SpawnPlayer(test_world.World(), 2)).x(), 2.f * kSpawnSpacing);
}

}  // namespace
}  // namespace z13::gameplay
