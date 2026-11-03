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

#include "chunk_sync.h"

#include <algorithm>
#include <cstdint>

#include <lib_profile/profile.h>
#include <primitives/palette.h>
#include <z13/components/station/grid.h>

namespace z13::raylib {

namespace {

// The blocks of the primitives named in `replaced` leave out: props stand in for them.
std::vector<z13::station::Block> CollectShownBlocks(const BlockQuery& query, const std::vector<std::string>& replaced,
                                                    BlockMeshes::OptionalPalette palette) {
  std::vector<z13::station::Block> blocks = CollectBlocks(query);
  if (replaced.empty() || !palette) {
    return blocks;
  }
  std::vector<uint32_t> hidden;
  for (const std::string& name : replaced) {
    if (const auto primitive = palette->get().Find(name)) {
      hidden.push_back(primitive->get().id);
    }
  }
  std::erase_if(blocks, [&hidden](const z13::station::Block& block) {
    return std::ranges::contains(hidden, block.spec.type_id);
  });
  return blocks;
}

}  // namespace

BlockMeshes::OptionalPalette PaletteOf(const flecs::world& world) {
  const auto* palette = world.try_get<z13::building::primitives::BlockPalette>();
  return palette != nullptr ? BlockMeshes::OptionalPalette(palette->palette) : std::nullopt;
}

std::vector<z13::station::Block> CollectBlocks(const BlockQuery& query) {
  Z13_PROFILE_ZONE("CollectBlocks");
  std::vector<z13::station::Block> blocks;
  query.each([&blocks](const z13::station::Block& block) { blocks.push_back(block); });
  return blocks;
}

std::vector<z13::station::Block> CollectMainGridBlocks(const BlockQuery& query) {
  std::vector<z13::station::Block> blocks = CollectBlocks(query);
  std::erase_if(blocks, [](const z13::station::Block& block) { return block.grid != z13::station::kMainGrid; });
  return blocks;
}

void SyncBlockChunks(BlockChunks& chunks, const BlockQuery& blocks, const z13::station::GridPoses& poses,
                     const z13::RenderTuning& tuning, BlockMeshes::OptionalPalette palette,
                     const std::vector<std::string>& replaced, std::vector<std::string>& synced_replaced) {
  // Checked first: iterating the query resets its changed state.
  if (blocks.changed() || !chunks.SyncedWith(tuning.chunk_cells, palette) || !chunks.PosedBy(poses) ||
      replaced != synced_replaced) {
    chunks.Sync(CollectShownBlocks(blocks, replaced, palette), poses, palette, tuning.chunk_cells);
    synced_replaced = replaced;
  }
}

}  // namespace z13::raylib
