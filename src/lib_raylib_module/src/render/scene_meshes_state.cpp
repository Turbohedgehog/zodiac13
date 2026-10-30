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

#include "scene_meshes_state.h"

#include <algorithm>
#include <unordered_set>
#include <vector>

#include <raymath.h>

#include "block_checker.h"
#include "render_resources.h"

namespace z13::raylib {

namespace {

constexpr size_t kTexcoordsPerVertex = 2;

::Color ToRaylibColor(const z13::Rgba& color) {
  return {color[0], color[1], color[2], color[3]};
}

std::vector<float> Flattened(const std::vector<Eigen::Vector3f>& vectors) {
  std::vector<float> flat;
  flat.reserve(vectors.size() * 3);
  for (const Eigen::Vector3f& vector : vectors) {
    flat.insert(flat.end(), {vector.x(), vector.y(), vector.z()});
  }
  return flat;
}

std::shared_ptr<::Model> Upload(const z13::render::SceneMesh& source, ::Shader lighting_shader) {
  ::Mesh mesh {};
  mesh.vertexCount = static_cast<int>(source.positions.size());
  mesh.triangleCount = mesh.vertexCount / 3;
  mesh.vertices = CopyToRaylib(Flattened(source.positions));
  mesh.normals = CopyToRaylib(Flattened(source.normals));
  // Unused: the colour comes from the material.
  mesh.texcoords = CopyToRaylib(std::vector<float>(source.positions.size() * kTexcoordsPerVertex));
  UploadMesh(&mesh, false);
  ::Model model = LoadModelFromMesh(mesh);
  if (lighting_shader.id != 0) {
    model.materials[0].shader = lighting_shader;
  }
  model.materials[0].maps[MATERIAL_MAP_DIFFUSE].color = ToRaylibColor(source.color);
  return MakeManagedModel(model, lighting_shader.id);
}

Eigen::AlignedBox3f BoundsOf(const z13::render::SceneMesh& source) {
  Eigen::AlignedBox3f bounds;
  for (const Eigen::Vector3f& position : source.positions) {
    bounds.extend(position);
  }
  return bounds;
}

bool IsDrawable(const z13::render::SceneMesh& source) {
  return !source.positions.empty() && source.positions.size() % 3 == 0 &&
         source.normals.size() == source.positions.size();
}

}  // namespace

void SceneMeshes::State::Sync(std::span<const SceneMeshEntry> entries, ::Shader lighting_shader) {
  std::unordered_set<flecs::entity_t> listed;
  for (const SceneMeshEntry& entry : entries) {
    listed.insert(entry.entity);
    const z13::render::SceneMesh& source = entry.mesh;
    const auto found = uploaded_.find(entry.entity);
    if (found != uploaded_.end() && found->second.revision == source.revision) {
      continue;
    }
    if (!IsDrawable(source)) {
      uploaded_.erase(entry.entity);
      continue;
    }
    uploaded_.insert_or_assign(
        entry.entity, UploadedSceneMesh {.model = Upload(source, lighting_shader),
                                         .revision = source.revision,
                                         .bounds = BoundsOf(source)});
  }
  std::erase_if(uploaded_, [&listed](const auto& item) { return !listed.contains(item.first); });
}

size_t SceneMeshes::State::Draw(const ViewCulling& culling, const Lighting& lighting) const {
  StopChecker(lighting);
  size_t drawn = 0;
  for (const auto& [entity, uploaded] : uploaded_) {
    if (culling.Visible(uploaded.bounds)) {
      DrawModel(*uploaded.model, Vector3Zero(), 1.f, WHITE);
      ++drawn;
    }
  }
  return drawn;
}

}  // namespace z13::raylib
