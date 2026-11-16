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
#include <map>
#include <set>
#include <utility>

#include <raymath.h>

#include <lib_profile/profile.h>
#include <primitives/chunk_mesh.h>
#include <primitives/draw_order.h>

#include "block_checker.h"
#include "block_model.h"
#include "../tools/math_convert.h"
#include "render_components.h"
#include "render_stats.h"
#include <rooms/view_culling.h>

namespace z13::raylib {

namespace {

// Enough for every change between two frames a lamp is redrawn in.
constexpr size_t kKeptChanges = 64;

ChunkSyncKey KeyOf(int chunk_cells, BlockChunks::OptionalPalette palette) {
  return {
      .chunk_cells = chunk_cells,
      .palette_hash = palette ? std::optional(palette->get().hash) : std::nullopt,
  };
}

Eigen::AlignedBox3f InMeters(const z13::building::primitives::CellBox& cells) {
  return {cells.min.cast<float>() * z13::station::kCellSize, cells.End().cast<float>() * z13::station::kCellSize};
}

// The world's box around the corners of `box` taken through `pose`.
Eigen::AlignedBox3f Posed(const Eigen::AlignedBox3f& box, const Eigen::Isometry3f& pose) {
  constexpr int kCorners = 8;
  Eigen::AlignedBox3f posed;
  for (int corner = 0; corner < kCorners; ++corner) {
    posed.extend(pose * box.corner(static_cast<Eigen::AlignedBox3f::CornerType>(corner)));
  }
  return posed;
}

// Other grids stand outside the main grid's rooms: the frustum and ViewCulling::GridInView cull them.
bool InView(const ViewCulling& culling, z13::station::GridId grid, const Eigen::AlignedBox3f& bounds) {
  return grid == z13::station::kMainGrid ? culling.Visible(bounds)
                                         : culling.InFrustum(bounds) && culling.GridInView(grid);
}

bool SamePoses(const z13::station::GridPoses& first, const z13::station::GridPoses& second) {
  return first.size() == second.size() && std::ranges::all_of(first, [&second](const auto& entry) {
           const auto other = second.find(entry.first);
           return other != second.end() && other->second.matrix() == entry.second.matrix();
         });
}

void DrawChunk(const ChunkModels& chunk, const Lighting& lighting, RenderStats& stats) {
  for (const ChunkPart& part : chunk.parts) {
    UseChecker(lighting, part.material, part.texture != nullptr);
    // Lent for the draw: the model frees what its material holds, the tile is shared.
    ::Texture2D& slot = part.model->materials[0].maps[MATERIAL_MAP_DIFFUSE].texture;
    const ::Texture2D own = slot;
    if (part.texture) {
      slot = *part.texture;
    }
    DrawModel(*part.model, Vector3Zero(), 1.f, WHITE);
    slot = own;
    ++stats.meshes_drawn;
  }
}

}  // namespace

bool BlockChunks::State::SyncedWith(int chunk_cells, OptionalPalette palette) const {
  return synced_with_ == KeyOf(chunk_cells, palette);
}

bool BlockChunks::State::PosedBy(const z13::station::GridPoses& poses) const {
  return SamePoses(synced_poses_, poses);
}

void BlockChunks::State::Sync(std::span<const z13::station::Block> blocks, const z13::station::GridPoses& poses,
                              OptionalPalette palette, int chunk_cells) {
  Z13_PROFILE_ZONE("BlockChunks::Sync");
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
  // Only the opaque chunks cast shadows.
  synced_poses_ = poses;
  if (const Eigen::AlignedBox3f changed = SyncChunks(opaque_, opaque, poses, palette, chunk_cells);
      !changed.isEmpty()) {
    ++version_;
    changes_.push_back({.version = version_, .bounds = changed});
    if (changes_.size() > kKeptChanges) {
      changes_.pop_front();
    }
    extent_.extend(changed);
  }
  SyncChunks(transparent_, transparent, poses, palette, chunk_cells);
}

Eigen::AlignedBox3f BlockChunks::State::ChangedSince(uint64_t version) const {
  if (version >= version_) {
    return {};
  }
  if (changes_.empty() || changes_.front().version > version + 1) {
    return extent_;
  }
  Eigen::AlignedBox3f changed;
  for (const ChunkChange& change : changes_) {
    if (change.version > version) {
      changed.extend(change.bounds);
    }
  }
  return changed;
}

std::optional<Eigen::AlignedBox3f> BlockChunks::State::OpaqueBounds() const {
  if (opaque_.empty()) {
    return std::nullopt;
  }
  Eigen::AlignedBox3f bounds;
  for (const auto& [key, chunk] : opaque_) {
    bounds.extend(chunk.bounds);
  }
  return bounds;
}

void BlockChunks::State::DrawShadowCasters(const std::function<bool(const Eigen::AlignedBox3f&)>& keep,
                                           const ::Material& material) const {
  for (const auto& [key, chunk] : opaque_) {
    if (!keep(chunk.bounds)) {
      continue;
    }
    for (const ChunkPart& part : chunk.parts) {
      for (int mesh = 0; mesh < part.model->meshCount; ++mesh) {
        DrawMesh(part.model->meshes[mesh], material, part.model->transform);
      }
    }
  }
}

Eigen::AlignedBox3f BlockChunks::State::SyncChunks(ChunkMap& chunks, std::span<const z13::station::Block> blocks,
                                                   const z13::station::GridPoses& poses, OptionalPalette palette,
                                                   int chunk_cells) {
  std::map<z13::station::GridId, std::vector<z13::station::Block>> by_grid;
  for (const z13::station::Block& block : blocks) {
    by_grid[block.grid].push_back(block);
  }
  std::unordered_map<ChunkKey, std::vector<z13::station::Block>, ChunkKeyHash> grouped;
  for (const auto& [grid, grid_blocks] : by_grid) {
    for (auto& [chunk, chunk_blocks] : z13::building::primitives::GroupByChunk(grid_blocks, chunk_cells)) {
      grouped.emplace(ChunkKey {.grid = grid, .chunk = chunk}, std::move(chunk_blocks));
    }
  }
  Eigen::AlignedBox3f changed;
  std::erase_if(chunks, [&grouped, &changed](const auto& chunk) {
    if (grouped.contains(chunk.first)) {
      return false;
    }
    changed.extend(chunk.second.bounds);
    return true;
  });
  for (auto& [key, chunk_blocks] : grouped) {
    z13::building::primitives::SortBlocks(chunk_blocks);
    const Eigen::Isometry3f pose = z13::station::PoseOfGrid(poses, key.grid);
    const auto existing = chunks.find(key);
    if (existing == chunks.end() || existing->second.blocks != chunk_blocks ||
        existing->second.pose.matrix() != pose.matrix()) {
      if (existing != chunks.end()) {
        changed.extend(existing->second.bounds);
      }
      changed.extend(chunks.insert_or_assign(key, Build(std::move(chunk_blocks), pose, palette)).first->second.bounds);
    }
  }
  return changed;
}

ChunkModels BlockChunks::State::Build(
    std::vector<z13::station::Block> blocks, const Eigen::Isometry3f& pose, OptionalPalette palette) {
  z13::building::primitives::ChunkMesh mesh = z13::building::primitives::BuildChunkMesh(
      blocks, palette, z13::building::primitives::DoorLeaves::kApart);
  ChunkModels chunk {.blocks = std::move(blocks), .bounds = Posed(InMeters(mesh.bounds), pose), .pose = pose};
  const ::Matrix transform = EigenToRaylibMatrix(pose.matrix());
  std::ranges::transform(mesh.parts, std::back_inserter(chunk.parts), [this, &transform](const auto& part) {
    ChunkPart built {.material = part.material,
                     .model = LoadBlockModel(part.mesh, lighting_shader_),
                     .texture = part.material.texture ? textures_.Find(*part.material.texture) : nullptr};
    built.model->transform = transform;
    return built;
  });
  return chunk;
}

void BlockChunks::State::DrawOpaque(const ViewCulling& culling, const Lighting& lighting, RenderStats& stats,
                                    const GridDrawHook& before_grid) const {
  stats.chunks = opaque_.size();
  std::set<z13::station::GridId> grids;
  for (const auto& [key, chunk] : opaque_) {
    grids.insert(key.grid);
  }
  // Grid by grid, the main one last, so what is drawn after the chunks keeps the main grid's light.
  const auto main = grids.extract(z13::station::kMainGrid);
  const auto draw_grid = [&](z13::station::GridId grid) {
    before_grid(grid);
    for (const auto& [key, chunk] : opaque_) {
      if (key.grid == grid && InView(culling, key.grid, chunk.bounds)) {
        ++stats.chunks_drawn;
        DrawChunk(chunk, lighting, stats);
      }
    }
  };
  std::ranges::for_each(grids, draw_grid);
  if (!main.empty()) {
    draw_grid(z13::station::kMainGrid);
  }
}

// Whole chunks go from the farthest; panes within one rarely overlap (flat windows).
void BlockChunks::State::DrawTransparent(const ViewCulling& culling, const Eigen::Vector3f& eye,
                                         const Lighting& lighting, RenderStats& stats,
                                         const GridDrawHook& before_grid) {
  stats.glass_chunks = transparent_.size();
  visible_transparent_.clear();
  for (const auto& [key, chunk] : transparent_) {
    if (InView(culling, key.grid, chunk.bounds)) {
      visible_transparent_.push_back({.grid = key.grid, .key = key.chunk, .bounds = chunk.bounds});
    }
  }
  z13::building::primitives::SortFarthestFirst(visible_transparent_, eye);
  stats.glass_chunks_drawn = visible_transparent_.size();
  std::optional<z13::station::GridId> current;
  for (const z13::building::primitives::ChunkBounds& visible : visible_transparent_) {
    if (current != visible.grid) {
      before_grid(visible.grid);
      current = visible.grid;
    }
    DrawChunk(transparent_.at({.grid = visible.grid, .chunk = visible.key}), lighting, stats);
  }
}

}  // namespace z13::raylib
