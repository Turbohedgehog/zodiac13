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

#include <cmath>
#include <numbers>

#include <Eigen/Dense>

#include <z13/components/grid_pose.h>

namespace z13::station {
namespace {

constexpr float kRounding = 1e-5f;

TEST(GridPoseTest, TheMainGridStandsWhereTheWorldDoes) {
  const Eigen::Isometry3f pose = GridToWorld(Grid {});

  EXPECT_TRUE(pose.matrix().isApprox(Eigen::Matrix4f::Identity(), kRounding));
}

TEST(GridPoseTest, TheOriginMovesTheGridByCells) {
  const Eigen::Isometry3f pose = GridToWorld(Grid {.origin = {40, -8, 12}});

  EXPECT_TRUE(pose.translation().isApprox(Eigen::Vector3f(40.f, -8.f, 12.f) * kCellSize, kRounding));
  EXPECT_TRUE(pose.linear().isApprox(Eigen::Matrix3f::Identity(), kRounding));
}

TEST(GridPoseTest, AYawTurnsTheGridAboutTheWorldsZ) {
  const Eigen::Isometry3f pose = GridToWorld(Grid {.yaw_degrees = 90.f});

  EXPECT_TRUE((pose.linear() * Eigen::Vector3f::UnitX()).isApprox(Eigen::Vector3f::UnitY(), kRounding));
  EXPECT_TRUE((pose.linear() * Eigen::Vector3f::UnitZ()).isApprox(Eigen::Vector3f::UnitZ(), kRounding));
}

TEST(GridPoseTest, APositivePitchRaisesTheEndAlongPlusXAndTiltsTheDown) {
  constexpr float kPitchDeg = 4.f;
  const Eigen::Isometry3f pose = GridToWorld(Grid {.pitch_degrees = kPitchDeg});

  EXPECT_GT((pose.linear() * Eigen::Vector3f::UnitX()).z(), 0.f);
  EXPECT_NEAR((pose.linear() * Eigen::Vector3f::UnitZ()).dot(Eigen::Vector3f::UnitZ()),
              std::cos(kPitchDeg * std::numbers::pi_v<float> / 180.f), kRounding);
}

TEST(GridPoseTest, ThePitchIsAboutTheGridsOwnYAfterTheYaw) {
  // Yawed by 90 degrees the grid's +X is the world's +Y: the end that rises is the one along +Y.
  const Eigen::Isometry3f pose = GridToWorld(Grid {.yaw_degrees = 90.f, .pitch_degrees = 4.f});

  const Eigen::Vector3f along = pose.linear() * Eigen::Vector3f::UnitX();
  EXPECT_GT(along.z(), 0.f);
  EXPECT_NEAR(along.x(), 0.f, kRounding);
  EXPECT_GT(along.y(), 0.f);
}

TEST(GridPoseTest, PosesAreFoundByGridIdAndAnUnknownGridHasNone) {
  GridPoses poses;
  poses.emplace(GridId {3}, GridToWorld(Grid {.id = 3, .origin = {1, 2, 3}}));

  EXPECT_TRUE(PoseOfGrid(poses, 3).translation().isApprox(Eigen::Vector3f(1.f, 2.f, 3.f) * kCellSize));
  EXPECT_TRUE(PoseOfGrid(poses, 9).matrix().isApprox(Eigen::Matrix4f::Identity()));
}

}  // namespace
}  // namespace z13::station
