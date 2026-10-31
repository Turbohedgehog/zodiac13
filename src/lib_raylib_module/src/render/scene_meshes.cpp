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

#include "scene_meshes.h"

#include "scene_meshes_state.h"

namespace z13::raylib {

SceneMeshes::SceneMeshes() : state_(std::make_shared<State>()) {
}

void SceneMeshes::Sync(std::span<const SceneMeshEntry> entries, ::Shader lighting_shader) {
  state_->Sync(entries, lighting_shader);
}

size_t SceneMeshes::Draw(const ViewCulling& culling, const Lighting& lighting) const {
  return state_->Draw(culling, lighting);
}

size_t SceneMeshes::Count() const {
  return state_->Count();
}

}  // namespace z13::raylib
