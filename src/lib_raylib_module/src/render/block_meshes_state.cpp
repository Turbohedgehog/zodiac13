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

#include "block_meshes_state.h"

#include "block_model.h"

namespace z13::raylib {

std::shared_ptr<::Model> BlockMeshes::State::Get(
    const z13::building::primitives::BlockShapeKey& shape, OptionalPalette palette) {
  CachedBlockModel& cached = models_[shape];
  cached.used = true;
  if (!cached.model) {
    cached.model = Build(shape, palette);
  }
  return cached.model;
}

void BlockMeshes::State::ReleaseUnused() {
  std::erase_if(models_, [](const auto& item) { return !item.second.used; });
  for (auto& [shape, cached] : models_) {
    cached.used = false;
  }
}

std::shared_ptr<::Model> BlockMeshes::State::Build(
    const z13::building::primitives::BlockShapeKey& shape, OptionalPalette palette) const {
  return LoadBlockModel(
      z13::building::primitives::BuildMesh(z13::building::primitives::BlockSolids(palette, shape)), lighting_shader_);
}

}  // namespace z13::raylib
