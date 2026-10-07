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
#include <memory>
#include <span>
#include <vector>

#include <raylib.h>

#include <lib_core/utils/frustum.h>
#include <z13/components/station.h>
#include <z13_primitives/geometry.h>

namespace z13::raylib {

struct Lighting;
struct RenderStats;

// Opaque blocks merged into one model per render chunk and material
// (z13_primitives/chunk_mesh.h); transparent ones are kept apart, to be drawn one by one
// in order. Singleton exempt from the "no pointers" rule (see CLAUDE.md); never state.
class BlockChunks {
 public:
  using Singleton = void;
  using OptionalPalette = z13::building::primitives::OptionalPalette;

  // `lighting_shader` is borrowed by every model's material; id 0 keeps raylib's default.
  explicit BlockChunks(::Shader lighting_shader = {});

  // Whether the last Sync used these; if not, the blocks must be synced even if unchanged.
  bool SyncedWith(int chunk_cells, OptionalPalette palette) const;

  // Rebuilds only the chunks whose blocks differ from the previous call's.
  void Sync(std::span<const z13::station::Block> blocks, OptionalPalette palette, int chunk_cells);

  // Counts the chunks and meshes drawn in `stats`.
  void DrawOpaque(const z13::math::Frustum& frustum, const Lighting& lighting, RenderStats& stats) const;

  const std::vector<z13::station::Block>& Transparent() const;

 private:
  class State;

  std::shared_ptr<State> state_;
};

}  // namespace z13::raylib
