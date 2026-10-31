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

#include <Eigen/Dense>

#include <z13/components/block.h>
#include <z13/components/block_changes.h>
#include <z13/components/door.h>
#include <z13/components/scene_mesh.h>
#include <z13_tests/shipped_station.h>

#include "../../building_module/tests/support/station_builders.h"
#include "../../z13_module/tests/support/z13_test_world.h"
#include "../src/door_leaf_mesh.h"

namespace z13::door_render {
namespace {

using z13::render::SceneMesh;
using z13::station::Block;
using z13::station::DoorState;
using z13::station::kDoorOpen;

constexpr uint32_t kDoorId = 3;
constexpr float kCell = z13::station::kCellSize;
constexpr float kTolerance = 1e-5f;

const Block kDoor {.spec = {.type_id = kDoorId, .size = {6, 1, 10}}, .cell = {10, 10, 10}};

float HeightOf(const SceneMesh& mesh) {
  const auto [lowest, highest] =
      std::ranges::minmax_element(mesh.positions, {}, [](const Eigen::Vector3f& p) { return p.z(); });
  return highest->z() - lowest->z();
}

float TopOf(const SceneMesh& mesh) {
  return std::ranges::max_element(mesh.positions, {}, [](const Eigen::Vector3f& p) { return p.z(); })->z();
}

TEST(DoorLeafMeshTest, AShutLeafFillsTheOpeningAndRollsUpIntoItsTop) {
  const auto palette = z13::testing::ShippedPalette();
  ASSERT_TRUE(palette.has_value()) << palette.error();

  const auto shut = BuildLeafMesh(kDoor, 0, std::cref(*palette));
  const auto half = BuildLeafMesh(kDoor, kDoorOpen / 2, std::cref(*palette));

  ASSERT_TRUE(shut.has_value());
  ASSERT_TRUE(half.has_value());
  EXPECT_EQ(shut->positions.size(), shut->normals.size());
  EXPECT_NEAR(HeightOf(*half), HeightOf(*shut) / 2.f, kTolerance);
  EXPECT_NEAR(TopOf(*half), TopOf(*shut), kTolerance) << "it rolls up into the top, which stays";
  EXPECT_EQ(shut->color, palette->Find(kDoorId)->get().material.first);
}

TEST(DoorLeafMeshTest, AFullyOpenDoorAndABlockWithoutALeafHaveNone) {
  const auto palette = z13::testing::ShippedPalette();
  ASSERT_TRUE(palette.has_value()) << palette.error();
  const Block wall {.spec = {.type_id = 2, .size = {8, 1, 8}}};

  EXPECT_FALSE(BuildLeafMesh(kDoor, kDoorOpen, std::cref(*palette)).has_value());
  EXPECT_FALSE(BuildLeafMesh(wall, 0, std::cref(*palette)).has_value());
}

TEST(DoorLeafSystemTest, ADoorGetsItsLeafMeshAndItShrinksAsTheDoorOpens) {
  z13::testing::Z13TestWorld world = z13::testing::StationWorld();
  world.Tick();
  const flecs::entity door = z13::station::SpawnBlock(world.World().entity(), kDoor);
  z13::testing::Ticks(world, 3);
  ASSERT_TRUE(door.has<SceneMesh>());
  const SceneMesh shut = door.get<SceneMesh>();

  door.set(DoorState {.position = kDoorOpen / 2, .open_wanted = true, .leaf_blocks = true});
  world.Tick();

  ASSERT_TRUE(door.has<SceneMesh>());
  EXPECT_NEAR(HeightOf(door.get<SceneMesh>()), HeightOf(shut) / 2.f, kTolerance * 10.f);
  EXPECT_GT(door.get<SceneMesh>().revision, shut.revision);

  door.set(DoorState {.position = kDoorOpen, .open_wanted = true, .leaf_blocks = false});
  world.Tick();
  EXPECT_FALSE(door.has<SceneMesh>());
}

}  // namespace
}  // namespace z13::door_render
