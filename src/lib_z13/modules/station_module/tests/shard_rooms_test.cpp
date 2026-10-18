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
#include <optional>
#include <string>

#include <Eigen/Dense>

#include <rooms/room_cache.h>
#include <z13_tests/shipped_station.h>

#include "../../z13_module/tests/support/z13_test_world.h"

// The shard station (docs/skew-station-plan.md).
namespace z13::station {
namespace {

using z13::testing::Z13TestWorld;

// Cells of the shipped blueprint: the middle of each hall on deck 2, the same x and y on decks 1 and 3.
const Eigen::Vector3i kHallA {128, 80, 18};
const Eigen::Vector3i kHallB {224, 80, 18};
const Eigen::Vector3i kCargoA {128, 80, 6};
const Eigen::Vector3i kCargoB {224, 80, 6};
const Eigen::Vector3i kLoft {128, 80, 30};
const Eigen::Vector3i kObservatory {224, 80, 30};
// 45 degrees from the box corner (80, 16): the filled side has u + v = 25 and the air side 33.
const Eigen::Vector3i kHallAFilled {92, 29, 18};
const Eigen::Vector3i kHallAAir {96, 33, 18};
// 2:3 from the box corner (176, 16): the filled side has 3u + 2v = 68 and the air side 88.
const Eigen::Vector3i kHallBFilled {186, 34, 18};
const Eigen::Vector3i kHallBAir {190, 38, 18};

class ShardRoomsTest : public ::testing::Test {
 protected:
  ShardRoomsTest() : world_({std::string(z13::testing::kStationSceneArg), std::string(z13::testing::kShardScene)}) {
    world_.Tick();
  }

  const rooms::RoomGraph& Graph() { return world_.World().get<rooms::RoomCache>().Current()->get(); }

  std::optional<rooms::RoomId> RoomAt(const Eigen::Vector3i& cell) { return Graph().RoomAt(cell); }

  bool InARoom(const Eigen::Vector3i& cell) {
    const auto room = RoomAt(cell);
    return room.has_value() && *room != rooms::kVacuumRoom;
  }

  Z13TestWorld world_;
};

TEST_F(ShardRoomsTest, EveryHallAndDeckBoxIsARoom) {
  for (const Eigen::Vector3i& cell : {kHallA, kHallB, kCargoA, kCargoB, kLoft, kObservatory}) {
    EXPECT_TRUE(InARoom(cell)) << cell.transpose();
  }
}

TEST_F(ShardRoomsTest, ARampOpensTheDeckBelowIntoTheHall) {
  EXPECT_EQ(RoomAt(kCargoA), RoomAt(kHallA));
  EXPECT_EQ(RoomAt(kHallB), RoomAt(kObservatory));
}

TEST_F(ShardRoomsTest, DecksWithoutARampBetweenThemAreSeparateRooms) {
  EXPECT_NE(RoomAt(kHallA), RoomAt(kLoft));
  EXPECT_NE(RoomAt(kHallB), RoomAt(kCargoB));
  EXPECT_NE(RoomAt(kCargoA), RoomAt(kCargoB));
}

TEST_F(ShardRoomsTest, ADoorJoinsTheHalls) {
  const auto a = RoomAt(kHallA);
  const auto b = RoomAt(kHallB);
  ASSERT_TRUE(a && b);

  const auto& portals = Graph().portals;
  EXPECT_TRUE(std::ranges::any_of(portals, [&](const rooms::Portal& portal) {
    return std::min(portal.a, portal.b) == std::min(*a, *b) && std::max(portal.a, portal.b) == std::max(*a, *b);
  }));
}

TEST_F(ShardRoomsTest, AHallsCornerIsFilledUpToItsSlantedFace) {
  EXPECT_FALSE(InARoom(kHallAFilled));
  EXPECT_EQ(RoomAt(kHallAAir), RoomAt(kHallA));
  EXPECT_FALSE(InARoom(kHallBFilled));
  EXPECT_EQ(RoomAt(kHallBAir), RoomAt(kHallB));
}

}  // namespace
}  // namespace z13::station
