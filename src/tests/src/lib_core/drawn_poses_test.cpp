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

#include <Eigen/Dense>
#include <flecs.h>

#include <lib_core/utils/drawn_poses.h>
#include <lib_core/utils/flecs_utils.h>
#include <lib_core/utils/math.h>

namespace z13 {
namespace {

constexpr float kFrameSeconds = 1.f / 60.f;
constexpr int kSettleFrames = 120;
constexpr float kEpsilon = 1e-4f;
constexpr float kSnapDistance = 10.f;
constexpr math::PoseSmoothingParams kParams {
    .smooth_time_seconds = 0.1f, .snap_distance = kSnapDistance, .snap_angle_rad = 2.f};

struct OwnTag {};

Eigen::Matrix4f At(float x) {
  Eigen::Matrix4f transform = Eigen::Matrix4f::Identity();
  math::SetTranslation(Eigen::Vector3f(x, 0.f, 0.f), transform);
  return transform;
}

float DrawnX(const DrawnPoses& drawn, flecs::entity e) {
  return math::ExtractTranslation<float>(DrawnTransform(drawn, e, e.get<Eigen::Matrix4f>())).x();
}

class DrawnPosesTest : public ::testing::Test {
 protected:
  void Chase() { ChaseDrawnPoses(drawn_, query_, kFrameSeconds, kParams, HasInAncestry<OwnTag>); }

  flecs::world world_;
  flecs::query<const Eigen::Matrix4f> query_ = world_.query<const Eigen::Matrix4f>();
  DrawnPoses drawn_;
};

TEST_F(DrawnPosesTest, OwnEntitiesAndTheirChildrenAreDrawnAsSimulated) {
  const flecs::entity own = world_.entity().add<OwnTag>().set(At(0.f));
  const flecs::entity own_child = world_.entity().child_of(own).set(At(0.f));
  Chase();
  own.set(At(3.f));
  own_child.set(At(3.f));
  Chase();

  EXPECT_FALSE(drawn_.by_entity.contains(own.id()));
  EXPECT_FALSE(drawn_.by_entity.contains(own_child.id()));
  EXPECT_EQ(DrawnX(drawn_, own), 3.f);
  EXPECT_EQ(DrawnX(drawn_, own_child), 3.f);
}

TEST_F(DrawnPosesTest, OtherEntitiesGlideToTheirTransform) {
  const flecs::entity other = world_.entity().set(At(0.f));
  Chase();
  other.set(At(3.f));
  Chase();
  EXPECT_GT(DrawnX(drawn_, other), 0.f);
  EXPECT_LT(DrawnX(drawn_, other), 3.f);

  for (int frame = 0; frame < kSettleFrames; ++frame) {
    Chase();
  }
  EXPECT_NEAR(DrawnX(drawn_, other), 3.f, kEpsilon);
}

TEST_F(DrawnPosesTest, AJumpPastTheSnapDistanceIsDrawnAtOnce) {
  const flecs::entity other = world_.entity().set(At(0.f));
  Chase();
  other.set(At(kSnapDistance * 2.f));
  Chase();
  EXPECT_EQ(DrawnX(drawn_, other), kSnapDistance * 2.f);
}

TEST_F(DrawnPosesTest, GoneAndNewlyOwnEntitiesDropOut) {
  const flecs::entity gone = world_.entity().set(At(0.f));
  const flecs::entity becomes_own = world_.entity().set(At(0.f));
  Chase();
  ASSERT_EQ(drawn_.by_entity.size(), 2u);

  gone.destruct();
  becomes_own.add<OwnTag>();
  Chase();
  EXPECT_TRUE(drawn_.by_entity.empty());
}

}  // namespace
}  // namespace z13
