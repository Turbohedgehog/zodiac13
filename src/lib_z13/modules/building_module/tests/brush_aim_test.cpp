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

#include <z13_primitives/placement.h>

#include "../src/brush_aim.h"

namespace z13::building {
namespace {

using z13::building::grid::BlockIndex;
using z13::building::primitives::OccupiedCells;

constexpr grid::BlockId kFloor = 1;
const z13::station::BlockSpec kWedge {.type_id = 5, .size = {4, 4, 4}};

BlockIndex Floor() {
  BlockIndex index;
  index.Insert(kFloor, {.min = {-12, -12, -1}, .extent = {24, 24, 1}});
  return index;
}

TEST(BrushAimTest, ABlockRestsOnTheFaceTheRayHits) {
  const BlockIndex index = Floor();
  const Aim aim = AimAt(index, {0.5f, 0.5f, 6.f}, {10.5f, 0.5f, -3.f});

  const auto block = BlockAt(aim, kWedge, index);

  EXPECT_EQ(aim.normal, Eigen::Vector3i(0, 0, 1));
  EXPECT_EQ(block.cell.z(), 0);
  EXPECT_FALSE(index.Overlaps(OccupiedCells(block)));
}

TEST(BrushAimTest, AFreeBlockThatWouldSinkIntoTheFloorIsDrawnBackAlongTheRay) {
  const BlockIndex index = Floor();
  // The reach ends 1.2 cells above the floor, short of it: centred there, a 4-cell block sinks in.
  const Aim aim = AimAt(index, {0.5f, 0.5f, 3.f}, {20.5f, 0.5f, 1.2f});
  ASSERT_TRUE(aim.normal.isZero());

  const auto block = BlockAt(aim, kWedge, index);

  EXPECT_FALSE(index.Overlaps(OccupiedCells(block)));
}

TEST(BrushAimTest, ABlockBesideAnotherNearTheFloorSlidesUpOutOfTheFloor) {
  BlockIndex index = Floor();
  constexpr grid::BlockId kFirst = 2;
  index.Insert(kFirst, {.min = {8, 0, 0}, .extent = {4, 4, 4}});
  // A ray at the first block's -X face, 0.5 cells above the floor: centred there, the block sinks into it.
  const Aim aim = AimAt(index, {0.5f, 2.5f, 0.5f}, {12.5f, 2.5f, 0.5f});
  ASSERT_EQ(aim.normal, Eigen::Vector3i(-1, 0, 0));

  const auto block = BlockAt(aim, kWedge, index);

  EXPECT_FALSE(index.Overlaps(OccupiedCells(block)));
  EXPECT_EQ(OccupiedCells(block).End().x(), 8);
}

TEST(BrushAimTest, AFreeBlockInOpenSpaceIsCentredOnTheReach) {
  const BlockIndex index = Floor();
  const Aim aim = AimAt(index, {0.5f, 0.5f, 20.f}, {10.5f, 0.5f, 20.f});

  EXPECT_EQ(BlockAt(aim, kWedge, index).cell, z13::building::primitives::PlaceCentredOn(aim.point, kWedge).cell);
}

}  // namespace
}  // namespace z13::building
