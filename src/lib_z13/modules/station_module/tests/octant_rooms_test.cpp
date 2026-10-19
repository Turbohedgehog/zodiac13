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
#include <set>
#include <string>

#include <Eigen/Dense>

#include <rooms/room_cache.h>
#include <z13_tests/shipped_station.h>

#include "../../z13_module/tests/support/z13_test_world.h"

// The Octant station, converted from the Blender map by docs/station-layout/generate_octant.py.
namespace z13::station {
namespace {

using z13::testing::Z13TestWorld;

// Cells of the shipped blueprint, one cell above the floor in the middle of a room.
const Eigen::Vector3i kBridge {376, 536, 3};
const Eigen::Vector3i kAtrium {376, 312, 3};
const Eigen::Vector3i kMainCorridor {376, 480, 3};

class OctantRoomsTest : public ::testing::Test {
 protected:
  OctantRoomsTest() : world_({std::string(z13::testing::kStationSceneArg), std::string(z13::testing::kOctantScene)}) {
    world_.Tick();
  }

  const rooms::RoomGraph& Graph() { return world_.World().get<rooms::RoomCache>().Current()->get(); }

  std::optional<rooms::RoomId> RoomAt(const Eigen::Vector3i& cell) { return Graph().RoomAt(cell); }

  Z13TestWorld world_;
};

// The Blender map has 162 rooms; a few of its partitions meet no door and split a room.
constexpr size_t kMapRooms = 162;

TEST_F(OctantRoomsTest, EveryRoomOfTheMapIsSealed) {
  EXPECT_GE(Graph().rooms.size(), kMapRooms);
  EXPECT_GE(Graph().portals.size(), kMapRooms);
}

TEST_F(OctantRoomsTest, EachSampledRoomIsItsOwnRoom) {
  const std::set<std::optional<rooms::RoomId>> ids {RoomAt(kBridge), RoomAt(kAtrium), RoomAt(kMainCorridor)};
  EXPECT_EQ(ids.size(), 3U);
  for (const Eigen::Vector3i& cell : {kBridge, kAtrium, kMainCorridor}) {
    const auto room = RoomAt(cell);
    EXPECT_TRUE(room.has_value() && *room != rooms::kVacuumRoom) << cell.transpose();
  }
}

}  // namespace
}  // namespace z13::station
