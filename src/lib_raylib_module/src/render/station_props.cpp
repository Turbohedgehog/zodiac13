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

#include "station_props.h"

#include <cmath>
#include <format>

#include <lib_core/utils/log.h>
#include <primitives/decor.h>
#include <primitives/station_assets.h>

#include "../tools/assimp_loader.h"
#include "../tools/math_convert.h"
#include "render_resources.h"
#include "view_culling.h"

namespace z13::raylib {

namespace {

namespace primitives = z13::building::primitives;

std::expected<std::shared_ptr<::Model>, std::string> LoadPropModel(
    const AssetSource& assets, std::string_view name, ::Shader lighting_shader) {
  const std::string file = primitives::PropModelFile(name).generic_string();
  auto loaded = LoadModelFromAsset(assets, file);
  if (!loaded) {
    return std::unexpected(loaded.error());
  }
  for (const std::string& warning : loaded->warnings) {
    log_warn("[raylib] prop '{}': {}", file, warning);
  }
  for (int i = 0; i < loaded->model.materialCount; ++i) {
    loaded->model.materials[i].shader = lighting_shader;
  }
  return MakeManagedModel(loaded->model, lighting_shader.id);
}

Eigen::AlignedBox3f BoundsOf(const ::Model& model, const Eigen::Matrix4f& transform) {
  Eigen::AlignedBox3f bounds;
  for (int i = 0; i < model.meshCount; ++i) {
    const ::BoundingBox box = GetMeshBoundingBox(model.meshes[i]);
    for (const float x : {box.min.x, box.max.x}) {
      for (const float y : {box.min.y, box.max.y}) {
        for (const float z : {box.min.z, box.max.z}) {
          bounds.extend((transform * Eigen::Vector4f(x, y, z, 1.F)).head<3>());
        }
      }
    }
  }
  return bounds;
}

}  // namespace

std::expected<StationProps, std::string> StationProps::Load(
    const AssetSource& assets, std::string_view scene, ::Shader lighting_shader) {
  StationProps result;
  const auto json = assets.ReadAsset(primitives::DecorFile(scene));
  if (!json) {
    return result;
  }
  const auto decor = primitives::ParseDecor(*json);
  if (!decor) {
    return std::unexpected(decor.error());
  }
  std::unordered_map<std::string, std::shared_ptr<::Model>> models;
  result.replaced_ = decor->replaces;
  result.props_.reserve(decor->props.size());
  for (const primitives::Prop& prop : decor->props) {
    auto found = models.find(prop.model);
    if (found == models.end()) {
      auto model = LoadPropModel(assets, prop.model, lighting_shader);
      if (!model) {
        return std::unexpected(std::format("prop '{}': {}", prop.model, model.error()));
      }
      found = models.emplace(prop.model, *std::move(model)).first;
    }
    Eigen::Affine3f pose = Eigen::Affine3f::Identity();
    pose.translate(prop.position);
    pose.rotate(Eigen::AngleAxisf(prop.yaw, Eigen::Vector3f::UnitZ()));
    pose.rotate(Eigen::AngleAxisf(prop.pitch, Eigen::Vector3f::UnitX()));
    const Eigen::Matrix4f matrix = pose.matrix();
    result.props_.push_back({.model = found->second,
                             .transform = EigenToRaylibMatrix(matrix),
                             .bounds = BoundsOf(*found->second, matrix)});
  }
  return result;
}

size_t StationProps::Draw(const ViewCulling& culling) const {
  size_t drawn = 0;
  for (const PlacedProp& prop : props_) {
    if (!culling.Visible(prop.bounds)) {
      continue;
    }
    const ::Model& model = *prop.model;
    for (int i = 0; i < model.meshCount; ++i) {
      DrawMesh(model.meshes[i], model.materials[model.meshMaterial[i]], prop.transform);
    }
    ++drawn;
  }
  return drawn;
}

}  // namespace z13::raylib
