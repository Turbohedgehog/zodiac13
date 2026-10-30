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
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <Eigen/Dense>

#include <z13/components/rooms.h>

#include "edge_types.h"

namespace z13::station {

struct PlaneEdges {
  // Edges of whole cell faces, each toggled in or out so shared ones cancel at once.
  std::unordered_set<EdgeSpan, EdgeSpanHash> cell_edges;
  // Edges of cut polygons, which share vertices only up to how they were split.
  std::vector<EdgeSpan> polygon_edges;
};

// The outline of a surface made of planar polygons: an edge two coplanar polygons share, however
// they split it, is inside the surface and drops out, so what is left is where it ends or bends.
class OutlineEdges {
 public:
  // A whole face of a grid cell: `far_side` is the one at the cell's far end along `axis`.
  void AddCellFace(const Eigen::Vector3i& cell, int axis, bool far_side);
  // A planar polygon, its vertices in cells, lying in a plane with this unit normal (a sliver's
  // own normal is too rough to tell planes apart).
  void AddPolygon(std::span<const Eigen::Vector3f> polygon, const Eigen::Vector3f& unit_normal);
  std::vector<OverlaySegment> Segments(const Rgba& color) const;

  // How fine the edges' ends are told apart, in steps per cell.
  static constexpr int kStepsPerCell = 1024;

 private:
  static EdgeSpan Ordered(EdgePoint a, EdgePoint b);

  std::unordered_map<EdgePlane, PlaneEdges, EdgePlaneHash> planes_;
};

}  // namespace z13::station
