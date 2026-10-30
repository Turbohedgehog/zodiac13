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

#include "outline_edges.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <optional>
#include <tuple>
#include <unordered_set>
#include <utility>

#include "span_vertices.h"

namespace z13::station {

namespace {

constexpr int kSteps = OutlineEdges::kStepsPerCell;
constexpr int kAxes = kEdgeAxes;
constexpr size_t kHashMix = 0x9e3779b97f4a7c15ULL;  // the golden-ratio constant boost::hash_combine uses

void Mix(size_t& seed, int value) {
  seed ^= std::hash<int> {}(value) + kHashMix + (seed << 6) + (seed >> 2);
}

bool Before(const EdgePoint& first, const EdgePoint& second) {
  if (first.x != second.x) {
    return first.x < second.x;
  }
  return first.y != second.y ? first.y < second.y : first.z < second.z;
}

int Quantized(float cells) {
  return static_cast<int>(std::lround(cells * kSteps));
}

EdgePoint QuantizedPoint(const Eigen::Vector3f& point) {
  return {Quantized(point.x()), Quantized(point.y()), Quantized(point.z())};
}

// The plane's normal points to the side its first nonzero component is positive on, so two
// polygons facing opposite ways but lying in one plane are told apart by nothing else.
EdgePlane PlaneOf(const Eigen::Vector3f& unit_normal, const Eigen::Vector3f& point) {
  EdgePlane plane {Quantized(unit_normal.x()), Quantized(unit_normal.y()), Quantized(unit_normal.z()),
               Quantized(unit_normal.dot(point))};
  const int lead = plane.nx != 0 ? plane.nx : (plane.ny != 0 ? plane.ny : plane.nz);
  if (lead < 0) {
    plane = {-plane.nx, -plane.ny, -plane.nz, -plane.offset};
  }
  return plane;
}

std::optional<int> GridAxisOf(const EdgeSpan& span) {
  int along = -1;
  for (int axis = 0; axis < kAxes; ++axis) {
    if (Coordinate(span.a, axis) != Coordinate(span.b, axis)) {
      if (along >= 0) {
        return std::nullopt;
      }
      along = axis;
    }
  }
  return along >= 0 ? std::optional(along) : std::nullopt;
}

// A polygon edge along a grid line, cut at the cell borders, so it meets the cell edges beside it.
std::vector<EdgeSpan> AtCellBorders(const EdgeSpan& edge) {
  const auto along = GridAxisOf(edge);
  if (!along) {
    return {edge};
  }
  std::vector<EdgeSpan> pieces;
  EdgePoint from = edge.a;
  const int end = Coordinate(edge.b, *along);
  for (int border = (Coordinate(edge.a, *along) / kSteps + 1) * kSteps; border < end; border += kSteps) {
    EdgePoint to = from;
    (*along == 0 ? to.x : (*along == 1 ? to.y : to.z)) = border;
    pieces.push_back({from, to});
    from = to;
  }
  pieces.push_back({from, edge.b});
  return pieces;
}

// What one plane's edges leave once the pieces two of them share, however split, cancel.
std::vector<EdgeSpan> LeftOver(const std::vector<EdgeSpan>& polygon_edges,
                           const std::unordered_set<EdgeSpan, EdgeSpanHash>& cell_edges) {
  std::vector<EdgeSpan> spans;
  for (const EdgeSpan& edge : polygon_edges) {
    const std::vector<EdgeSpan> pieces = AtCellBorders(edge);
    spans.insert(spans.end(), pieces.begin(), pieces.end());
  }
  const SpanVertices vertices(spans);  // only the polygons' ends: a cell edge is unit and splits at few
  spans.insert(spans.end(), cell_edges.begin(), cell_edges.end());
  std::unordered_set<EdgeSpan, EdgeSpanHash> odd;
  for (const EdgeSpan& edge : spans) {
    for (const EdgeSpan& piece : vertices.Split(edge)) {
      if (!odd.insert(piece).second) {
        odd.erase(piece);
      }
    }
  }
  return {odd.begin(), odd.end()};
}

std::vector<EdgeSpan> Joined(std::vector<EdgeSpan> spans) {
  std::ranges::sort(spans, [](const EdgeSpan& left, const EdgeSpan& right) {
    const auto left_axis = GridAxisOf(left).value_or(kAxes);
    const auto right_axis = GridAxisOf(right).value_or(kAxes);
    if (left_axis != right_axis) {
      return left_axis < right_axis;
    }
    return std::tie(left.a.x, left.a.y, left.a.z, left.b.x, left.b.y, left.b.z) <
           std::tie(right.a.x, right.a.y, right.a.z, right.b.x, right.b.y, right.b.z);
  });
  std::vector<EdgeSpan> joined;
  for (const EdgeSpan& span : spans) {
    const auto axis = GridAxisOf(span);
    if (!joined.empty() && axis && GridAxisOf(joined.back()) == axis && joined.back().b == span.a) {
      joined.back().b = span.b;
    } else {
      joined.push_back(span);
    }
  }
  return joined;
}

}  // namespace

size_t EdgePlaneHash::operator()(const EdgePlane& plane) const {
  size_t seed = 0;
  for (const int value : {plane.nx, plane.ny, plane.nz, plane.offset}) {
    Mix(seed, value);
  }
  return seed;
}

size_t EdgeSpanHash::operator()(const EdgeSpan& span) const {
  size_t seed = 0;
  for (const int value : {span.a.x, span.a.y, span.a.z, span.b.x, span.b.y, span.b.z}) {
    Mix(seed, value);
  }
  return seed;
}

EdgeSpan OutlineEdges::Ordered(EdgePoint a, EdgePoint b) {
  return Before(b, a) ? EdgeSpan {b, a} : EdgeSpan {a, b};
}

void OutlineEdges::AddCellFace(const Eigen::Vector3i& cell, int axis, bool far_side) {
  const int u = (axis + 1) % kAxes;
  const int v = (axis + 2) % kAxes;
  std::array<int, kAxes> corner {cell.x() * kSteps, cell.y() * kSteps, cell.z() * kSteps};
  corner[axis] += far_side ? kSteps : 0;
  const auto point = [&](int along_u, int along_v) {
    std::array<int, kAxes> at = corner;
    at[u] += along_u * kSteps;
    at[v] += along_v * kSteps;
    return EdgePoint {at[0], at[1], at[2]};
  };
  std::array<int, kAxes> normal {};
  normal[axis] = kSteps;
  const EdgePlane plane {normal[0], normal[1], normal[2], corner[axis]};
  const std::array corners {point(0, 0), point(1, 0), point(1, 1), point(0, 1)};
  for (size_t i = 0; i < corners.size(); ++i) {
    const EdgeSpan edge = Ordered(corners[i], corners[(i + 1) % corners.size()]);
    auto& cell_edges = planes_[plane].cell_edges;
    if (!cell_edges.insert(edge).second) {
      cell_edges.erase(edge);
    }
  }
}

void OutlineEdges::AddPolygon(std::span<const Eigen::Vector3f> polygon, const Eigen::Vector3f& unit_normal) {
  auto& edges = planes_[PlaneOf(unit_normal, polygon.front())].polygon_edges;
  for (size_t i = 0; i < polygon.size(); ++i) {
    const EdgePoint from = QuantizedPoint(polygon[i]);
    const EdgePoint to = QuantizedPoint(polygon[(i + 1) % polygon.size()]);
    if (from != to) {
      edges.push_back(Ordered(from, to));
    }
  }
}

std::vector<OverlaySegment> OutlineEdges::Segments(const Rgba& color) const {
  std::unordered_set<EdgeSpan, EdgeSpanHash> unique;
  for (const auto& [plane, edges] : planes_) {
    if (edges.polygon_edges.empty()) {
      unique.insert(edges.cell_edges.begin(), edges.cell_edges.end());
      continue;
    }
    const std::vector<EdgeSpan> left = LeftOver(edges.polygon_edges, edges.cell_edges);
    unique.insert(left.begin(), left.end());
  }
  const std::vector<EdgeSpan> lines = Joined({unique.begin(), unique.end()});
  std::vector<OverlaySegment> segments;
  segments.reserve(lines.size());
  const auto to_cells = [](const EdgePoint& point) -> Eigen::Vector3f {
    return Eigen::Vector3f(static_cast<float>(point.x), static_cast<float>(point.y), static_cast<float>(point.z)) /
           static_cast<float>(kSteps);
  };
  for (const EdgeSpan& span : lines) {
    segments.push_back({.a = to_cells(span.a), .b = to_cells(span.b), .color = color});
  }
  return segments;
}

}  // namespace z13::station
