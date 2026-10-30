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

#include "span_vertices.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace z13::station {

namespace {

// A vertex this close to an edge (in steps) is taken to lie on it; the ends are quantized.
constexpr double kOnEdgeSteps = 2.0;

double Dot(const std::array<double, kEdgeAxes>& left, const std::array<double, kEdgeAxes>& right) {
  return left[0] * right[0] + left[1] * right[1] + left[2] * right[2];
}

std::array<double, kEdgeAxes> Between(const EdgePoint& from, const EdgePoint& to) {
  return {static_cast<double>(to.x - from.x), static_cast<double>(to.y - from.y), static_cast<double>(to.z - from.z)};
}

}  // namespace

SpanVertices::SpanVertices(const std::vector<EdgeSpan>& spans) {
  for (const EdgeSpan& span : spans) {
    for (auto& axis_sorted : sorted_) {
      axis_sorted.push_back(span.a);
      axis_sorted.push_back(span.b);
    }
  }
  for (int axis = 0; axis < kEdgeAxes; ++axis) {
    std::ranges::sort(sorted_[axis], {}, [axis](const EdgePoint& point) { return Coordinate(point, axis); });
  }
}

std::vector<EdgeSpan> SpanVertices::Split(const EdgeSpan& edge) const {
  const auto along = Between(edge.a, edge.b);
  const double length = std::sqrt(Dot(along, along));
  int axis = 0;
  for (int other = 1; other < kEdgeAxes; ++other) {
    if (std::abs(Coordinate(edge.b, other) - Coordinate(edge.a, other)) >
        std::abs(Coordinate(edge.b, axis) - Coordinate(edge.a, axis))) {
      axis = other;
    }
  }
  const int low = std::min(Coordinate(edge.a, axis), Coordinate(edge.b, axis));
  const int high = std::max(Coordinate(edge.a, axis), Coordinate(edge.b, axis));
  const auto& column = sorted_[axis];
  const auto first = std::ranges::lower_bound(column, low, {}, [axis](const EdgePoint& p) { return Coordinate(p, axis); });
  std::vector<std::pair<double, EdgePoint>> on_edge;
  for (auto it = first; it != column.end() && Coordinate(*it, axis) <= high; ++it) {
    const auto offset = Between(edge.a, *it);
    const double ahead = Dot(offset, along) / length;
    const double off_line = std::sqrt(std::max(0.0, Dot(offset, offset) - ahead * ahead));
    if (off_line <= kOnEdgeSteps && ahead > kOnEdgeSteps && ahead < length - kOnEdgeSteps) {
      on_edge.emplace_back(ahead, *it);
    }
  }
  std::ranges::sort(on_edge, {}, &std::pair<double, EdgePoint>::first);
  std::vector<EdgeSpan> pieces;
  EdgePoint from = edge.a;
  for (const auto& [ahead, vertex] : on_edge) {
    pieces.push_back({from, vertex});
    from = vertex;
  }
  pieces.push_back({from, edge.b});
  return pieces;
}

}  // namespace z13::station
