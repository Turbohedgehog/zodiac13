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

#include "block_model.h"

#include <vector>

#include <z13/components/station.h>

#include "render_resources.h"

namespace z13::raylib {

namespace {

constexpr size_t kTexcoordsPerVertex = 2;

::Mesh ToRaylibMesh(const z13::building::primitives::Mesh& source) {
  std::vector<float> vertices;
  std::vector<float> normals;
  for (size_t i = 0; i < source.positions.size(); ++i) {
    const Eigen::Vector3f position = source.positions[i] * z13::station::kCellSize;
    vertices.insert(vertices.end(), {position.x(), position.y(), position.z()});
    normals.insert(normals.end(), {source.normals[i].x(), source.normals[i].y(), source.normals[i].z()});
  }

  ::Mesh mesh {};
  mesh.vertexCount = static_cast<int>(source.positions.size());
  mesh.triangleCount = mesh.vertexCount / 3;
  mesh.vertices = CopyToRaylib(vertices);
  mesh.normals = CopyToRaylib(normals);
  // Unused: the lighting shader paints blocks from their world position.
  mesh.texcoords = CopyToRaylib(std::vector<float>(source.positions.size() * kTexcoordsPerVertex));
  UploadMesh(&mesh, false);
  return mesh;
}

}  // namespace

std::shared_ptr<::Model> LoadBlockModel(const z13::building::primitives::Mesh& mesh, ::Shader lighting_shader) {
  ::Model model = LoadModelFromMesh(ToRaylibMesh(mesh));
  if (lighting_shader.id != 0) {
    model.materials[0].shader = lighting_shader;
  }
  return MakeManagedModel(model, lighting_shader.id);
}

}  // namespace z13::raylib
