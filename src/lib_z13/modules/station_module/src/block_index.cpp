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

#include "block_index.h"

#include <algorithm>
#include <limits>

namespace z13::station {

namespace {

constexpr int kChunkCells = 16;

// Prime multipliers spreading neighbouring chunks across buckets.
constexpr size_t kHashY = 73856093;
constexpr size_t kHashZ = 19349663;

// Floor division, so negative cells land in the chunk below zero.
Eigen::Vector3i ChunkOf(const Eigen::Vector3i& cell) {
  return cell.unaryExpr([](int c) { return c >= 0 ? c / kChunkCells : -((-c - 1) / kChunkCells) - 1; });
}

}  // namespace

size_t BlockIndex::ChunkHash::operator()(const Eigen::Vector3i& chunk) const {
  return static_cast<size_t>(chunk.x()) ^ (static_cast<size_t>(chunk.y()) * kHashY) ^
         (static_cast<size_t>(chunk.z()) * kHashZ);
}

template <class Visit>
void BlockIndex::ForEachChunk(const z13::primitives::CellBox& cells, Visit visit) const {
  const Eigen::Vector3i first = ChunkOf(cells.min);
  const Eigen::Vector3i last = ChunkOf(cells.End() - Eigen::Vector3i::Ones());
  for (int x = first.x(); x <= last.x(); ++x) {
    for (int y = first.y(); y <= last.y(); ++y) {
      for (int z = first.z(); z <= last.z(); ++z) {
        visit(Eigen::Vector3i(x, y, z));
      }
    }
  }
}

void BlockIndex::Clear() {
  boxes_.clear();
  chunks_.clear();
}

void BlockIndex::Insert(flecs::entity_t block, const z13::primitives::CellBox& cells) {
  Erase(block);
  if ((cells.extent.array() < 1).any()) {
    return;
  }
  boxes_.emplace(block, cells);
  ForEachChunk(cells, [this, block](const Eigen::Vector3i& chunk) { chunks_[chunk].push_back(block); });
}

void BlockIndex::Erase(flecs::entity_t block) {
  const auto it = boxes_.find(block);
  if (it == boxes_.end()) {
    return;
  }
  ForEachChunk(it->second, [this, block](const Eigen::Vector3i& chunk) {
    const auto bucket = chunks_.find(chunk);
    if (bucket == chunks_.end()) {
      return;
    }
    std::erase(bucket->second, block);
    if (bucket->second.empty()) {
      chunks_.erase(bucket);
    }
  });
  boxes_.erase(it);
}

std::optional<flecs::entity_t> BlockIndex::At(const Eigen::Vector3i& cell) const {
  const auto bucket = chunks_.find(ChunkOf(cell));
  if (bucket == chunks_.end()) {
    return std::nullopt;
  }
  const auto hit = std::ranges::find_if(bucket->second, [this, &cell](flecs::entity_t block) {
    return boxes_.at(block).Contains(cell);
  });
  return hit != bucket->second.end() ? std::optional(*hit) : std::nullopt;
}

bool BlockIndex::Overlaps(const z13::primitives::CellBox& cells) const {
  bool overlaps = false;
  ForEachChunk(cells, [this, &cells, &overlaps](const Eigen::Vector3i& chunk) {
    const auto bucket = chunks_.find(chunk);
    if (overlaps || bucket == chunks_.end()) {
      return;
    }
    overlaps = std::ranges::any_of(
        bucket->second, [this, &cells](flecs::entity_t block) { return boxes_.at(block).Overlaps(cells); });
  });
  return overlaps;
}

// Steps cell by cell along the segment (Amanatides & Woo).
std::optional<flecs::entity_t> BlockIndex::Raycast(const Eigen::Vector3f& from, const Eigen::Vector3f& to) const {
  const Eigen::Vector3f direction = to - from;
  if (direction.isZero()) {
    return At(from.array().floor().cast<int>());
  }

  Eigen::Vector3i cell = from.array().floor().cast<int>();
  Eigen::Vector3i step = Eigen::Vector3i::Zero();
  Eigen::Vector3f next_crossing = Eigen::Vector3f::Constant(std::numeric_limits<float>::infinity());
  Eigen::Vector3f crossing_interval = next_crossing;
  for (int axis = 0; axis < 3; ++axis) {
    if (direction[axis] == 0.f) {
      continue;
    }
    step[axis] = direction[axis] > 0.f ? 1 : -1;
    const float boundary = static_cast<float>(cell[axis] + (step[axis] > 0 ? 1 : 0));
    next_crossing[axis] = (boundary - from[axis]) / direction[axis];
    crossing_interval[axis] = static_cast<float>(step[axis]) / direction[axis];
  }

  float travelled = 0.f;
  while (travelled <= 1.f) {
    if (const auto block = At(cell)) {
      return block;
    }
    int axis = 0;
    next_crossing.minCoeff(&axis);
    travelled = next_crossing[axis];
    cell[axis] += step[axis];
    next_crossing[axis] += crossing_interval[axis];
  }
  return std::nullopt;
}

}  // namespace z13::station
