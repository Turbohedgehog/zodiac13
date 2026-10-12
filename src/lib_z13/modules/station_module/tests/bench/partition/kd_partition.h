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
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

#include <Eigen/Dense>
#include <boost/geometry/geometries/box.hpp>
#include <boost/geometry/geometries/point.hpp>
#include <boost/geometry/index/rtree.hpp>

#include <primitives/placement.h>

namespace z13::testing::partition {

using CellPoint = boost::geometry::model::point<int, 3, boost::geometry::cs::cartesian>;
// Closed at both ends, [min, End()]: boxes sharing a face intersect.
using LeafBox = boost::geometry::model::box<CellPoint>;
using LeafEntry = std::pair<LeafBox, uint32_t>;
// Nodes of up to this many entries, the R*-tree's usual choice for small boxes.
inline constexpr size_t kRtreeNodeEntries = 16;
using LeafTree = boost::geometry::index::rtree<LeafEntry, boost::geometry::index::rstar<kRtreeNodeEntries>>;

struct KdChunk {
  std::vector<z13::building::primitives::CellBox> leaves;
  LeafTree tree;
};

// The rooms of the free space, each with its anchor and volume, like the column map's.
struct KdRooms {
  std::vector<Eigen::Vector3i> anchors;
  std::vector<int64_t> volumes;
};

// A prototype of the free space cut per chunk into free boxes along the faces of the sealed
// ones, kd-tree style, with the boxes kept in a Boost R-tree per chunk and rooms found by
// Boost.Graph. Measured against the column map.
class KdPartition {
 public:
  KdPartition(const std::vector<z13::building::primitives::CellBox>& sealed, int chunk_cells);

  // Rebuilds the chunks `box` touches.
  void Rebuild(const std::vector<z13::building::primitives::CellBox>& sealed,
               const z13::building::primitives::CellBox& box);

  size_t LeafCount() const;
  size_t ChunkCount() const { return chunks_.size(); }

  // The free leaf holding the cell; nothing for a sealed cell or one outside the chunks.
  std::optional<z13::building::primitives::CellBox> LeafAt(const Eigen::Vector3i& cell) const;
  // The free leaves sharing cells with `box`.
  std::vector<z13::building::primitives::CellBox> LeavesIn(const z13::building::primitives::CellBox& box) const;

  // Joins leaves sharing a face; those on the outer faces of the chunks are the vacuum.
  KdRooms FloodRooms() const;

 private:
  using Binned = std::unordered_map<Eigen::Vector3i, std::vector<z13::building::primitives::CellBox>,
                                    z13::building::primitives::CellHash>;

  Binned Bin(const std::vector<z13::building::primitives::CellBox>& sealed) const;
  void BuildChunk(const Eigen::Vector3i& chunk, const std::vector<z13::building::primitives::CellBox>& sealed);
  z13::building::primitives::CellBox ChunkBox(const Eigen::Vector3i& chunk) const;

  int chunk_cells_ {};
  // The chunks span [low_chunk_, high_chunk_), the station's bounds and a free cell around them.
  Eigen::Vector3i low_chunk_ = Eigen::Vector3i::Zero();
  Eigen::Vector3i high_chunk_ = Eigen::Vector3i::Zero();
  std::unordered_map<Eigen::Vector3i, KdChunk, z13::building::primitives::CellHash> chunks_;
};

}  // namespace z13::testing::partition
