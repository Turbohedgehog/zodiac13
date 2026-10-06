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

#include <vector>

#include <z13/components/station.h>

#include "render_resources.h"

namespace z13::raylib {

namespace {

// The checker texture is 2x2 squares of one pixel each, so one cell spans half a UV unit.
constexpr int kCheckerPixels = 2;
constexpr float kUvPerCell = 0.5f;

constexpr z13::building::primitives::Checker kUnknownMaterial {
    .first = {128, 128, 128, 255},
    .second = {100, 100, 100, 255},
};

::Color ToColor(const z13::building::primitives::Rgba& rgba) {
  return {rgba[0], rgba[1], rgba[2], rgba[3]};
}

::Mesh ToRaylibMesh(const z13::building::primitives::Mesh& source) {
  std::vector<float> vertices;
  std::vector<float> normals;
  std::vector<float> texcoords;
  for (size_t i = 0; i < source.positions.size(); ++i) {
    const Eigen::Vector3f position = source.positions[i] * z13::station::kCellSize;
    const Eigen::Vector2f uv = source.uvs[i] * kUvPerCell;
    vertices.insert(vertices.end(), {position.x(), position.y(), position.z()});
    normals.insert(normals.end(), {source.normals[i].x(), source.normals[i].y(), source.normals[i].z()});
    texcoords.insert(texcoords.end(), {uv.x(), uv.y()});
  }

  ::Mesh mesh {};
  mesh.vertexCount = static_cast<int>(source.positions.size());
  mesh.triangleCount = mesh.vertexCount / 3;
  mesh.vertices = CopyToRaylib(vertices);
  mesh.normals = CopyToRaylib(normals);
  mesh.texcoords = CopyToRaylib(texcoords);
  UploadMesh(&mesh, false);
  return mesh;
}

}  // namespace

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
  const auto primitive = palette ? palette->get().Find(shape.type_id) : std::nullopt;
  const z13::building::primitives::Checker material = primitive ? primitive->get().material : kUnknownMaterial;
  const std::vector<z13::building::primitives::ConvexSolid> solids =
      z13::building::primitives::BlockSolids(palette, shape);
  ::Model model = LoadModelFromMesh(ToRaylibMesh(z13::building::primitives::BuildMesh(solids)));
  const ::Image checker = GenImageChecked(
      kCheckerPixels, kCheckerPixels, 1, 1, ToColor(material.first), ToColor(material.second));
  model.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = LoadTextureFromImage(checker);
  UnloadImage(checker);
  if (lighting_shader_.id != 0) {
    model.materials[0].shader = lighting_shader_;
  }
  return MakeManagedModel(model, lighting_shader_.id);
}

}  // namespace z13::raylib
