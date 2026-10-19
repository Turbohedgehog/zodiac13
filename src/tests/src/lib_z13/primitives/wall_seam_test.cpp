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

#include <string>

#include <Eigen/Dense>

#include <primitives/wall_seam.h>

namespace z13::building::primitives {
namespace {

using z13::station::WallAxis;

constexpr int kReach = 64;

PlanWall Wall(const Eigen::Vector2i& direction, const Eigen::Vector2i& offset) {
  return {.node = Eigen::Vector2i::Zero(), .direction = direction, .offset = offset};
}

PlanPoint FarEnd(const PlanWall& wall) {
  return {.x = wall.direction.x(), .y = wall.direction.y(), .w = 1};
}

// A point the tests know is on a whole cell.
Eigen::Vector2i Whole(const PlanPoint& point) {
  EXPECT_EQ(point.x % point.w, 0);
  EXPECT_EQ(point.y % point.w, 0);
  return {static_cast<int>(point.x / point.w), static_cast<int>(point.y / point.w)};
}

TEST(WallSeamTest, TwoWallsThickeningIntoTheirCornerAreMitredAcrossIt) {
  const auto seam = PlanSeam(Wall({10, 0}, {0, 2}), Wall({0, 10}, {2, 0}), kReach);
  ASSERT_TRUE(seam.has_value()) << seam.error();
  EXPECT_EQ(Whole(seam->outer), Eigen::Vector2i(0, 0));
  EXPECT_EQ(Whole(seam->inner), Eigen::Vector2i(2, 2));
  EXPECT_EQ(seam->line, (SeamLine {.a = 1, .b = -1, .c = 0}));
}

TEST(WallSeamTest, WallsOnTheTwoSidesOfTheirPathMeetAcrossAnExtendedEnd) {
  const PlanWall first = Wall({10, 0}, {0, 2});
  const PlanWall second = Wall({0, 10}, {-2, 0});
  const auto seam = PlanSeam(first, second, kReach);
  ASSERT_TRUE(seam.has_value()) << seam.error();
  EXPECT_EQ(Whole(seam->inner), Eigen::Vector2i(0, 2));
  EXPECT_EQ(Whole(seam->outer), Eigen::Vector2i(-2, 0));
  EXPECT_EQ(seam->line, (SeamLine {.a = 1, .b = -1, .c = 2}));
  // The first wall keeps the cell behind its node.
  EXPECT_EQ(SideOf(seam->line, PlanPoint {.x = -1, .y = 0, .w = 1}), SideOf(seam->line, FarEnd(first)));
}

TEST(WallSeamTest, TheSeamIsTheSameFromEitherWall) {
  const PlanWall first = Wall({12, 5}, {0, 3});
  const PlanWall second = Wall({-4, 9}, {2, 0});
  const auto forward = PlanSeam(first, second, kReach);
  const auto backward = PlanSeam(second, first, kReach);
  ASSERT_TRUE(forward.has_value()) << forward.error();
  ASSERT_TRUE(backward.has_value()) << backward.error();
  EXPECT_EQ(forward->line, backward->line);
}

TEST(WallSeamTest, TheWallsKeepOppositeSidesAndTheSeamHoldsBothCorners) {
  for (const Eigen::Vector2i& first_offset : {Eigen::Vector2i(0, 3), Eigen::Vector2i(0, -3)}) {
    for (const Eigen::Vector2i& second_offset : {Eigen::Vector2i(2, 0), Eigen::Vector2i(-2, 0)}) {
      const PlanWall first = Wall({12, 5}, first_offset);
      const PlanWall second = Wall({-4, 9}, second_offset);
      const auto seam = PlanSeam(first, second, kReach);
      ASSERT_TRUE(seam.has_value()) << seam.error();
      EXPECT_EQ(SideOf(seam->line, seam->inner), 0);
      EXPECT_EQ(SideOf(seam->line, seam->outer), 0);
      EXPECT_LT(SideOf(seam->line, FarEnd(first)) * SideOf(seam->line, FarEnd(second)), 0);
    }
  }
}

TEST(WallSeamTest, AStraightRunEndsAcrossItsThicknessAtTheNode) {
  const auto seam = PlanSeam(Wall({10, 0}, {0, 2}), Wall({-10, 0}, {0, 2}), kReach);
  ASSERT_TRUE(seam.has_value()) << seam.error();
  EXPECT_EQ(seam->line, (SeamLine {.a = 1, .b = 0, .c = 0}));
}

TEST(WallSeamTest, WallsLeavingTheNodeTheSameWayHaveNoSeam) {
  EXPECT_FALSE(PlanSeam(Wall({10, 0}, {0, 2}), Wall({6, 0}, {0, -1}), kReach).has_value());
}

TEST(WallSeamTest, AWallOfNoLengthOrThicknessHasNoSeam) {
  EXPECT_FALSE(PlanSeam(Wall({0, 0}, {0, 2}), Wall({0, 10}, {2, 0}), kReach).has_value());
  EXPECT_FALSE(PlanSeam(Wall({10, 0}, {0, 0}), Wall({0, 10}, {2, 0}), kReach).has_value());
  EXPECT_FALSE(PlanSeam(Wall({1000, 0}, {0, 2}), Wall({0, 10}, {2, 0}), kReach).has_value());
}

TEST(WallSeamTest, ATipReachingTooFarIsRefused) {
  const PlanWall first = Wall({20, 0}, {0, 2});
  const PlanWall second = Wall({20, 3}, {0, -2});
  const auto seam = PlanSeam(first, second, 8);
  ASSERT_FALSE(seam.has_value());
  EXPECT_NE(seam.error().find("reach"), std::string::npos) << seam.error();
}

TEST(WallSeamTest, AWallTooShortForTheJointSaysHowLongItMustBe) {
  const PlanWall first = Wall({20, 0}, {0, 2});
  const PlanWall second = Wall({20, 3}, {0, -2});
  const auto seam = PlanSeam(first, second, kReach);
  ASSERT_FALSE(seam.has_value());
  // The tip is 80/3 = 26.7 cells along: 27 are taken, plus the cell the wall keeps.
  EXPECT_NE(seam.error().find("at least 28"), std::string::npos) << seam.error();
  EXPECT_TRUE(PlanSeam(Wall({29, 0}, {0, 2}), Wall({35, 5}, {0, -2}), kReach).has_value());
}

TEST(WallSeamTest, AWallComesAsItsEndAtANodeInThePlaneOfItsStick) {
  const z13::building::primitives::WallGeometry wall {
      .node_a = {0, 0, 0}, .node_b = {7, 3, 0}, .stick_axis = WallAxis::kZ, .stick_length = 5, .thickness = 2};
  const auto at_a = PlanWallAt(wall, true);
  const auto at_b = PlanWallAt(wall, false);
  ASSERT_TRUE(at_a.has_value());
  ASSERT_TRUE(at_b.has_value());
  EXPECT_EQ(at_a->node, Eigen::Vector2i(0, 0));
  EXPECT_EQ(at_a->direction, Eigen::Vector2i(7, 3));
  EXPECT_EQ(at_a->offset, Eigen::Vector2i(0, 2));
  EXPECT_EQ(at_b->node, Eigen::Vector2i(7, 3));
  EXPECT_EQ(at_b->direction, Eigen::Vector2i(-7, -3));
  EXPECT_EQ(at_b->offset, Eigen::Vector2i(0, 2));

  // A stick along X puts the plane at (y, z).
  const z13::building::primitives::WallGeometry upright {
      .node_a = {4, 0, 0}, .node_b = {4, 3, 7}, .stick_axis = WallAxis::kX, .stick_length = 5, .thickness = -1};
  const auto in_yz = PlanWallAt(upright, true);
  ASSERT_TRUE(in_yz.has_value());
  EXPECT_EQ(in_yz->direction, Eigen::Vector2i(3, 7));
  EXPECT_EQ(in_yz->offset, Eigen::Vector2i(-1, 0));
}

}  // namespace
}  // namespace z13::building::primitives
