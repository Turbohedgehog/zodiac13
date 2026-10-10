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

#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <vector>

#include <Eigen/Dense>

#include <lib_core/state/world_state.h>

#include <z13/components/station.h>
#include <primitives/palette.h>
#include <z13_tests/shipped_station.h>

#include "../../../z13_module/tests/support/z13_test_world.h"

// Rooms built of shipped primitives, for the station module's tests.
namespace z13::station::testing {

inline constexpr std::string_view kFloor = "Floor";
inline constexpr std::string_view kWall = "Wall";
inline constexpr std::string_view kDoor = "Door";
inline constexpr std::string_view kWindow = "Window";

// Far from anything the site scene builds.
inline const Eigen::Vector3i kRoomCorner {30, 30, 5};
inline constexpr int kSide = 10;
inline constexpr int kHeight = 12;
// The door primitive's frame and its 4x8 opening.
inline constexpr int kDoorWidth = 6;
inline constexpr int kDoorHeight = 10;
inline constexpr int kDoorOpeningCells = 4 * 8;

inline uint32_t IdOf(std::string_view name) {
  return z13::testing::ShippedPalette()->Find(name)->get().id;
}

// Wall panels along y: the primitive's x runs along the world's y.
inline constexpr Orientation kAlongY = Orientation::kFacePosYUpPosZ;

inline Block At(std::string_view primitive, Eigen::Vector3i size, Orientation orientation, Eigen::Vector3i offset) {
  return {.spec = {.type_id = IdOf(primitive), .size = size, .orientation = orientation},
          .cell = kRoomCorner + offset};
}

// A sealed box whose inside is kSide x kSide x kHeight cells, with its lowest cell at (1, 1, 1).
inline std::vector<Block> SealedBox() {
  constexpr int outer = kSide + 2;
  return {
      At(kFloor, {outer, outer, 1}, {}, {0, 0, 0}),
      At(kFloor, {outer, outer, 1}, {}, {0, 0, kHeight + 1}),
      At(kWall, {outer, 1, kHeight}, {}, {0, 0, 1}),
      At(kWall, {outer, 1, kHeight}, {}, {0, outer - 1, 1}),
      At(kWall, {kSide, 1, kHeight}, kAlongY, {0, 1, 1}),
      At(kWall, {kSide, 1, kHeight}, kAlongY, {outer - 1, 1, 1}),
  };
}

// A partition at x = 5 with a door in it, leaving the box as two rooms.
inline std::vector<Block> DoorPartition() {
  return {
      At(kDoor, {kDoorWidth, 1, kDoorHeight}, kAlongY, {5, 1, 1}),
      At(kWall, {kSide - kDoorWidth, 1, kHeight}, kAlongY, {5, 1 + kDoorWidth, 1}),
      At(kWall, {kDoorWidth, 1, kHeight - kDoorHeight}, kAlongY, {5, 1, 1 + kDoorHeight}),
  };
}

inline flecs::entity AddBlock(z13::testing::Z13TestWorld& world, const Block& block) {
  // Named, as the game names its blocks: a restore matches state entities by name.
  const std::string name =
      std::format("RoomTest_{}_{}_{}_{}", block.spec.type_id, block.cell.x(), block.cell.y(), block.cell.z());
  return world.World().entity(name.c_str()).add<z13::flecs_tools::StateEntity>().set(block);
}

inline void AddBlocks(z13::testing::Z13TestWorld& world, const std::vector<Block>& blocks) {
  for (const Block& block : blocks) {
    AddBlock(world, block);
  }
}

}  // namespace z13::station::testing
