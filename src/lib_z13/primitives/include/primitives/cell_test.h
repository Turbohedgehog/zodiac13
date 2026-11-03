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

#include <cstdint>
#include <vector>

#include <Eigen/Dense>

#include <primitives/geometry.h>

namespace z13::building::primitives {

using IntegerPoint = Eigen::Vector3<int64_t>;

// Integer coordinates throughout: a cell touching a face without volume behind it is
// decided exactly, the same on every platform.
IntegerPoint ToInteger(const Eigen::Vector3f& vertex);

struct AxisRange {
  int64_t low {};
  int64_t high {};
};

// A candidate separating axis with the solid's range on it and the range of a unit cell
// relative to its lowest corner's dot product.
struct SeparatingAxis {
  IntegerPoint axis = IntegerPoint::Zero();
  AxisRange solid;
  AxisRange cell;
};

// Two convex solids share volume unless some axis (a face normal, or a cross product of
// one edge of each) separates them, touching included. The axes and the solid's range on each
// do not depend on the cell, so they are worked out once per solid.
class SolidCellTest {
 public:
  explicit SolidCellTest(const ConvexSolid& solid);

  // Whether the solid reaches the unit cell at `cell` with volume.
  bool ReachesWithVolume(const Eigen::Vector3i& cell) const;

 private:
  std::vector<SeparatingAxis> axes_;
};

}  // namespace z13::building::primitives
