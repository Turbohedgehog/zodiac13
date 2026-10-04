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

#include "motion_metrics.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace z13::testing {

namespace {

constexpr float kHalfTurnDeg = 180.f;
constexpr float kFullTurnDeg = 360.f;
// A per-tick step below this counts as standing still.
constexpr float kStillStep = 1e-4f;

float SegmentDistance(const Eigen::Vector3f& point, const Eigen::Vector3f& a, const Eigen::Vector3f& b) {
  const Eigen::Vector3f ab = b - a;
  const float length_sq = ab.squaredNorm();
  const float t = length_sq > 0.f ? std::clamp((point - a).dot(ab) / length_sq, 0.f, 1.f) : 0.f;
  return (point - (a + t * ab)).norm();
}

float MeanSquaredError(
    std::span<const Eigen::Vector3f> truth, std::span<const Eigen::Vector3f> observed, size_t lag) {
  double sum {};
  for (size_t t = lag; t < observed.size(); ++t) {
    sum += (observed[t] - truth[t - lag]).squaredNorm();
  }
  return static_cast<float>(sum / static_cast<double>(observed.size() - lag));
}

size_t BestLag(std::span<const Eigen::Vector3f> truth, std::span<const Eigen::Vector3f> observed, size_t max_lag) {
  size_t best {};
  float best_error = std::numeric_limits<float>::max();
  for (size_t lag = 0; lag <= max_lag && lag < observed.size(); ++lag) {
    const float error = MeanSquaredError(truth, observed, lag);
    if (error < best_error) {
      best_error = error;
      best = lag;
    }
  }
  return best;
}

float Step(std::span<const Eigen::Vector3f> path, size_t t) {
  return (path[t] - path[t - 1]).norm();
}

float PathLength(std::span<const Eigen::Vector3f> path) {
  float length {};
  for (size_t t = 1; t < path.size(); ++t) {
    length += Step(path, t);
  }
  return length;
}

float MaxStep(std::span<const Eigen::Vector3f> path) {
  float max_step {};
  for (size_t t = 1; t < path.size(); ++t) {
    max_step = std::max(max_step, Step(path, t));
  }
  return max_step;
}

float RmsAcceleration(std::span<const Eigen::Vector3f> path) {
  if (path.size() < 3) {
    return 0.f;
  }
  double sum {};
  for (size_t t = 2; t < path.size(); ++t) {
    sum += (path[t] - 2.f * path[t - 1] + path[t - 2]).squaredNorm();
  }
  return static_cast<float>(std::sqrt(sum / static_cast<double>(path.size() - 2)));
}

// Brute force over the whole history: bench runs are a few hundred ticks.
float MaxOffPath(std::span<const Eigen::Vector3f> truth, std::span<const Eigen::Vector3f> observed) {
  float max_off_path {};
  for (size_t t = 0; t < observed.size(); ++t) {
    float nearest = (observed[t] - truth[0]).norm();
    for (size_t i = 1; i <= t; ++i) {
      nearest = std::min(nearest, SegmentDistance(observed[t], truth[i - 1], truth[i]));
    }
    max_off_path = std::max(max_off_path, nearest);
  }
  return max_off_path;
}

std::optional<float> StallFraction(
    std::span<const Eigen::Vector3f> truth, std::span<const Eigen::Vector3f> observed, size_t lag) {
  size_t moving {};
  size_t stalled {};
  for (size_t t = lag + 1; t < observed.size(); ++t) {
    if (Step(truth, t - lag) < kStillStep) {
      continue;
    }
    ++moving;
    if (Step(observed, t) < kStillStep) {
      ++stalled;
    }
  }
  if (moving == 0) {
    return std::nullopt;
  }
  return static_cast<float>(stalled) / static_cast<float>(moving);
}

}  // namespace

TrackingMetrics MeasureTracking(
    std::span<const Eigen::Vector3f> truth, std::span<const Eigen::Vector3f> observed, int max_lag_ticks) {
  const size_t count = std::min(truth.size(), observed.size());
  truth = truth.first(count);
  observed = observed.first(count);
  TrackingMetrics metrics;
  if (count == 0) {
    return metrics;
  }

  const size_t lag = BestLag(truth, observed, static_cast<size_t>(std::max(max_lag_ticks, 0)));
  metrics.lag_ticks = static_cast<int>(lag);
  metrics.rms_error = std::sqrt(MeanSquaredError(truth, observed, lag));
  for (size_t t = lag; t < count; ++t) {
    metrics.max_error = std::max(metrics.max_error, (observed[t] - truth[t - lag]).norm());
  }
  metrics.max_off_path = MaxOffPath(truth, observed);

  const float truth_length = PathLength(truth);
  if (truth_length > 0.f) {
    metrics.path_ratio = PathLength(observed) / truth_length;
  }
  metrics.max_step = MaxStep(observed);
  metrics.truth_max_step = MaxStep(truth);
  metrics.rms_acceleration = RmsAcceleration(observed);
  metrics.truth_rms_acceleration = RmsAcceleration(truth);
  metrics.stall_fraction = StallFraction(truth, observed, lag);
  return metrics;
}

std::vector<Eigen::Vector3f> UnwrappedLook(std::span<const z13::gameplay::LookAngles> look) {
  std::vector<Eigen::Vector3f> path;
  path.reserve(look.size());
  float yaw {};
  for (size_t t = 0; t < look.size(); ++t) {
    if (t == 0) {
      yaw = look[t].yaw_deg;
    } else {
      float turn = std::fmod(look[t].yaw_deg - look[t - 1].yaw_deg + kHalfTurnDeg, kFullTurnDeg);
      if (turn < 0.f) {
        turn += kFullTurnDeg;
      }
      yaw += turn - kHalfTurnDeg;
    }
    path.emplace_back(yaw, look[t].pitch_deg, 0.f);
  }
  return path;
}

}  // namespace z13::testing
