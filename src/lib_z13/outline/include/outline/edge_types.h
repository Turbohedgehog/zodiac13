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

#include <cstddef>

namespace z13::station {

// A point of an outline edge, in 1/OutlineEdges::kStepsPerCell of a cell.
struct EdgePoint {
  int x {};
  int y {};
  int z {};

  bool operator==(const EdgePoint&) const = default;
};

constexpr int kEdgeAxes = 3;

inline int Coordinate(const EdgePoint& point, int axis) {
  return axis == 0 ? point.x : (axis == 1 ? point.y : point.z);
}

struct EdgePlane {
  int nx {};
  int ny {};
  int nz {};
  int offset {};

  bool operator==(const EdgePlane&) const = default;
};

struct EdgeSpan {
  EdgePoint a;
  EdgePoint b;

  bool operator==(const EdgeSpan&) const = default;
};

struct EdgePlaneHash {
  size_t operator()(const EdgePlane& plane) const;
};

struct EdgeSpanHash {
  size_t operator()(const EdgeSpan& span) const;
};

}  // namespace z13::station
