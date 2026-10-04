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

#include <Eigen/Dense>

#include "../support/motion_metrics.h"

namespace z13::testing {
namespace {

constexpr float kSpeed = 0.5f;
constexpr int kMoveTicks = 40;
constexpr int kStillTicks = 30;
constexpr int kMaxLag = 20;
constexpr float kEpsilon = 1e-4f;

// Runs along X for kMoveTicks, then stands still.
std::vector<Eigen::Vector3f> RunAndStop() {
  std::vector<Eigen::Vector3f> path;
  for (int t = 0; t < kMoveTicks + kStillTicks; ++t) {
    path.emplace_back(kSpeed * static_cast<float>(std::min(t, kMoveTicks)), 0.f, 0.f);
  }
  return path;
}

std::vector<Eigen::Vector3f> Delayed(const std::vector<Eigen::Vector3f>& path, int lag) {
  std::vector<Eigen::Vector3f> delayed;
  for (int t = 0; t < static_cast<int>(path.size()); ++t) {
    delayed.push_back(path[static_cast<size_t>(std::max(t - lag, 0))]);
  }
  return delayed;
}

TEST(MotionMetricsTest, PureLagIsMeasuredWithoutError) {
  constexpr int kLag = 7;
  const auto truth = RunAndStop();
  const TrackingMetrics metrics = MeasureTracking(truth, Delayed(truth, kLag), kMaxLag);

  EXPECT_EQ(metrics.lag_ticks, kLag);
  EXPECT_NEAR(metrics.rms_error, 0.f, kEpsilon);
  EXPECT_NEAR(metrics.max_off_path, 0.f, kEpsilon);
  ASSERT_TRUE(metrics.path_ratio);
  EXPECT_NEAR(*metrics.path_ratio, 1.f, kEpsilon);
  ASSERT_TRUE(metrics.stall_fraction);
  EXPECT_NEAR(*metrics.stall_fraction, 0.f, kEpsilon);
}

TEST(MotionMetricsTest, BounceShowsAsOffPathAndExtraTravel) {
  constexpr int kOvershootTicks = 10;
  const auto truth = RunAndStop();
  auto observed = truth;
  // Keeps running past the stop, then snaps back.
  for (int t = kMoveTicks; t < kMoveTicks + kOvershootTicks; ++t) {
    observed[static_cast<size_t>(t)].x() = kSpeed * static_cast<float>(t);
  }
  const TrackingMetrics metrics = MeasureTracking(truth, observed, kMaxLag);

  EXPECT_NEAR(metrics.max_off_path, kSpeed * (kOvershootTicks - 1), kEpsilon);
  ASSERT_TRUE(metrics.path_ratio);
  EXPECT_GT(*metrics.path_ratio, 1.f);
  EXPECT_GT(metrics.max_step, metrics.truth_max_step);
}

TEST(MotionMetricsTest, StepsShowAsStallsAndAcceleration) {
  constexpr int kStepTicks = 5;
  const auto truth = RunAndStop();
  auto observed = truth;
  for (size_t t = 0; t < observed.size(); ++t) {
    observed[t] = truth[t - t % kStepTicks];
  }
  const TrackingMetrics metrics = MeasureTracking(truth, observed, kMaxLag);

  EXPECT_NEAR(metrics.max_off_path, 0.f, kEpsilon);
  ASSERT_TRUE(metrics.stall_fraction);
  EXPECT_GT(*metrics.stall_fraction, 0.5f);
  EXPECT_GT(metrics.rms_acceleration, metrics.truth_rms_acceleration);
}

TEST(MotionMetricsTest, StandingStillHasNoPathRatio) {
  const std::vector<Eigen::Vector3f> still(kStillTicks, Eigen::Vector3f::Zero());
  const TrackingMetrics metrics = MeasureTracking(still, still, kMaxLag);

  EXPECT_FALSE(metrics.path_ratio);
  EXPECT_FALSE(metrics.stall_fraction);
}

TEST(MotionMetricsTest, YawIsUnwrappedAcrossTheSeam) {
  const std::vector<z13::gameplay::LookAngles> look {{.yaw_deg = 170.f}, {.yaw_deg = 180.f}, {.yaw_deg = -170.f}};
  const auto path = UnwrappedLook(look);

  ASSERT_EQ(path.size(), look.size());
  EXPECT_NEAR(path[2].x(), 190.f, kEpsilon);
}

}  // namespace
}  // namespace z13::testing
