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
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <Eigen/Dense>
// The whole of it: the R-tree alone lacks the strategies it needs for 3D boxes.
#include <boost/geometry.hpp>
#include <boost/geometry/index/rtree.hpp>

#include <primitives/placement.h>

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

struct LeafRef {
  Eigen::Vector3i chunk = Eigen::Vector3i::Zero();
  uint32_t leaf {};

  bool operator==(const LeafRef&) const = default;
};

// The most chunks the station's free space may span, which keeps a stray block far from
// the rest from filling the space in between.
inline constexpr size_t kMaxChunks = 64u * 1024u;

// The free space around and between the sealed boxes, cut per chunk into free boxes (the
// leaves) along the faces of the sealed ones. The chunks cover the station's bounds; all
// outside them is the vacuum.
class FreeSpace {
 public:
  using Chunks = std::unordered_map<Eigen::Vector3i, std::shared_ptr<const FreeChunk>,
                                    z13::building::primitives::CellHash>;

  FreeSpace() = default;
  static std::expected<FreeSpace, std::string> Build(const std::vector<z13::building::primitives::CellBox>& sealed,
                                                     int chunk_cells);

  int ChunkCells() const { return chunk_cells_; }
  // The sealed boxes' bounds and a free cell around them; zero without any.
  const z13::building::primitives::CellBox& Bounds() const { return bounds_; }
  // The cells the chunks span, Bounds() rounded out to whole chunks.
  const z13::building::primitives::CellBox& Area() const { return area_; }
  const Chunks& AllChunks() const { return chunks_; }
  size_t LeafCount() const;
  // The chunk keys by AnchorBefore, the order leaves are numbered in.
  std::vector<Eigen::Vector3i> OrderedChunks() const;

  const z13::building::primitives::CellBox& Leaf(const LeafRef& ref) const;
  // Nothing for a sealed cell or one outside Area().
  std::optional<LeafRef> LeafAt(const Eigen::Vector3i& cell) const;
  // The leaves sharing cells with `box`.
  std::vector<LeafRef> LeavesIn(const z13::building::primitives::CellBox& box) const;
  // The leaves sharing part of a face with the leaf, not just an edge or a corner.
  std::vector<LeafRef> Neighbours(const LeafRef& ref) const;
  // Whether the leaf reaches the outside of Area(), which is open to space.
  bool OnOuterFace(const z13::building::primitives::CellBox& leaf) const;

 private:
  int chunk_cells_ {};
  z13::building::primitives::CellBox bounds_;
  z13::building::primitives::CellBox area_;
  Chunks chunks_;
};

}  // namespace z13::station::rooms
