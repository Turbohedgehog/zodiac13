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

#include "block_chunks.h"

#include "block_chunks_state.h"

namespace z13::raylib {

BlockChunks::BlockChunks(::Shader lighting_shader) : state_(std::make_shared<State>(lighting_shader)) {
}

bool BlockChunks::SyncedWith(int chunk_cells, OptionalPalette palette) const {
  return state_->SyncedWith(chunk_cells, palette);
}

void BlockChunks::Sync(std::span<const z13::station::Block> blocks, OptionalPalette palette, int chunk_cells) {
  state_->Sync(blocks, palette, chunk_cells);
}

void BlockChunks::DrawOpaque(const ViewCulling& culling, const Lighting& lighting, RenderStats& stats) const {
  state_->DrawOpaque(culling, lighting, stats);
}

std::vector<z13::station::Block> BlockChunks::VisibleTransparent(const ViewCulling& culling) const {
  return state_->VisibleTransparent(culling);
}

size_t BlockChunks::TransparentCount() const {
  return state_->TransparentCount();
}

}  // namespace z13::raylib
