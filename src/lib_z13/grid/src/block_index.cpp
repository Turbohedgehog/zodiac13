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

#include <grid/block_index.h>

#include <algorithm>

#include <grid/cell_walk.h>

#include <building_tuning_generated.h>

namespace z13::building::grid {

int DefaultIndexChunkCells() {
  return fbs::building::BuildingTuningT {}.index_chunk_cells;
}

using z13::building::primitives::ChunkOf;

BlockIndex::BlockIndex(int chunk_cells) : chunk_cells_(chunk_cells) {
}

template <class Visit>
void BlockIndex::ForEachChunk(const z13::building::primitives::CellBox& cells, Visit visit) const {
  const Eigen::Vector3i first = ChunkOf(cells.min, chunk_cells_);
  const Eigen::Vector3i last = ChunkOf(cells.End() - Eigen::Vector3i::Ones(), chunk_cells_);
  for (int x = first.x(); x <= last.x(); ++x) {
    for (int y = first.y(); y <= last.y(); ++y) {
      for (int z = first.z(); z <= last.z(); ++z) {
        visit(Eigen::Vector3i(x, y, z));
      }
    }
  }
}

void BlockIndex::Insert(BlockId block, const z13::building::primitives::CellShape& shape) {
  Erase(block);
  if ((shape.box.extent.array() < 1).any()) {
    return;
  }
  shapes_.emplace(block, shape);
  ForEachChunk(shape.box, [this, block](const Eigen::Vector3i& chunk) { chunks_[chunk].push_back(block); });
}

void BlockIndex::Erase(BlockId block) {
  const auto it = shapes_.find(block);
  if (it == shapes_.end()) {
    return;
  }
  ForEachChunk(it->second.box, [this, block](const Eigen::Vector3i& chunk) {
    const auto bucket = chunks_.find(chunk);
    if (bucket == chunks_.end()) {
      return;
    }
    std::erase(bucket->second, block);
    if (bucket->second.empty()) {
      chunks_.erase(bucket);
    }
  });
  shapes_.erase(it);
}

std::optional<BlockId> BlockIndex::At(const Eigen::Vector3i& cell) const {
  const auto bucket = chunks_.find(ChunkOf(cell, chunk_cells_));
  if (bucket == chunks_.end()) {
    return std::nullopt;
  }
  const auto hit = std::ranges::find_if(bucket->second, [this, &cell](BlockId block) {
    return shapes_.at(block).Contains(cell);
  });
  return hit != bucket->second.end() ? std::optional(*hit) : std::nullopt;
}

template <class Test>
bool BlockIndex::AnyShape(const z13::building::primitives::CellBox& box, Test test) const {
  bool found = false;
  ForEachChunk(box, [this, &test, &found](const Eigen::Vector3i& chunk) {
    const auto bucket = chunks_.find(chunk);
    if (found || bucket == chunks_.end()) {
      return;
    }
    found = std::ranges::any_of(bucket->second, [this, &test](BlockId block) { return test(block, shapes_.at(block)); });
  });
  return found;
}

bool BlockIndex::Overlaps(const z13::building::primitives::CellBox& cells) const {
  return AnyShape(
      cells, [&cells](BlockId, const z13::building::primitives::CellShape& other) { return other.Overlaps(cells); });
}

bool BlockIndex::Overlaps(
    const z13::building::primitives::CellShape& shape, std::span<const BlockId> ignored) const {
  return AnyShape(shape.box, [&shape, ignored](BlockId block, const z13::building::primitives::CellShape& other) {
    return !std::ranges::contains(ignored, block) && other.Overlaps(shape);
  });
}

std::vector<BlockId> BlockIndex::Overlapping(const z13::building::primitives::CellBox& cells) const {
  std::vector<BlockId> found;
  ForEachChunk(cells, [this, &cells, &found](const Eigen::Vector3i& chunk) {
    const auto bucket = chunks_.find(chunk);
    if (bucket == chunks_.end()) {
      return;
    }
    for (const BlockId block : bucket->second) {
      // A block spanning several chunks is filed in each of them.
      if (shapes_.at(block).Overlaps(cells) && std::ranges::find(found, block) == found.end()) {
        found.push_back(block);
      }
    }
  });
  return found;
}

std::optional<BlockId> BlockIndex::Raycast(const Eigen::Vector3f& from, const Eigen::Vector3f& to) const {
  const auto hit = RaycastHit(from, to);
  return hit ? std::optional(hit->block) : std::nullopt;
}

std::optional<BlockIndex::RayHit> BlockIndex::RaycastHit(const Eigen::Vector3f& from, const Eigen::Vector3f& to) const {
  const Eigen::Vector3f direction = to - from;
  if (direction.isZero()) {
    const auto block = At(from.array().floor().cast<int>());
    return block ? std::optional(RayHit {.block = *block, .point = from}) : std::nullopt;
  }

  std::optional<RayHit> hit;
  WalkCells(from, to, [&](const CellStep& at) {
    if (const auto block = At(at.cell)) {
      hit = RayHit {.block = *block, .normal = at.normal, .point = from + direction * at.travelled};
    }
    return hit.has_value();
  });
  return hit;
}

}  // namespace z13::building::grid
