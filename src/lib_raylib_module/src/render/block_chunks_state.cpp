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

#include "block_chunks_state.h"

#include <algorithm>
#include <iterator>
#include <utility>

#include <raymath.h>

#include <primitives/chunk_mesh.h>
#include <primitives/draw_order.h>

#include "block_checker.h"
#include "block_model.h"
#include "render_components.h"
#include "render_stats.h"
#include "view_culling.h"

namespace z13::raylib {

namespace {

ChunkSyncKey KeyOf(int chunk_cells, BlockChunks::OptionalPalette palette) {
  return {
      .chunk_cells = chunk_cells,
      .palette_hash = palette ? std::optional(palette->get().hash) : std::nullopt,
  };
}

Eigen::AlignedBox3f InMeters(const z13::building::primitives::CellBox& cells) {
  return {cells.min.cast<float>() * z13::station::kCellSize, cells.End().cast<float>() * z13::station::kCellSize};
}

// Chunks go in key order, not the map's: SortForDrawing breaks distance ties by index, so a
// rehash would otherwise swap equally distant panes.
std::vector<GlassChunk> GroupGlass(std::span<const z13::station::Block> transparent, int chunk_cells) {
  const z13::building::primitives::ChunkBlocks grouped =
      z13::building::primitives::GroupByChunk(transparent, chunk_cells);
  std::vector<Eigen::Vector3i> keys;
  std::ranges::transform(grouped, std::back_inserter(keys), [](const auto& entry) { return entry.first; });
  std::ranges::sort(keys, [](const Eigen::Vector3i& a, const Eigen::Vector3i& b) {
    return std::lexicographical_compare(a.data(), a.data() + a.size(), b.data(), b.data() + b.size());
  });
  std::vector<GlassChunk> glass;
  for (const Eigen::Vector3i& key : keys) {
    GlassChunk chunk;
    for (const z13::station::Block& block : grouped.at(key)) {
      const Eigen::AlignedBox3f bounds = InMeters(z13::building::primitives::OccupiedCells(block));
      chunk.bounds.extend(bounds);
      chunk.blocks.push_back({.block = block, .bounds = bounds});
    }
    glass.push_back(std::move(chunk));
  }
  return glass;
}

}  // namespace

bool BlockChunks::State::SyncedWith(int chunk_cells, OptionalPalette palette) const {
  return synced_with_ == KeyOf(chunk_cells, palette);
}

void BlockChunks::State::Sync(
    std::span<const z13::station::Block> blocks, OptionalPalette palette, int chunk_cells) {
  if (!SyncedWith(chunk_cells, palette)) {
    chunks_ = {};
    synced_with_ = KeyOf(chunk_cells, palette);
  }
  std::vector<z13::station::Block> opaque;
  std::vector<z13::station::Block> transparent;
  for (const z13::station::Block& block : blocks) {
    (z13::building::primitives::IsTransparent(block, palette) ? transparent : opaque).push_back(block);
  }
  glass_ = GroupGlass(transparent, chunk_cells);
  transparent_count_ = transparent.size();

  z13::building::primitives::ChunkBlocks grouped = z13::building::primitives::GroupByChunk(opaque, chunk_cells);
  std::erase_if(chunks_, [&grouped](const auto& chunk) { return !grouped.contains(chunk.first); });
  for (auto& [key, chunk_blocks] : grouped) {
    z13::building::primitives::SortBlocks(chunk_blocks);
    const auto existing = chunks_.find(key);
    if (existing == chunks_.end() || existing->second.blocks != chunk_blocks) {
      chunks_.insert_or_assign(key, Build(std::move(chunk_blocks), palette));
    }
  }
}

ChunkModels BlockChunks::State::Build(std::vector<z13::station::Block> blocks, OptionalPalette palette) const {
  z13::building::primitives::ChunkMesh mesh = z13::building::primitives::BuildChunkMesh(blocks, palette);
  ChunkModels chunk {.blocks = std::move(blocks), .bounds = InMeters(mesh.bounds)};
  std::ranges::transform(mesh.parts, std::back_inserter(chunk.parts), [this](const auto& part) {
    return ChunkPart {.material = part.material, .model = LoadBlockModel(part.mesh, lighting_shader_)};
  });
  return chunk;
}

void BlockChunks::State::DrawOpaque(
    const ViewCulling& culling, const Lighting& lighting, RenderStats& stats) const {
  stats.chunks = chunks_.size();
  for (const auto& [key, chunk] : chunks_) {
    if (!culling.Visible(chunk.bounds)) {
      continue;
    }
    ++stats.chunks_drawn;
    for (const ChunkPart& part : chunk.parts) {
      UseChecker(lighting, part.material);
      DrawModel(*part.model, Vector3Zero(), 1.f, WHITE);
      ++stats.meshes_drawn;
    }
  }
}

std::vector<z13::station::Block> BlockChunks::State::VisibleTransparent(const ViewCulling& culling) const {
  std::vector<z13::station::Block> visible;
  for (const GlassChunk& chunk : glass_) {
    if (!culling.Visible(chunk.bounds)) {
      continue;
    }
    for (const GlassBlock& glass : chunk.blocks) {
      if (culling.Visible(glass.bounds)) {
        visible.push_back(glass.block);
      }
    }
  }
  return visible;
}

}  // namespace z13::raylib
