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
#include <functional>
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

void DrawChunk(const ChunkModels& chunk, const Lighting& lighting, RenderStats& stats) {
  for (const ChunkPart& part : chunk.parts) {
    UseChecker(lighting, part.material);
    DrawModel(*part.model, Vector3Zero(), 1.f, WHITE);
    ++stats.meshes_drawn;
  }
}

}  // namespace

bool BlockChunks::State::SyncedWith(int chunk_cells, OptionalPalette palette) const {
  return synced_with_ == KeyOf(chunk_cells, palette);
}

void BlockChunks::State::Sync(
    std::span<const z13::station::Block> blocks, OptionalPalette palette, int chunk_cells) {
  if (!SyncedWith(chunk_cells, palette)) {
    opaque_ = {};
    transparent_ = {};
    synced_with_ = KeyOf(chunk_cells, palette);
  }
  std::vector<z13::station::Block> opaque;
  std::vector<z13::station::Block> transparent;
  for (const z13::station::Block& block : blocks) {
    (z13::building::primitives::IsTransparent(block, palette) ? transparent : opaque).push_back(block);
  }
  transparent_blocks_ = transparent.size();
  SyncChunks(opaque_, opaque, palette, chunk_cells);
  SyncChunks(transparent_, transparent, palette, chunk_cells);
}

void BlockChunks::State::SyncChunks(
    ChunkMap& chunks, std::span<const z13::station::Block> blocks, OptionalPalette palette, int chunk_cells) const {
  z13::building::primitives::ChunkBlocks grouped = z13::building::primitives::GroupByChunk(blocks, chunk_cells);
  std::erase_if(chunks, [&grouped](const auto& chunk) { return !grouped.contains(chunk.first); });
  for (auto& [key, chunk_blocks] : grouped) {
    z13::building::primitives::SortBlocks(chunk_blocks);
    const auto existing = chunks.find(key);
    if (existing == chunks.end() || existing->second.blocks != chunk_blocks) {
      chunks.insert_or_assign(key, Build(std::move(chunk_blocks), palette));
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
  stats.chunks = opaque_.size();
  for (const auto& [key, chunk] : opaque_) {
    if (culling.Visible(chunk.bounds)) {
      ++stats.chunks_drawn;
      DrawChunk(chunk, lighting, stats);
    }
  }
}

// Whole chunks go from the farthest; panes within one rarely overlap (flat windows).
void BlockChunks::State::DrawTransparent(
    const ViewCulling& culling, const Eigen::Vector3f& eye, const Lighting& lighting, RenderStats& stats) const {
  stats.glass = transparent_blocks_;
  std::vector<std::reference_wrapper<const ChunkModels>> visible;
  std::vector<z13::building::primitives::ChunkBounds> bounds;
  for (const auto& [key, chunk] : transparent_) {
    if (culling.Visible(chunk.bounds)) {
      visible.emplace_back(chunk);
      bounds.push_back({.key = key, .bounds = chunk.bounds});
    }
  }
  for (const size_t i : z13::building::primitives::FarthestFirst(bounds, eye)) {
    stats.glass_drawn += visible[i].get().blocks.size();
    DrawChunk(visible[i], lighting, stats);
  }
}

}  // namespace z13::raylib
