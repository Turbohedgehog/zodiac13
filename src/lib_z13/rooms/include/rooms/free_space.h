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

#include <primitives/placement.h>
#include <rooms/parallel_for.h>

namespace z13::station::rooms {

// The free boxes of one chunk with their R-tree (free_chunk.h, private to the library).
struct FreeChunk;

struct LeafRef {
  Eigen::Vector3i chunk = Eigen::Vector3i::Zero();
  uint32_t leaf {};

  bool operator==(const LeafRef&) const = default;
};

struct LeafRefHash {
  size_t operator()(const LeafRef& ref) const;
};

struct RebuiltSpace;

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
                                                     int chunk_cells, const ParallelFor& parallel = Serial);
  // A copy with the chunks the `changed` boxes touch cut again from `sealed`, and the area
  // fitted to the new bounds; every other chunk is shared.
  std::expected<RebuiltSpace, std::string> Rebuilt(const std::vector<z13::building::primitives::CellBox>& sealed,
                                                   const std::vector<z13::building::primitives::CellBox>& changed,
                                                   const ParallelFor& parallel = Serial) const;

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

const std::vector<z13::building::primitives::CellBox>& LeavesOf(const FreeChunk& chunk);

struct RebuiltSpace {
  FreeSpace space;
  // The chunks cut again or added, by AnchorBefore.
  std::vector<Eigen::Vector3i> rebuilt;
};

}  // namespace z13::station::rooms
