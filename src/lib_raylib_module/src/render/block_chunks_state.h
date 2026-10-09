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
#include <memory>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

#include <Eigen/Dense>
#include <raylib.h>

#include <lib_core/utils/frustum.h>
#include <z13/components/station.h>
#include <primitives/palette.h>
#include <primitives/placement.h>

#include "block_chunks.h"

namespace z13::raylib {

struct ChunkPart {
  z13::building::primitives::Checker material;
  std::shared_ptr<::Model> model;
};

struct ChunkModels {
  // Sorted (SortBlocks), to tell whether the chunk changed.
  std::vector<z13::station::Block> blocks;
  Eigen::AlignedBox3f bounds;
  std::vector<ChunkPart> parts;
};

// What a Sync was made with; another chunk size or palette rebuilds every chunk.
struct ChunkSyncKey {
  int chunk_cells {};
  std::optional<uint64_t> palette_hash;

  bool operator==(const ChunkSyncKey&) const = default;
};

class BlockChunks::State {
 public:
  explicit State(::Shader lighting_shader) : lighting_shader_(lighting_shader) {}

  bool SyncedWith(int chunk_cells, OptionalPalette palette) const;
  void Sync(std::span<const z13::station::Block> blocks, OptionalPalette palette, int chunk_cells);
  void DrawOpaque(const z13::math::Frustum& frustum, const Lighting& lighting, RenderStats& stats) const;
  const std::vector<z13::station::Block>& Transparent() const { return transparent_; }

 private:
  ChunkModels Build(std::vector<z13::station::Block> blocks, OptionalPalette palette) const;

  ::Shader lighting_shader_ {};
  std::optional<ChunkSyncKey> synced_with_;
  std::unordered_map<Eigen::Vector3i, ChunkModels, z13::building::primitives::CellHash> chunks_;
  std::vector<z13::station::Block> transparent_;
};

}  // namespace z13::raylib
