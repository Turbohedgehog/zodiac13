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
#include <cstdint>
#include <utility>
#include <vector>

// The whole of it: the R-tree alone lacks the strategies it needs for 3D boxes.
#include <boost/geometry.hpp>
#include <boost/geometry/index/rtree.hpp>

#include <primitives/placement.h>
#include <rooms/free_space.h>

namespace z13::station::rooms {

using CellPoint = boost::geometry::model::point<int, 3, boost::geometry::cs::cartesian>;
// Closed at both ends, [min, End()]: boxes sharing a face intersect.
using LeafBox = boost::geometry::model::box<CellPoint>;
using LeafEntry = std::pair<LeafBox, uint32_t>;
// Chunks hold a few leaves each and are bulk-loaded, so the split algorithm barely runs:
// linear is the fastest to build, and queries cost the same with any (f/partition-bench).
inline constexpr size_t kLeafTreeNodeEntries = 16;
using LeafTree = boost::geometry::index::rtree<LeafEntry, boost::geometry::index::linear<kLeafTreeNodeEntries>>;

// The free boxes of one chunk, never changed once built: graphs share unchanged chunks.
struct FreeChunk {
  std::vector<z13::building::primitives::CellBox> leaves;
  LeafTree tree;
};

}  // namespace z13::station::rooms
