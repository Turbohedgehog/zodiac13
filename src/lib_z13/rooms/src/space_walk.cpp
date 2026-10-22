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


#include <rooms/space_walk.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <limits>
#include <numbers>

namespace z13::station::rooms {

namespace {

using z13::building::primitives::CellBox;

constexpr int kOrbitStops = 8;
constexpr std::array kAxes {0, 1};

Eigen::Vector3f CentreOf(const CellBox& box) {
  return (box.min.cast<float>() + box.End().cast<float>()) / 2.f;
}

// Past the nearest xy wall of the hull, at the point's own height.
Eigen::Vector3f ExitPoint(const CellBox& hull, const Eigen::Vector3f& from) {
  Eigen::Vector3f exit = from;
  float nearest = std::numeric_limits<float>::max();
  const Eigen::Vector3f end = hull.End().cast<float>();
  for (const int axis : kAxes) {
    const float to_min = from[axis] - static_cast<float>(hull.min[axis]);
    const float to_end = end[axis] - from[axis];
    if (std::min(to_min, to_end) < nearest) {
      nearest = std::min(to_min, to_end);
      exit = from;
      exit[axis] = to_min < to_end ? static_cast<float>(hull.min[axis] - kExitMarginCells) : end[axis] + kExitMarginCells;
    }
  }
  return exit;
}

}  // namespace

std::vector<SpaceWalkStop> SpaceWalkRoute(const CellBox& hull, std::span<const Eigen::Vector3f> inside) {
  std::vector<SpaceWalkStop> route;
  for (size_t i = 0; i < inside.size(); ++i) {
    route.push_back({.label = std::format("inside{}", i), .eye = inside[i], .target = inside[i] + Eigen::Vector3f::UnitX()});
  }
  const Eigen::Vector3f centre = CentreOf(hull);
  if (!inside.empty()) {
    route.push_back({.label = "exit", .eye = ExitPoint(hull, inside.back()), .target = inside.back()});
  }
  const Eigen::Vector3f extent = hull.extent.cast<float>();
  const float reach = kOrbitDistance * std::max(extent.x(), extent.y());
  const float rise = static_cast<float>(hull.End().z() + kOrbitRiseCells);
  for (int i = 0; i < kOrbitStops; ++i) {
    const float angle = 2.f * std::numbers::pi_v<float> * static_cast<float>(i) / static_cast<float>(kOrbitStops);
    route.push_back({.label = std::format("orbit{}", i),
                     .eye = {centre.x() + reach * std::cos(angle), centre.y() + reach * std::sin(angle), rise},
                     .target = centre});
  }
  const float top = static_cast<float>(hull.End().z()) + kOverviewHeight * std::max(extent.x(), extent.y());
  route.push_back({.label = "overview", .eye = {centre.x(), centre.y(), top}, .target = centre});
  return route;
}

}  // namespace z13::station::rooms
