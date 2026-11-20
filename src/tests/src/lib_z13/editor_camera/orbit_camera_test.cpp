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
#include <limits>

#include <editor_camera/orbit_camera.h>
#include <lib_core/utils/math.h>

namespace z13::editor::camera {
namespace {

constexpr float kTolerance = static_cast<float>(z13::math::kEpsilon);
constexpr DistanceLimits kLimits {.min = 4.f, .max = 800.f};
constexpr float kFov = 60.f;
constexpr float kDegreesPerPixel = 0.3f;
constexpr float kPerPixel = 0.01f;
constexpr float kDistance = 100.f;
constexpr float kDrag = 10.f;
constexpr float kAspect = 16.f / 9.f;

OrbitCamera EastOfTheTarget() {
  return {.target = Eigen::Vector3f::Zero(), .distance = kDistance, .yaw_degrees = 0.f, .pitch_degrees = 0.f};
}

TEST(OrbitCameraTest, TheEyeStandsOnThePlusXSideAtYawZeroAndAboveAtPitchNinety) {
  EXPECT_TRUE(EyeOf(EastOfTheTarget()).isApprox(Eigen::Vector3f(kDistance, 0.f, 0.f), kTolerance));

  OrbitCamera above = EastOfTheTarget();
  above.pitch_degrees = 90.f;
  above.target = Eigen::Vector3f(1.f, 2.f, 3.f);
  EXPECT_TRUE(EyeOf(above).isApprox(Eigen::Vector3f(1.f, 2.f, 3.f + kDistance), kTolerance));
}

TEST(OrbitCameraTest, DraggingRightTurnsTheSceneRightAndDownTipsItToLookFromAbove) {
  const OrbitCamera turned = Orbited(EastOfTheTarget(), {100.f, 0.f}, kDegreesPerPixel);
  EXPECT_NEAR(turned.yaw_degrees, -30.f, kTolerance * 10);

  const OrbitCamera tipped = Orbited(EastOfTheTarget(), {0.f, 100.f}, kDegreesPerPixel);
  EXPECT_NEAR(tipped.pitch_degrees, 30.f, kTolerance * 10);
}

TEST(OrbitCameraTest, ThePitchStopsShortOfStraightDownAndUp) {
  EXPECT_LT(Orbited(EastOfTheTarget(), {0.f, 1e6f}, kDegreesPerPixel).pitch_degrees, 90.f);
  EXPECT_GT(Orbited(EastOfTheTarget(), {0.f, -1e6f}, kDegreesPerPixel).pitch_degrees, -90.f);
}

TEST(OrbitCameraTest, DraggingGrabsTheGroundAlongTheFloor) {
  // The eye is east of the target looking west: down is towards the target's side of the floor, right is north.
  const OrbitCamera down = Panned(EastOfTheTarget(), {0.f, kDrag}, kPerPixel);
  EXPECT_TRUE(down.target.isApprox(Eigen::Vector3f(-kDrag, 0.f, 0.f) * kPerPixel * kDistance, kTolerance));

  const OrbitCamera right = Panned(EastOfTheTarget(), {kDrag, 0.f}, kPerPixel);
  EXPECT_TRUE(right.target.isApprox(Eigen::Vector3f(0.f, -kDrag, 0.f) * kPerPixel * kDistance, kTolerance));
}

TEST(OrbitCameraTest, PanningStaysOnTheFloorWhateverThePitch) {
  OrbitCamera tilted = EastOfTheTarget();
  tilted.pitch_degrees = 60.f;

  EXPECT_FLOAT_EQ(Panned(tilted, {kDrag, kDrag}, kPerPixel).target.z(), 0.f);
}

TEST(OrbitCameraTest, ZoomingStaysBetweenTheLimits) {
  EXPECT_NEAR(Zoomed(EastOfTheTarget(), 0.5f, kLimits).distance, kDistance * 0.5f, kTolerance);
  EXPECT_FLOAT_EQ(Zoomed(EastOfTheTarget(), 1e-3f, kLimits).distance, kLimits.min);
  EXPECT_FLOAT_EQ(Zoomed(EastOfTheTarget(), 1e3f, kLimits).distance, kLimits.max);
}

TEST(OrbitCameraTest, ANotchThatDividesByZeroOrNothingLeavesTheDistanceAlone) {
  EXPECT_FLOAT_EQ(NotchFactor(0.f, true), 1.f);
  EXPECT_FLOAT_EQ(NotchFactor(-2.f, false), 1.f);
  EXPECT_FLOAT_EQ(NotchFactor(2.f, true), 0.5f);
  EXPECT_FLOAT_EQ(NotchFactor(2.f, false), 2.f);
  EXPECT_FLOAT_EQ(Zoomed(EastOfTheTarget(), 0.f, kLimits).distance, kDistance);
  EXPECT_FLOAT_EQ(Zoomed(EastOfTheTarget(), std::numeric_limits<float>::quiet_NaN(), kLimits).distance, kDistance);
}

TEST(OrbitCameraTest, AFieldOfViewWithNoTangentFramesFromTheFarthestDistance) {
  const Eigen::Vector3f extent(40.f, 24.f, 12.f);

  EXPECT_FLOAT_EQ(Framed(EastOfTheTarget(), Eigen::Vector3f::Zero(), extent, 0.f, kLimits).distance, kLimits.max);
  EXPECT_TRUE(std::isfinite(Framed(EastOfTheTarget(), Eigen::Vector3f::Zero(), extent, 180.f, kLimits).distance));
}

TEST(OrbitCameraTest, ARayMeetsTheFloorWhereItGoesDownToItAndNowhereElse) {
  const Ray down {.origin = Eigen::Vector3f(1.f, 2.f, 10.f), .direction = Eigen::Vector3f(0.f, 0.f, -1.f)};
  const Ray level {.origin = Eigen::Vector3f(1.f, 2.f, 10.f), .direction = Eigen::Vector3f::UnitX()};
  const Ray up {.origin = Eigen::Vector3f(1.f, 2.f, 10.f), .direction = Eigen::Vector3f::UnitZ()};

  ASSERT_TRUE(GroundHit(down, 4.f));
  EXPECT_TRUE(GroundHit(down, 4.f)->isApprox(Eigen::Vector3f(1.f, 2.f, 4.f), kTolerance));
  EXPECT_FALSE(GroundHit(level, 4.f));
  EXPECT_FALSE(GroundHit(up, 4.f));
}

TEST(OrbitCameraTest, FramingLooksAtTheMiddleOfTheBoxFromFarEnoughToSeeIt) {
  const Eigen::Vector3f min(10.f, 20.f, 0.f);
  const Eigen::Vector3f extent(40.f, 24.f, 12.f);

  const OrbitCamera framed = Framed(EastOfTheTarget(), min, extent, kFov, kLimits);

  EXPECT_TRUE(framed.target.isApprox(min + extent * 0.5f, kTolerance));
  EXPECT_GT(framed.distance, extent.norm() * 0.5f);
  EXPECT_LE(framed.distance, kLimits.max);
}

TEST(OrbitCameraTest, TheRayThroughTheMiddleOfTheScreenReachesTheTarget) {
  OrbitCamera camera {.target = Eigen::Vector3f(5.f, 6.f, 1.f), .distance = kDistance, .yaw_degrees = 40.f,
                      .pitch_degrees = 35.f};

  const Ray middle = RayThrough(camera, Eigen::Vector2f::Zero(), kFov, kAspect);

  EXPECT_TRUE(middle.origin.isApprox(EyeOf(camera), kTolerance));
  EXPECT_NEAR(middle.direction.norm(), 1.f, kTolerance);
  EXPECT_TRUE((middle.origin + middle.direction * kDistance).isApprox(camera.target, kTolerance * 10));
}

TEST(OrbitCameraTest, ARayThroughTheRightEdgeLeansRightAndOneThroughTheTopLeansUp) {
  const OrbitCamera camera = EastOfTheTarget();  // looking west: right is north (+y)

  EXPECT_GT(RayThrough(camera, {1.f, 0.f}, kFov, kAspect).direction.y(), 0.f);
  EXPECT_GT(RayThrough(camera, {0.f, 1.f}, kFov, kAspect).direction.z(), 0.f);
}

TEST(OrbitCameraTest, FingersMovingApartZoomInAndTurningThemTwistsClockwise) {
  const Eigen::Vector2f origin = Eigen::Vector2f::Zero();
  const Eigen::Vector2f ten(10.f, 0.f);

  const Gesture apart = TwoFingerGesture(origin, ten, origin, Eigen::Vector2f(20.f, 0.f));
  EXPECT_NEAR(apart.zoom_factor, 0.5f, kTolerance);
  EXPECT_TRUE(apart.pan_pixels.isApprox(Eigen::Vector2f(5.f, 0.f), kTolerance));
  EXPECT_NEAR(apart.twist_degrees, 0.f, kTolerance);

  const Gesture turned = TwoFingerGesture(origin, ten, origin, Eigen::Vector2f(0.f, 10.f));
  EXPECT_NEAR(turned.twist_degrees, 90.f, kTolerance * 10);
  EXPECT_NEAR(turned.zoom_factor, 1.f, kTolerance);
}

TEST(OrbitCameraTest, FingersOnTheSamePointAreNoPinch) {
  const Eigen::Vector2f here(3.f, 4.f);

  const Gesture gesture = TwoFingerGesture(here, here, here, Eigen::Vector2f(30.f, 40.f));

  EXPECT_FLOAT_EQ(gesture.zoom_factor, 1.f);
  EXPECT_FLOAT_EQ(gesture.twist_degrees, 0.f);
}

}  // namespace
}  // namespace z13::editor::camera
