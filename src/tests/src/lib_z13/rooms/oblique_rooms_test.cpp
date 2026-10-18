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

#include <z13_tests/shipped_station.h>

#include <primitives/occupancy.h>
#include <rooms/room_builder.h>

namespace z13::station::rooms {
namespace {

using z13::building::primitives::CellBox;
using z13::building::primitives::SealedBoxes;

constexpr uint32_t kSlopeId = 5;
constexpr int kSide = 10;
constexpr int kHeight = 6;

// Six slabs enclosing a room whose inside is [1, kSide + 1) in x and y and [1, kHeight + 1) in z.
std::vector<CellBox> Shell() {
  constexpr int outer = kSide + 2;
  constexpr int tall = kHeight + 2;
  return {
      {.min = {0, 0, 0}, .extent = {outer, outer, 1}},
      {.min = {0, 0, tall - 1}, .extent = {outer, outer, 1}},
      {.min = {0, 0, 1}, .extent = {1, outer, kHeight}},
      {.min = {outer - 1, 0, 1}, .extent = {1, outer, kHeight}},
      {.min = {1, 0, 1}, .extent = {kSide, 1, kHeight}},
      {.min = {1, outer - 1, 1}, .extent = {kSide, 1, kHeight}},
  };
}

z13::station::Block Slope(const Eigen::Vector3i& cell, const Eigen::Vector3i& size) {
  return {.spec = {.type_id = kSlopeId, .size = size}, .cell = cell};
}

int Cells(const std::vector<CellBox>& boxes) {
  int cells = 0;
  for (const CellBox& box : boxes) {
    cells += box.extent.prod();
  }
  return cells;
}

// A slope reaching the roof at its tall end closes the room there: the air above the slope
// and the air past its tall face are different rooms, and the slope's own cells hold none.
TEST(ObliqueRoomsTest, ASlopeToTheRoofDividesTheRoom) {
  const auto palette = z13::testing::ShippedPalette().value();
  RoomSources sources {.sealed = Shell()};
  const auto sealed = SealedBoxes(Slope({3, 1, 1}, {6, kSide, kHeight}), palette);
  sources.sealed.insert(sources.sealed.end(), sealed.begin(), sealed.end());

  const RoomGraph graph = BuildRooms(sources).value();

  ASSERT_EQ(graph.rooms.size(), 3U);
  const auto above_slope = graph.RoomAt({7, 5, 6});
  const auto behind_it = graph.RoomAt({10, 5, 3});
  EXPECT_EQ(graph.RoomAt({1, 5, 3}), above_slope);
  EXPECT_NE(above_slope, behind_it);
  // Frame cell x 4 holds the slope up to z 4; the cell just above is air, the one below isn't.
  EXPECT_EQ(graph.RoomAt({7, 5, 5}), std::nullopt);
  const int behind_volume = 2 * kSide * kHeight;
  const int above_volume = 2 * kSide * kHeight + kSide * (5 + 4 + 3 + 2 + 1);
  EXPECT_EQ(graph.rooms.at(*behind_it).volume_cells, behind_volume);
  EXPECT_EQ(graph.rooms.at(*above_slope).volume_cells, above_volume);
}

// The boxes are exactly the cells whose centre is at or under the surface.
TEST(ObliqueRoomsTest, ASlopeSealsTheCellsUnderItsSurface) {
  const auto palette = z13::testing::ShippedPalette().value();
  for (const Eigen::Vector3i size : {Eigen::Vector3i(4, 4, 4), Eigen::Vector3i(16, 3, 4), Eigen::Vector3i(12, 2, 3)}) {
    const auto boxes = SealedBoxes(Slope({0, 0, 0}, size), palette);
    int expected = 0;
    for (int x = 0; x < size.x(); ++x) {
      for (int z = 0; z < size.z(); ++z) {
        // The cell's centre (x + 1/2, z + 1/2) is at or under the surface z = x * rise / run.
        if ((2 * z + 1) * size.x() <= (2 * x + 1) * size.z()) {
          expected += size.y();
        }
      }
    }
    EXPECT_EQ(Cells(boxes), expected) << size.transpose();
  }
}

TEST(ObliqueRoomsTest, AWallSealsItsBox) {
  const auto palette = z13::testing::ShippedPalette().value();
  const z13::station::Block wall {.spec = {.type_id = 2, .size = {8, 1, 8}}, .cell = {1, 2, 3}};

  const auto boxes = SealedBoxes(wall, palette);

  ASSERT_EQ(boxes.size(), 1U);
  EXPECT_EQ(boxes[0], (CellBox {.min = {1, 2, 3}, .extent = {8, 1, 8}}));
}

}  // namespace
}  // namespace z13::station::rooms
