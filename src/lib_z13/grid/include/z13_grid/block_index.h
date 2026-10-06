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
#include <vector>

#include <Eigen/Dense>

#include <building_tuning_generated.h>
#include <z13_primitives/placement.h>

namespace z13::building::grid {

// A block's flecs entity id; this library itself doesn't depend on flecs.
using BlockId = uint64_t;

// Which block occupies which cells, derived from the Block components. Blocks are filed
// by the chunks of `chunk_cells`³ cells they touch rather than per cell: a floor or wall
// covers thousands of cells.
class BlockIndex {
 public:
  // Where a ray first met a block, in cells: the block, the face's outward normal (zero if
  // the ray started inside it) and the point on that face.
  struct RayHit {
    BlockId block {};
    Eigen::Vector3i normal = Eigen::Vector3i::Zero();
    Eigen::Vector3f point = Eigen::Vector3f::Zero();
  };

  using Singleton = void;

  explicit BlockIndex(int chunk_cells = fbs::building::BuildingTuningT {}.index_chunk_cells);

  int ChunkCells() const { return chunk_cells_; }
  void Insert(BlockId block, const z13::building::primitives::CellBox& cells);
  void Erase(BlockId block);
  size_t Size() const { return boxes_.size(); }

  std::optional<BlockId> At(const Eigen::Vector3i& cell) const;
  bool Overlaps(const z13::building::primitives::CellBox& cells) const;
  // Every block sharing a cell with `cells`, in no particular order.
  std::vector<BlockId> Overlapping(const z13::building::primitives::CellBox& cells) const;

  // The first block along the segment, both ends in cells.
  std::optional<BlockId> Raycast(const Eigen::Vector3f& from, const Eigen::Vector3f& to) const;
  std::optional<RayHit> RaycastHit(const Eigen::Vector3f& from, const Eigen::Vector3f& to) const;

 private:
  struct ChunkHash {
    size_t operator()(const Eigen::Vector3i& chunk) const;
  };

  Eigen::Vector3i ChunkOf(const Eigen::Vector3i& cell) const;

  template <class Visit>
  void ForEachChunk(const z13::building::primitives::CellBox& cells, Visit visit) const;

  int chunk_cells_ {};
  std::unordered_map<BlockId, z13::building::primitives::CellBox> boxes_;
  std::unordered_map<Eigen::Vector3i, std::vector<BlockId>, ChunkHash> chunks_;
};

}  // namespace z13::building::grid
