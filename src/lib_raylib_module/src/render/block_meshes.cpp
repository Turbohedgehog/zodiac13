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

#include <map>
#include <tuple>
#include <vector>

#include <z13/components/station.h>
#include <z13_primitives/geometry.h>

#include "render_resources.h"

namespace z13::raylib {

namespace {

// The checker texture is 2x2 squares of one pixel each, so one cell spans half a UV unit.
constexpr int kCheckerPixels = 2;
constexpr float kUvPerCell = 0.5f;

constexpr z13::primitives::Checker kUnknownMaterial {
    .first = {128, 128, 128, 255},
    .second = {100, 100, 100, 255},
};

using Key = std::tuple<uint32_t, int, int, int>;

::Color ToColor(const z13::primitives::Rgba& rgba) {
  return {rgba[0], rgba[1], rgba[2], rgba[3]};
}

::Mesh ToRaylibMesh(const z13::primitives::Mesh& source) {
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

class BlockMeshes::State {
 public:
  explicit State(::Shader lighting_shader) : lighting_shader_(lighting_shader) {}

  std::shared_ptr<::Model> Get(uint32_t type_id, const Eigen::Vector3i& size, OptionalPalette palette) {
    const Key key {type_id, size.x(), size.y(), size.z()};
    Entry& entry = models_[key];
    entry.used = true;
    if (!entry.model) {
      entry.model = Build(type_id, size, palette);
    }
    return entry.model;
  }

  void ReleaseUnused() {
    std::erase_if(models_, [](const auto& item) { return !item.second.used; });
    for (auto& [key, entry] : models_) {
      entry.used = false;
    }
  }

 private:
  struct Entry {
    std::shared_ptr<::Model> model;
    bool used {};
  };

  std::shared_ptr<::Model> Build(uint32_t type_id, const Eigen::Vector3i& size, OptionalPalette palette) const {
    const auto primitive = palette ? palette->get().Find(type_id) : std::nullopt;
    z13::primitives::Shape shape {.kind = z13::primitives::ShapeKind::kBox};
    z13::primitives::Checker material = kUnknownMaterial;
    if (primitive) {
      shape = primitive->get().shape;
      material = primitive->get().material;
    }
    auto solids = z13::primitives::BuildSolids(shape, size);
    if (!solids) {
      solids = z13::primitives::BuildSolids({.kind = z13::primitives::ShapeKind::kBox}, size);
    }

    ::Model model = LoadModelFromMesh(ToRaylibMesh(z13::primitives::BuildMesh(solids.value_or(
        std::vector<z13::primitives::ConvexSolid> {}))));
    const ::Image checker = GenImageChecked(
        kCheckerPixels, kCheckerPixels, 1, 1, ToColor(material.first), ToColor(material.second));
    model.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = LoadTextureFromImage(checker);
    UnloadImage(checker);
    if (lighting_shader_.id != 0) {
      model.materials[0].shader = lighting_shader_;
    }
    return MakeManagedModel(model, lighting_shader_.id);
  }

  ::Shader lighting_shader_ {};
  std::map<Key, Entry> models_;
};

BlockMeshes::BlockMeshes(::Shader lighting_shader) : state_(std::make_shared<State>(lighting_shader)) {
}

std::shared_ptr<::Model> BlockMeshes::Get(uint32_t type_id, const Eigen::Vector3i& size, OptionalPalette palette) {
  return state_->Get(type_id, size, palette);
}

void BlockMeshes::ReleaseUnused() {
  state_->ReleaseUnused();
}

}  // namespace z13::raylib
