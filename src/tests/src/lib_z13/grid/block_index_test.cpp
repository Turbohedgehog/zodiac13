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

#include <Eigen/Dense>

#include <z13_grid/block_index.h>

namespace z13::building::grid {
namespace {

constexpr BlockId kFloor = 101;
constexpr BlockId kWall = 102;

// A floor spanning several chunks, negative cells included, and a wall standing on it.
BlockIndex FloorAndWall() {
  BlockIndex index;
  index.Insert(kFloor, {.min = {-40, -40, -1}, .extent = {80, 80, 1}});
  index.Insert(kWall, {.min = {5, -3, 0}, .extent = {1, 6, 10}});
  return index;
}

TEST(BlockIndexTest, FindsTheBlockAtEveryCellItOccupies) {
  const BlockIndex index = FloorAndWall();

  EXPECT_EQ(index.At({-40, -40, -1}), kFloor);
  EXPECT_EQ(index.At({39, 39, -1}), kFloor);
  EXPECT_EQ(index.At({5, 2, 9}), kWall);
  EXPECT_EQ(index.At({40, 0, -1}), std::nullopt);
  EXPECT_EQ(index.At({4, 0, 0}), std::nullopt);
}

TEST(BlockIndexTest, OverlapsOnlyWhenACellIsShared) {
  const BlockIndex index = FloorAndWall();

  EXPECT_TRUE(index.Overlaps({.min = {5, 2, 9}, .extent = {1, 1, 1}}));
  EXPECT_TRUE(index.Overlaps({.min = {-100, 0, -1}, .extent = {200, 1, 100}}));
  EXPECT_FALSE(index.Overlaps({.min = {6, -3, 0}, .extent = {4, 6, 10}}));
  EXPECT_FALSE(index.Overlaps({.min = {0, 0, 10}, .extent = {16, 16, 16}}));
}

TEST(BlockIndexTest, ErasedBlocksLeaveTheirCellsFree) {
  BlockIndex index = FloorAndWall();

  index.Erase(kWall);

  EXPECT_EQ(index.Size(), 1u);
  EXPECT_EQ(index.At({5, 0, 0}), std::nullopt);
  EXPECT_FALSE(index.Overlaps({.min = {5, -3, 0}, .extent = {1, 6, 10}}));
}

TEST(BlockIndexTest, RaycastHitsTheFirstBlockAlongTheSegment) {
  const BlockIndex index = FloorAndWall();

  // Level, at eye height: the wall, not the floor below.
  EXPECT_EQ(index.Raycast({0.5f, 0.5f, 6.5f}, {20.5f, 0.5f, 6.5f}), kWall);
  // Down at the floor, in front of the wall.
  EXPECT_EQ(index.Raycast({0.5f, 0.5f, 6.5f}, {3.5f, 0.5f, -4.5f}), kFloor);
  // Too short to reach either.
  EXPECT_EQ(index.Raycast({0.5f, 0.5f, 6.5f}, {4.5f, 0.5f, 6.5f}), std::nullopt);
  // Backwards, away from the wall, never reaching the floor.
  EXPECT_EQ(index.Raycast({0.5f, 0.5f, 6.5f}, {-20.5f, 0.5f, 6.5f}), std::nullopt);
}

TEST(BlockIndexTest, OverlappingListsEachBlockOnceEvenAcrossChunks) {
  const BlockIndex index = FloorAndWall();

  const auto both = index.Overlapping({.min = {0, 0, -1}, .extent = {10, 2, 2}});
  EXPECT_EQ(both.size(), 2u);
  EXPECT_NE(std::ranges::find(both, kFloor), both.end());
  EXPECT_NE(std::ranges::find(both, kWall), both.end());
  EXPECT_TRUE(index.Overlapping({.min = {100, 100, 100}, .extent = {1, 1, 1}}).empty());
}

TEST(BlockIndexTest, RaycastHitReportsTheFaceEntered) {
  const BlockIndex index = FloorAndWall();

  const auto wall = index.RaycastHit({0.5f, 0.5f, 6.5f}, {20.5f, 0.5f, 6.5f});
  ASSERT_TRUE(wall);
  EXPECT_EQ(wall->normal, Eigen::Vector3i(-1, 0, 0));
  EXPECT_NEAR(wall->point.x(), 5.f, 1e-4f);

  const auto floor = index.RaycastHit({0.5f, 0.5f, 6.5f}, {3.5f, 0.5f, -4.5f});
  ASSERT_TRUE(floor);
  EXPECT_EQ(floor->normal, Eigen::Vector3i(0, 0, 1));
  EXPECT_NEAR(floor->point.z(), 0.f, 1e-4f);

  const auto inside = index.RaycastHit({5.5f, 0.5f, 6.5f}, {8.5f, 0.5f, 6.5f});
  ASSERT_TRUE(inside);
  EXPECT_TRUE(inside->normal.isZero());
}

}  // namespace
}  // namespace z13::building::grid
