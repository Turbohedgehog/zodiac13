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

#include <span>
#include <string>
#include <vector>

#include <Eigen/Dense>

#include <primitives/placement.h>

namespace z13::station::rooms {

// One stop of the space walk: where the camera stands and what it looks at, in cells.
struct SpaceWalkStop {
  std::string label;
  Eigen::Vector3f eye = Eigen::Vector3f::Zero();
  Eigen::Vector3f target = Eigen::Vector3f::Zero();
};

// Outside the hull, in cells: the exit stop stands this far past the nearest wall.
inline constexpr int kExitMarginCells = 8;
// The orbit's eye is this many hull extents (the larger one in the plan) from its centre.
inline constexpr float kOrbitDistance = 0.75f;
// The orbit's eye is this many cells above the hull's top, the overview's this many extents.
inline constexpr int kOrbitRiseCells = 24;
inline constexpr float kOverviewHeight = 1.5f;

// A walk out of the station and around it, the same for the same input: every point of
// `inside` (each looking along +x), then an exit through the nearest wall of `hull` (looking
// back at the last inside point), the eight compass points of an orbit around the hull, and
// the overview from above.
std::vector<SpaceWalkStop> SpaceWalkRoute(const z13::building::primitives::CellBox& hull,
                                          std::span<const Eigen::Vector3f> inside);

}  // namespace z13::station::rooms
