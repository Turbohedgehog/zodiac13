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

#include <string>
#include <vector>

#include <flecs.h>

#include <z13/components/station/block.h>
#include <z13_settings/settings.h>

#include "block_chunks.h"
#include "block_meshes.h"

namespace z13::raylib {

using BlockQuery = flecs::query<const z13::station::Block>;

BlockMeshes::OptionalPalette PaletteOf(const flecs::world& world);

std::vector<z13::station::Block> CollectBlocks(const BlockQuery& query);

// The baked light is the main grid's; the other grids get the lamps' own light only.
std::vector<z13::station::Block> CollectMainGridBlocks(const BlockQuery& query);

// Rebuilds the chunks only when blocks, the chunk size, the palette or the replaced primitives
// changed; `synced_replaced` is what the last rebuild left out.
void SyncBlockChunks(BlockChunks& chunks, const BlockQuery& blocks, const z13::station::GridPoses& poses,
                     const z13::RenderTuning& tuning, BlockMeshes::OptionalPalette palette,
                     const std::vector<std::string>& replaced, std::vector<std::string>& synced_replaced);

}  // namespace z13::raylib
