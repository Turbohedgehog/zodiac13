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

#include <vector>

#include <Eigen/Dense>

#include <z13/components/gameplay.h>
#include <z13/components/wall.h>

#include "../../z13_module/tests/support/z13_test_world.h"
#include "../src/wall_entities.h"
#include "../src/wall_joints.h"
#include "support/station_builders.h"

// Where walls meet two at a node they end along one seam; the changes that would break one are refused.
namespace z13::building {
namespace {

using z13::station::WallAxis;
using z13::testing::StationWorld;
using z13::testing::Z13TestWorld;

constexpr int kReach = 16;
constexpr int kThickness = 2;

JointWall Wall(uint32_t id, uint32_t node_a, uint32_t node_b, const Eigen::Vector3i& from, const Eigen::Vector3i& to,
               WallAxis axis = WallAxis::kZ) {
  return {.id = id,
          .node_a = node_a,
          .node_b = node_b,
          .geometry = {.node_a = from, .node_b = to, .stick_axis = axis, .stick_length = 4, .thickness = kThickness}};
}

TEST(WallJointsTest, TwoWallsAtANodeShareOneSeam) {
  const std::vector<JointWall> walls {Wall(1, 1, 2, {0, 0, 0}, {10, 0, 0}), Wall(2, 1, 3, {0, 0, 0}, {0, 10, 0})};
  const WallJoints joints = SolveJoints(walls, kReach);

  ASSERT_TRUE(joints.refused.empty());
  ASSERT_TRUE(joints.ends.contains(1));
  ASSERT_TRUE(joints.ends.contains(2));
  ASSERT_TRUE(joints.ends.at(1).at_a.has_value());
  ASSERT_TRUE(joints.ends.at(2).at_a.has_value());
  EXPECT_EQ(joints.ends.at(1).at_a->line, joints.ends.at(2).at_a->line);
  EXPECT_FALSE(joints.ends.at(1).at_b.has_value());
}

TEST(WallJointsTest, ThreeWallsOrWallsOfDifferentSticksStaySquare) {
  const std::vector<JointWall> three {Wall(1, 1, 2, {0, 0, 0}, {10, 0, 0}), Wall(2, 1, 3, {0, 0, 0}, {0, 10, 0}),
                                      Wall(3, 1, 4, {0, 0, 0}, {-10, 0, 0})};
  EXPECT_TRUE(SolveJoints(three, kReach).ends.empty());

  const std::vector<JointWall> mixed {Wall(1, 1, 2, {0, 0, 0}, {10, 0, 0}),
                                      Wall(2, 1, 3, {0, 0, 0}, {0, 0, 10}, WallAxis::kY)};
  EXPECT_TRUE(SolveJoints(mixed, kReach).ends.empty());
}

TEST(WallJointsTest, ATooSharpJointIsRefusedForBothWalls) {
  const std::vector<JointWall> walls {Wall(1, 1, 2, {0, 0, 0}, {20, 0, 0}), Wall(2, 1, 3, {0, 0, 0}, {20, 3, 0})};
  const WallJoints joints = SolveJoints(walls, 8);

  EXPECT_TRUE(joints.ends.empty());
  EXPECT_TRUE(joints.refused.contains(1));
  EXPECT_TRUE(joints.refused.contains(2));
}

TEST(WallJointsTest, OnlyAChangeThatBreaksAJointIsAProblem) {
  const std::vector<JointWall> alone {Wall(1, 1, 2, {0, 0, 0}, {20, 0, 0})};
  const std::vector<JointWall> square {Wall(1, 1, 2, {0, 0, 0}, {20, 0, 0}), Wall(2, 1, 3, {0, 0, 0}, {0, 20, 0})};
  const std::vector<JointWall> sharp {Wall(1, 1, 2, {0, 0, 0}, {20, 0, 0}), Wall(2, 1, 3, {0, 0, 0}, {20, 1, 0})};

  EXPECT_FALSE(JointProblem(alone, square, kReach).has_value());
  EXPECT_TRUE(JointProblem(alone, sharp, kReach).has_value());
  // A joint that was refused already is not the change's doing.
  EXPECT_FALSE(JointProblem(sharp, sharp, kReach).has_value());
}

TEST(WallJointsTest, AWallAboutToBeLaidSeesTheNodesAtItsEnds) {
  Z13TestWorld test_world = StationWorld();
  flecs::world world = test_world.World();
  SpawnWall(world, world.get_mut<z13::gameplay::IdCounters>(),
            {.node_a = {0, 0, 0}, .node_b = {20, 0, 0}, .stick_axis = WallAxis::kZ, .stick_length = 4,
             .thickness = kThickness});

  const z13::building::primitives::WallGeometry sharp {
      .node_a = {0, 0, 0}, .node_b = {20, 1, 0}, .stick_axis = WallAxis::kZ, .stick_length = 4,
      .thickness = kThickness};
  const z13::building::primitives::WallGeometry square {
      .node_a = {0, 0, 0}, .node_b = {0, 20, 0}, .stick_axis = WallAxis::kZ, .stick_length = 4,
      .thickness = kThickness};
  const std::vector<JointWall> standing = JointWallsOf(world);

  ASSERT_EQ(standing.size(), 1u);
  EXPECT_TRUE(JointProblem(standing, JointWallsWith(world, sharp), kReach).has_value());
  EXPECT_FALSE(JointProblem(standing, JointWallsWith(world, square), kReach).has_value());
}

}  // namespace
}  // namespace z13::building
