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
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

#include <Eigen/Dense>
#include <raylib.h>

#include <z13/components/station.h>
#include <primitives/draw_order.h>
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

using ChunkMap = std::unordered_map<Eigen::Vector3i, ChunkModels, z13::building::primitives::CellHash>;

// What a Sync was made with; another chunk size or palette rebuilds every chunk.
struct ChunkSyncKey {
  int chunk_cells {};
  std::optional<uint64_t> palette_hash;

  bool operator==(const ChunkSyncKey&) const = default;
};

// Where the opaque chunks changed, as of a version.
struct ChunkChange {
  uint64_t version {};
  Eigen::AlignedBox3f bounds;
};

class BlockChunks::State {
 public:
  explicit State(::Shader lighting_shader) : lighting_shader_(lighting_shader) {}

  bool SyncedWith(int chunk_cells, OptionalPalette palette) const;
  void Sync(std::span<const z13::station::Block> blocks, OptionalPalette palette, int chunk_cells);
  uint64_t Version() const { return version_; }
  Eigen::AlignedBox3f ChangedSince(uint64_t version) const;
  std::optional<Eigen::AlignedBox3f> OpaqueBounds() const;
  void DrawShadowCasters(const std::function<bool(const Eigen::AlignedBox3f&)>& keep,
                         const ::Material& material) const;
  void DrawOpaque(const ViewCulling& culling, const Lighting& lighting, RenderStats& stats) const;
  void DrawTransparent(
      const ViewCulling& culling, const Eigen::Vector3f& eye, const Lighting& lighting, RenderStats& stats);

 private:
  // The bounds of the chunks rebuilt or dropped, before and after.
  Eigen::AlignedBox3f SyncChunks(ChunkMap& chunks, std::span<const z13::station::Block> blocks, OptionalPalette palette,
                  int chunk_cells) const;
  ChunkModels Build(std::vector<z13::station::Block> blocks, OptionalPalette palette) const;

  ::Shader lighting_shader_ {};
  std::optional<ChunkSyncKey> synced_with_;
  uint64_t version_ {};
  // The latest changes; one older than these counts as a change of all of extent_.
  std::deque<ChunkChange> changes_;
  // Of every opaque chunk there has been.
  Eigen::AlignedBox3f extent_;
  ChunkMap opaque_;
  ChunkMap transparent_;
  // Kept between frames so drawing allocates nothing.
  std::vector<z13::building::primitives::ChunkBounds> visible_transparent_;
};

}  // namespace z13::raylib
