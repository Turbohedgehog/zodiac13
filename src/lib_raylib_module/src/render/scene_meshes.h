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
#include <cstdint>
#include <functional>
#include <memory>
#include <span>

#include <flecs.h>
#include <raylib.h>

#include <rooms/view_culling.h>
#include <z13/components/scene_mesh.h>

namespace z13::raylib {

struct Lighting;
using z13::station::rooms::ViewCulling;

struct SceneMeshEntry {
  flecs::entity_t entity {};
  std::reference_wrapper<const z13::render::SceneMesh> mesh;
};

// The GPU models of every entity's SceneMesh, kept by entity. Singleton exempt from the "no pointers"
// rule (see CLAUDE.md); never state.
class SceneMeshes {
 public:
  using Singleton = void;

  SceneMeshes() = default;

  // Uploads the meshes whose revision changed and drops the entities not listed; `lighting_shader` is borrowed by
  // every material, id 0 keeps raylib's default.
  void Sync(std::span<const SceneMeshEntry> entries, ::Shader lighting_shader);

  size_t Draw(const ViewCulling& culling, const Lighting& lighting) const;

  size_t Count() const;

 private:
  class State;

  std::shared_ptr<State> state_ = std::make_shared<State>();
};

}  // namespace z13::raylib
