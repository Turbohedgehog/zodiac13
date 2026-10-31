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

#include <unordered_map>

#include <Eigen/Dense>
#include <raylib.h>

#include "render_components.h"
#include "scene_meshes.h"

namespace z13::raylib {

struct UploadedSceneMesh {
  std::shared_ptr<::Model> model;
  uint64_t revision {};
  // In meters.
  Eigen::AlignedBox3f bounds;
};

class SceneMeshes::State {
 public:
  void Sync(std::span<const SceneMeshEntry> entries, ::Shader lighting_shader);
  size_t Draw(const ViewCulling& culling, const Lighting& lighting) const;
  size_t Count() const { return uploaded_.size(); }

 private:
  std::unordered_map<flecs::entity_t, UploadedSceneMesh> uploaded_;
};

}  // namespace z13::raylib
