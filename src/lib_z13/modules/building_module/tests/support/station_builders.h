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

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <Eigen/Dense>
#include <flecs.h>

#include <lib_core/state/state_markers.h>
#include <lib_core/utils/math.h>
#include <z13/components/building.h>
#include <z13/components/block.h>
#include <z13_module/gameplay/gameplay_entities.h>

#include <z13_tests/shipped_station.h>

#include "../../../z13_module/tests/support/building_test_helpers.h"
#include "../../../z13_module/tests/support/z13_test_world.h"

namespace z13::testing {

inline std::vector<std::string> SiteArgs() {
  return {std::string(kStationSceneArg), std::string(kSiteScene)};
}

inline std::vector<std::string> WithServerArg(std::vector<std::string> args) {
  args.emplace_back(kServerArg);
  return args;
}

inline Z13TestWorld StationWorld() {return Z13TestWorld(SiteArgs());
}

// assets/station/palette.json's primitives: ids and palette window slots, in its order.
inline constexpr uint32_t kFloorId = 1;
inline constexpr uint32_t kWallId = 2;
inline constexpr uint32_t kDoorId = 3;
inline constexpr uint32_t kWindowId = 4;
inline constexpr uint32_t kSlopeId = 5;
inline constexpr size_t kFloorSlot = 0;
inline constexpr size_t kDoorSlot = 2;
inline constexpr size_t kSlopeSlot = 4;
// The Building action group joins a frame after the tool is out.
inline constexpr int kToolSettleTicks = 3;

inline void Ticks(Z13TestWorld& test_world, int count) {
  for (int i = 0; i < count; ++i) {
    test_world.Tick();
  }
}

// The local player with the build tool taken through input, its Building group joined.
inline flecs::entity LocalBuilder(Z13TestWorld& test_world) {
  test_world.Tick();
  EnterBuildMode(test_world);
  Ticks(test_world, kToolSettleTicks);
  return test_world.Player();
}

inline Eigen::Matrix4f Facing(const Eigen::Vector3f& position, const Eigen::Vector3f& forward) {
  Eigen::Matrix4f transform = Eigen::Matrix4f::Identity();
  transform.block<3, 3>(0, 0) =
      Eigen::Quaternionf::FromTwoVectors(Eigen::Vector3f::UnitX(), forward.normalized()).toRotationMatrix();
  z13::math::SetTranslation(position, transform);
  return transform;
}

// An extra player driven by its transform and request tags, so it can look anywhere
// without mouse input.
inline flecs::entity AddBuilder(Z13TestWorld& test_world, uint32_t id, const Eigen::Matrix4f& transform) {
  flecs::entity builder = HoldBuildTool(z13::gameplay::SpawnPlayer(test_world.World(), id).set(transform));
  // The kit, the brush and the tool's BlockBrush appear on the first frame.
  test_world.Tick();
  return builder;
}

// A 24x12 cell wall 16 cells ahead of the player, its thin side facing them, centred on their eye.
struct WallAhead {
  flecs::entity entity;
  z13::station::Block block;
};

inline WallAhead AddWallAhead(Z13TestWorld& test_world, flecs::entity player) {
  const Eigen::Vector3i eye =
      (z13::math::ExtractTranslation<float>(player.get<Eigen::Matrix4f>()) / z13::station::kCellSize)
          .array().floor().cast<int>();
  const z13::station::Block wall {
      .spec = {.type_id = kWallId, .size = {24, 1, 12}, .orientation = z13::station::Orientation::kFacePosYUpPosZ},
      .cell = {eye.x() + 16, eye.y() - 12, eye.z() - 6}};
  const flecs::entity entity = test_world.World().entity("TestWall").add<z13::flecs_tools::StateEntity>().set(wall);
  Ticks(test_world, 2);
  return {entity, wall};
}

inline std::vector<z13::station::Block> BlocksOfType(flecs::world world, uint32_t type_id) {
  std::vector<z13::station::Block> blocks;
  world.query_builder<const z13::station::Block>().build().each([&](const z13::station::Block& block) {
    if (block.spec.type_id == type_id) {
      blocks.push_back(block);
    }
  });
  return blocks;
}

}  // namespace z13::testing
