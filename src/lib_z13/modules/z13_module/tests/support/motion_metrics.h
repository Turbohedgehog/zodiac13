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

#pragma once

#include <optional>
#include <span>
#include <vector>

#include <Eigen/Dense>

#include <z13_module/gameplay/camera_look.h>

namespace z13::testing {

// How faithfully `observed` replays `truth`, both sampled once per tick. Distances are in
// the trajectory's units (world units, or degrees for look).
struct TrackingMetrics {
  // The delay that best aligns `observed` with `truth`; errors below are measured after it.
  int lag_ticks {};
  float rms_error {};
  float max_error {};
  // How far the observer ever showed the player from anywhere it had actually been by then:
  // extrapolation, overshoot, bounce. Lag alone keeps this at 0.
  float max_off_path {};
  // Observed path length over the true one; jitter and bounce add travel, 1 is ideal.
  std::optional<float> path_ratio;
  // Per-tick step and its change; the truth's own values are the reference.
  float max_step {};
  float truth_max_step {};
  float rms_acceleration {};
  float truth_rms_acceleration {};
  // Of the ticks the (lag-aligned) truth moved, the share the observer stood still.
  std::optional<float> stall_fraction;
};

TrackingMetrics MeasureTracking(
    std::span<const Eigen::Vector3f> truth, std::span<const Eigen::Vector3f> observed, int max_lag_ticks);

// Look angles as a continuous (yaw, pitch, 0) trajectory: yaw is unwrapped across +-180 deg.
std::vector<Eigen::Vector3f> UnwrappedLook(std::span<const z13::gameplay::LookAngles> look);

}  // namespace z13::testing
