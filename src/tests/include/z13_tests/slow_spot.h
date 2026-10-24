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

#include <Eigen/Dense>

#include <primitives/station_assets.h>
#include <primitives/station_viewpoints.h>

namespace z13::testing {

inline constexpr float kSlowSpotEyeM = 1.75f;

// The octant's atrium, where the 19 fps were seen, in meters.
inline const Eigen::Vector3f kSlowSpotM = [] {
  const auto viewpoints = z13::building::primitives::StationViewpoints(z13::building::primitives::kOctantScene);
  const Eigen::Vector2f floor = viewpoints.front().position_m;
  return Eigen::Vector3f(floor.x(), floor.y(), kSlowSpotEyeM);
}();

}  // namespace z13::testing
