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


#include <primitives/cell_test.h>

#include <algorithm>
#include <iterator>
#include <span>

namespace z13::building::primitives {

namespace {

using Vec = IntegerPoint;

AxisRange Project(std::span<const Vec> points, const Vec& axis) {
  AxisRange range {.low = points.front().dot(axis), .high = points.front().dot(axis)};
  for (const Vec& point : points) {
    range.low = std::min(range.low, point.dot(axis));
    range.high = std::max(range.high, point.dot(axis));
  }
  return range;
}

}  // namespace

IntegerPoint ToInteger(const Eigen::Vector3f& vertex) {
  return vertex.array().round().cast<int64_t>();
}

SolidCellTest::SolidCellTest(const ConvexSolid& solid) {
  std::vector<Vec> points;
  std::ranges::transform(solid.vertices, std::back_inserter(points), ToInteger);
  std::vector<Vec> axes {Vec::UnitX(), Vec::UnitY(), Vec::UnitZ()};
  for (const auto& face : solid.faces) {
    const Vec origin = points[face[0]];
    axes.push_back((points[face[1]] - origin).cross(points[face[2]] - origin));
    for (size_t i = 0; i < face.size(); ++i) {
      const Vec edge = points[face[(i + 1) % face.size()]] - points[face[i]];
      for (const Vec& cube_edge : {Vec(Vec::UnitX()), Vec(Vec::UnitY()), Vec(Vec::UnitZ())}) {
        axes.push_back(edge.cross(cube_edge));
      }
    }
  }
  for (const Vec& axis : axes) {
    if (!axis.isZero()) {
      axes_.push_back({.axis = axis,
                       .solid = Project(points, axis),
                       .cell = {.low = axis.cwiseMin(0).sum(), .high = axis.cwiseMax(0).sum()}});
    }
  }
}

bool SolidCellTest::ReachesWithVolume(const Eigen::Vector3i& cell) const {
  const Vec corner = cell.cast<int64_t>();
  return std::ranges::none_of(axes_, [&corner](const SeparatingAxis& candidate) {
    const int64_t base = corner.dot(candidate.axis);
    return candidate.solid.high <= base + candidate.cell.low || base + candidate.cell.high <= candidate.solid.low;
  });
}

}  // namespace z13::building::primitives
