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

#include <limits>

#include <Eigen/Dense>

#include <lib_core/utils/math.h>

namespace z13::building::grid {

// Where a walk along a segment is: the cell, the face it came in through (its outward normal, zero for the first
// cell) and the share of the segment travelled to that face.
struct CellStep {
  Eigen::Vector3i cell = Eigen::Vector3i::Zero();
  Eigen::Vector3i normal = Eigen::Vector3i::Zero();
  float travelled {};
};

// Visits the cells of the segment from `from` to `to` (in cells) in order, Amanatides and Woo; `visit` returns true
// to stop.
template <class Visit>
void WalkCells(const Eigen::Vector3f& from, const Eigen::Vector3f& to, Visit visit) {
  const Eigen::Vector3f direction = to - from;
  CellStep at {.cell = from.array().floor().cast<int>()};
  Eigen::Vector3i step = Eigen::Vector3i::Zero();
  Eigen::Vector3f next_crossing = Eigen::Vector3f::Constant(std::numeric_limits<float>::infinity());
  Eigen::Vector3f crossing_interval = next_crossing;
  for (int axis = 0; axis < 3; ++axis) {
    const float along = direction[axis];
    const int axis_step = z13::math::Sign(along);
    if (axis_step == 0) {
      continue;
    }
    step[axis] = axis_step;
    const float boundary = static_cast<float>(at.cell[axis] + (axis_step > 0 ? 1 : 0));
    next_crossing[axis] = (boundary - from[axis]) / along;
    crossing_interval[axis] = static_cast<float>(axis_step) / along;
  }
  while (at.travelled <= 1.f && !visit(at)) {
    int axis = 0;
    at.travelled = next_crossing.minCoeff(&axis);
    const int axis_step = step[axis];
    at.cell[axis] += axis_step;
    at.normal = Eigen::Vector3i::Zero();
    at.normal[axis] = -axis_step;
    next_crossing[axis] += crossing_interval[axis];
  }
}

}  // namespace z13::building::grid
