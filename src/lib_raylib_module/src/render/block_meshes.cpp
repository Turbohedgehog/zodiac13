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

#include "block_meshes.h"

#include "block_meshes_state.h"

namespace z13::raylib {

BlockMeshes::BlockMeshes(::Shader lighting_shader) : state_(std::make_shared<State>(lighting_shader)) {
}

std::shared_ptr<::Model> BlockMeshes::Get(
    const z13::building::primitives::BlockShapeKey& shape, OptionalPalette palette) {
  return state_->Get(shape, palette);
}

void BlockMeshes::ReleaseUnused() {
  state_->ReleaseUnused();
}

}  // namespace z13::raylib
