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
#include <optional>
#include <unordered_map>
#include <vector>

#include <Eigen/Dense>
#include <flecs.h>

#include <z13_primitives/placement.h>

namespace z13::station {

// Which block occupies which cells, derived from the Block components. Blocks are filed
// by the chunks of kChunkCells³ cells they touch rather than per cell: a floor or wall
// covers thousands of cells.
class BlockIndex {
 public:
  using Singleton = void;

  void Clear();
  void Insert(flecs::entity_t block, const z13::primitives::CellBox& cells);
  void Erase(flecs::entity_t block);
  size_t Size() const { return boxes_.size(); }

  std::optional<flecs::entity_t> At(const Eigen::Vector3i& cell) const;
  bool Overlaps(const z13::primitives::CellBox& cells) const;

  // The first block along the segment, both ends in cells.
  std::optional<flecs::entity_t> Raycast(const Eigen::Vector3f& from, const Eigen::Vector3f& to) const;

 private:
  struct ChunkHash {
    size_t operator()(const Eigen::Vector3i& chunk) const;
  };

  template <class Visit>
  void ForEachChunk(const z13::primitives::CellBox& cells, Visit visit) const;

  std::unordered_map<flecs::entity_t, z13::primitives::CellBox> boxes_;
  std::unordered_map<Eigen::Vector3i, std::vector<flecs::entity_t>, ChunkHash> chunks_;
};

}  // namespace z13::station
