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

#include <array>
#include <vector>

#include "edge_types.h"

namespace z13::station {

// The vertices of one plane's polygons, each axis sorted, to find the ones on an edge.
class SpanVertices {
 public:
  explicit SpanVertices(const std::vector<EdgeSpan>& spans);

  std::vector<EdgeSpan> Split(const EdgeSpan& edge) const;

 private:
  std::array<std::vector<EdgePoint>, kEdgeAxes> sorted_;
};

}  // namespace z13::station
