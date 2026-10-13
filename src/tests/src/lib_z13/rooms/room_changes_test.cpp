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
#include <vector>

#include <rooms/room_builder.h>
#include <rooms/room_changes.h>
#include <rooms/room_update.h>

namespace z13::station::rooms {
namespace {

using z13::building::primitives::CellBox;

constexpr int kSide = 10;
constexpr int kHeight = 6;
constexpr int kChunkCells = 8;

// Two kSide x kSide rooms side by side along x, split by a wall at x = kSide + 1.
std::vector<CellBox> TwoRooms() {
  constexpr int outer_x = 2 * kSide + 3;
  constexpr int outer_y = kSide + 2;
  constexpr int tall = kHeight + 2;
  return {
      {.min = {0, 0, 0}, .extent = {outer_x, outer_y, 1}},
      {.min = {0, 0, tall - 1}, .extent = {outer_x, outer_y, 1}},
      {.min = {0, 0, 1}, .extent = {1, outer_y, kHeight}},
      {.min = {outer_x - 1, 0, 1}, .extent = {1, outer_y, kHeight}},
      {.min = {kSide + 1, 1, 1}, .extent = {1, kSide, kHeight}},
      {.min = {1, 0, 1}, .extent = {outer_x - 2, 1, kHeight}},
      {.min = {1, outer_y - 1, 1}, .extent = {outer_x - 2, 1, kHeight}},
  };
}

const Eigen::Vector3i kLeft {2, 5, 3};
const Eigen::Vector3i kRight {kSide + 5, 5, 3};

RoomGraph Build(const RoomSources& sources) {
  auto graph = BuildRooms(sources, kVacuumRoom + 1, {.chunk_cells = kChunkCells});
  EXPECT_TRUE(graph.has_value()) << graph.error();
  return graph.value_or(RoomGraph {});
}

RoomGraph Update(const RoomGraph& previous, const RoomSources& sources) {
  auto graph = UpdateRooms(previous, sources, previous.next_id, {.chunk_cells = kChunkCells});
  EXPECT_TRUE(graph.has_value()) << graph.error();
  return graph.value_or(RoomGraph {});
}

RoomId At(const RoomGraph& graph, const Eigen::Vector3i& cell) {
  return graph.RoomAt(cell).value_or(kVacuumRoom);
}

TEST(RoomChangesTest, AWallAcrossARoomChangesItAndAddsTheOtherPart) {
  RoomSources sources {.sealed = TwoRooms()};
  const RoomGraph before = Build(sources);
  sources.sealed.push_back({.min = {1, 4, 1}, .extent = {kSide, 1, kHeight}});

  const RoomGraph after = Update(before, sources);
  const RoomChanges changes = ChangesBetween(before, after);

  EXPECT_EQ(changes.changed, std::vector {At(before, kLeft)});
  EXPECT_EQ(changes.added, std::vector {At(after, {2, 7, 3})});
  EXPECT_TRUE(changes.removed.empty());
}

TEST(RoomChangesTest, TakingTheWallOutRemovesOneRoomAndChangesTheOther) {
  RoomSources sources {.sealed = TwoRooms()};
  const RoomGraph before = Build(sources);
  std::erase(sources.sealed, TwoRooms()[4]);

  const RoomChanges changes = ChangesBetween(before, Update(before, sources));

  EXPECT_EQ(changes.changed, std::vector {At(before, kLeft)});
  EXPECT_EQ(changes.removed, std::vector {At(before, kRight)});
  EXPECT_TRUE(changes.added.empty());
}

TEST(RoomChangesTest, ABlockMovedInsideARoomChangesItThoughItsVolumeAndBoundsStay) {
  RoomSources sources {.sealed = TwoRooms()};
  sources.sealed.push_back({.min = {3, 3, 1}, .extent = {1, 1, kHeight}});
  const RoomGraph before = Build(sources);
  sources.sealed.back() = {.min = {6, 6, 1}, .extent = {1, 1, kHeight}};

  const RoomGraph after = Update(before, sources);
  const RoomChanges changes = ChangesBetween(before, after);

  const RoomId left = At(before, kLeft);
  EXPECT_EQ(after.rooms.at(left).volume_cells, before.rooms.at(left).volume_cells);
  EXPECT_EQ(after.rooms.at(left).bounds, before.rooms.at(left).bounds);
  EXPECT_EQ(changes.changed, std::vector {left});
  EXPECT_TRUE(changes.added.empty());
  EXPECT_TRUE(changes.removed.empty());
}

TEST(RoomChangesTest, AWindowChangesTheRoomsOnItsTwoSidesOnly) {
  RoomSources sources {.sealed = TwoRooms()};
  sources.sealed.push_back({.min = {1, 4, 1}, .extent = {kSide, 1, kHeight}});
  const RoomGraph before = Build(sources);
  const CellBox window {.min = {kSide + 1, 6, 2}, .extent = {1, 2, 2}};
  sources.portals.push_back({.opening = window, .axis = Axis::kX, .visible = true});

  const RoomGraph after = Update(before, sources);
  const RoomChanges changes = ChangesBetween(before, after);

  std::vector expected {At(before, kLeft), At(before, kRight)};
  std::ranges::sort(expected);
  EXPECT_EQ(changes.changed, expected);
  EXPECT_TRUE(changes.added.empty());
  EXPECT_TRUE(changes.removed.empty());
}

TEST(RoomChangesTest, ARoomCutAgainBesideAnEditIsNoChange) {
  RoomSources sources {.sealed = TwoRooms()};
  const RoomGraph before = Build(sources);
  // In the right room, but in the chunk that also holds the left room's far end.
  sources.sealed.push_back({.min = {kSide + 3, 5, 1}, .extent = {1, 1, kHeight}});

  const RoomChanges changes = ChangesBetween(before, Update(before, sources));

  EXPECT_EQ(changes.changed, std::vector {At(before, kRight)});
  EXPECT_TRUE(changes.added.empty());
  EXPECT_TRUE(changes.removed.empty());
}

}  // namespace
}  // namespace z13::station::rooms
