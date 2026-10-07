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

#include <lib_core/utils/frustum.h>

namespace z13::math {
namespace {

constexpr float kNear = 0.1f;
constexpr float kFar = 100.f;

// OpenGL's perspective, looking down -Z, 90° wide and high.
Eigen::Matrix4f Perspective() {
  const float f = 1.f / std::tan(std::numbers::pi_v<float> / 4.f);
  Eigen::Matrix4f m = Eigen::Matrix4f::Zero();
  m(0, 0) = f;
  m(1, 1) = f;
  m(2, 2) = (kFar + kNear) / (kNear - kFar);
  m(2, 3) = 2.f * kFar * kNear / (kNear - kFar);
  m(3, 2) = -1.f;
  return m;
}

Eigen::AlignedBox3f UnitBoxAt(const Eigen::Vector3f& centre) {
  return {centre - Eigen::Vector3f::Constant(0.5f), centre + Eigen::Vector3f::Constant(0.5f)};
}

TEST(FrustumTest, KeepsBoxesInViewAndDropsTheOthers) {
  const Frustum frustum(Perspective());

  EXPECT_TRUE(frustum.Intersects(UnitBoxAt({0.f, 0.f, -10.f})));
  EXPECT_TRUE(frustum.Intersects(UnitBoxAt({10.f, 0.f, -10.f})));  // across the right edge
  EXPECT_FALSE(frustum.Intersects(UnitBoxAt({0.f, 0.f, 10.f})));   // behind
  EXPECT_FALSE(frustum.Intersects(UnitBoxAt({20.f, 0.f, -10.f})));
  EXPECT_FALSE(frustum.Intersects(UnitBoxAt({0.f, -20.f, -10.f})));
  EXPECT_FALSE(frustum.Intersects(UnitBoxAt({0.f, 0.f, -200.f})));  // past the far plane
}

TEST(FrustumTest, FollowsTheView) {
  // The camera at x = 50, turned to look down +X.
  Eigen::Matrix4f view = Eigen::Matrix4f::Identity();
  view.topLeftCorner<3, 3>() = Eigen::AngleAxisf(std::numbers::pi_v<float> / 2.f, Eigen::Vector3f::UnitY())
                                   .toRotationMatrix();
  view.topRightCorner<3, 1>() = view.topLeftCorner<3, 3>() * Eigen::Vector3f(-50.f, 0.f, 0.f);
  const Frustum frustum(Perspective() * view);

  EXPECT_TRUE(frustum.Intersects(UnitBoxAt({60.f, 0.f, 0.f})));
  EXPECT_FALSE(frustum.Intersects(UnitBoxAt({40.f, 0.f, 0.f})));
}

}  // namespace
}  // namespace z13::math
