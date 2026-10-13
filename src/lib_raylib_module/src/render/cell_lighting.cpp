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

#include "cell_lighting.h"

#include <lib_profile/profile.h>

#include "cell_lighting_state.h"

namespace z13::raylib {

CellLighting::CellLighting() : state_(std::make_shared<State>()) {}

bool CellLighting::SyncedWith(z13::building::primitives::OptionalPalette palette) const {
  return state_->SyncedWith(palette);
}

Status CellLighting::Sync(std::span<const z13::station::Block> blocks,
                          z13::building::primitives::OptionalPalette palette) {
  Z13_PROFILE_ZONE("CellLighting::Sync");
  return state_->Sync(blocks, palette);
}

const std::vector<z13::station::light::Lamp>& CellLighting::Lamps() const {
  return state_->Lamps();
}

void CellLighting::Upload(const ::Shader& shader, const LightUniforms& uniforms) const {
  state_->Upload(shader, uniforms);
}

void CellLighting::Bind() const {
  state_->Bind();
}

}  // namespace z13::raylib
