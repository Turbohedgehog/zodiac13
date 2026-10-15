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

#include <cstddef>
#include <string>

#include <lib_core/utils/thread_pool.h>
#include <rooms/room_builder.h>
#include <rooms/room_graph.h>
#include <rooms/room_update.h>
#include <z13_tests/pool_parallel.h>
#include <z13_tests/shipped_station.h>

// The room build on a thread pool gives the graph it gives in one thread (f/rooms-parallel).
namespace z13::station::rooms {
namespace {

void ExpectSameGraph(const RoomGraph& expected, const RoomGraph& actual, const std::string& scene) {
  EXPECT_EQ(actual.rooms, expected.rooms) << scene;
  EXPECT_EQ(actual.portals, expected.portals) << scene;
  EXPECT_EQ(actual.room_portals, expected.room_portals) << scene;
  EXPECT_EQ(actual.openings, expected.openings) << scene;
  EXPECT_EQ(actual.next_id, expected.next_id) << scene;
  ASSERT_EQ(actual.leaf_rooms.size(), expected.leaf_rooms.size()) << scene;
  for (const auto& [chunk, rooms] : expected.leaf_rooms) {
    ASSERT_TRUE(actual.leaf_rooms.contains(chunk)) << scene;
    EXPECT_EQ(*actual.leaf_rooms.at(chunk), *rooms) << scene;
  }
  for (const auto& [chunk, free] : expected.space.AllChunks()) {
    ASSERT_TRUE(actual.space.AllChunks().contains(chunk)) << scene;
    EXPECT_EQ(actual.space.AllChunks().at(chunk)->leaves, free->leaves) << scene;
  }
}

TEST(RoomParallelTest, EveryShippedStationBuildsAsInOneThread) {
  const auto palette = z13::testing::ShippedPalette();
  ASSERT_TRUE(palette.has_value()) << palette.error();
  const ThreadPool pool(z13::testing::kPoolThreads);
  for (const std::string& scene : z13::testing::ShippedScenes()) {
    const auto blocks = z13::testing::ShippedBlueprint(scene, *palette);
    ASSERT_TRUE(blocks.has_value()) << blocks.error();
    const RoomSources sources = z13::testing::RoomSourcesOf(*blocks, *palette);

    const auto serial = BuildRooms(sources);
    const auto pooled = BuildRooms(sources, kVacuumRoom + 1, {}, z13::testing::OnPool(pool));
    ASSERT_TRUE(serial.has_value()) << serial.error();
    ASSERT_TRUE(pooled.has_value()) << pooled.error();
    ExpectSameGraph(*serial, *pooled, scene);

    // An edit: the first sealing block torn down, the rest rebuilt around it.
    RoomSources edited = sources;
    edited.sealed.erase(edited.sealed.begin());
    const auto serial_update = UpdateRooms(*serial, edited, serial->next_id, {});
    const auto pooled_update = UpdateRooms(*pooled, edited, pooled->next_id, {}, z13::testing::OnPool(pool));
    ASSERT_TRUE(serial_update.has_value()) << serial_update.error();
    ASSERT_TRUE(pooled_update.has_value()) << pooled_update.error();
    ExpectSameGraph(*serial_update, *pooled_update, scene);
  }
}

}  // namespace
}  // namespace z13::station::rooms
