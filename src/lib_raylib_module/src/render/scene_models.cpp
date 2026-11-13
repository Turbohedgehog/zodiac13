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

#include "scene_models.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <Eigen/Dense>
#include <flecs.h>
#include <raylib.h>

#include <lib_core/state/world_state.h>
#include <lib_core/state/rollback.h>
#include <lib_core/utils/asset_source.h>
#include <lib_core/utils/log.h>
#include <lib_core/utils/math.h>
#include <z13/components/gameplay/gameplay.h>
#include <z13/components/station/station_mode.h>
#include <z13_settings/settings.h>

#include "../tools/assimp_loader.h"
#include "../tools/math_convert.h"
#include "block_checker.h"
#include "render_resources.h"
#include "station_props.h"

namespace z13::raylib {

namespace {

constexpr std::string_view kSpaceshipAsset = "models/spaceship2/spaceship.fbx";

// Points every material of `model` at Lighting's shared shader, and returns
// the id MakeManagedModel needs to skip unloading a shader it doesn't own.
unsigned int ApplyLightingShader(::Model& model, const Lighting& lighting) {
  if (!lighting.res) {
    return 0;
  }
  for (int i = 0; i < model.materialCount; ++i) {
    model.materials[i].shader = *lighting.res->shader;
  }
  return lighting.res->shader->id;
}

RenderModel LoadSpaceship(const Lighting& lighting, const AssetSource& assets, const z13::RenderTuning& tuning) {
  auto loaded = LoadModelFromAsset(assets, kSpaceshipAsset);
  if (!loaded) {
    log_error("[raylib] assimp: {}", loaded.error());
    return RenderModel{};
  }
  for (const std::string& warning : loaded->warnings) {
    log_warn("[raylib] assimp: '{}': {}", kSpaceshipAsset, warning);
  }
  ::Model& model = loaded->model;

  const unsigned int borrowed_shader_id = ApplyLightingShader(model, lighting);

  Eigen::Affine3f transform = Eigen::Affine3f::Identity();
  transform.translate(
      Eigen::Vector3f(tuning.ship_x, tuning.ship_y, tuning.ship_z));
  transform.rotate(
      Eigen::AngleAxisf(z13::math::ToRadians(tuning.ship_pitch_deg), Eigen::Vector3f::UnitX()));
  transform.rotate(
      Eigen::AngleAxisf(z13::math::ToRadians(tuning.ship_yaw_deg), Eigen::Vector3f::UnitY()));
  transform.scale(tuning.ship_scale);
  model.transform = EigenToRaylibMatrix(Eigen::Matrix4f(transform.matrix()));

  auto res = std::make_shared<ModelResources>();
  res->model = MakeManagedModel(model, borrowed_shader_id);
  return RenderModel{.res = std::move(res)};
}

// Flat-shaded: every face gets its own three vertices and normal.
::Mesh GenMeshOctahedron(float radius) {
  constexpr uint32_t kFaces = 8;
  constexpr uint32_t kCornersPerFace = 3;
  constexpr uint32_t kTexcoordsPerVertex = 2;
  constexpr uint32_t kVertexCount = kFaces * kCornersPerFace;
  std::vector<float> vertices;
  std::vector<float> normals;
  for (uint32_t face = 0; face < kFaces; ++face) {
    const Eigen::Vector3f sign((face & 1) ? -1.f : 1.f, (face & 2) ? -1.f : 1.f, (face & 4) ? -1.f : 1.f);
    std::array<Eigen::Vector3f, kCornersPerFace> corners = {
        Eigen::Vector3f(sign.x() * radius, 0.f, 0.f),
        Eigen::Vector3f(0.f, sign.y() * radius, 0.f),
        Eigen::Vector3f(0.f, 0.f, sign.z() * radius),
    };
    if (sign.prod() < 0.f) {
      std::swap(corners[1], corners[2]);  // keep the winding counter-clockwise from outside
    }
    const Eigen::Vector3f normal = sign.normalized();
    for (const Eigen::Vector3f& corner : corners) {
      vertices.insert(vertices.end(), {corner.x(), corner.y(), corner.z()});
      normals.insert(normals.end(), {normal.x(), normal.y(), normal.z()});
    }
  }

  ::Mesh mesh{};
  mesh.vertexCount = static_cast<int>(kVertexCount);
  mesh.triangleCount = static_cast<int>(kFaces);
  mesh.vertices = CopyToRaylib(vertices);
  mesh.normals = CopyToRaylib(normals);
  mesh.texcoords = CopyToRaylib(std::vector<float>(kVertexCount * kTexcoordsPerVertex));
  UploadMesh(&mesh, false);
  return mesh;
}

}  // namespace

Lighting LoadLighting(const AssetSource& assets) {
  ::Shader shader = LoadLightingShader(assets);
  if (shader.id == 0) {
    return Lighting{};
  }

  auto res = std::make_shared<LightingResources>();
  res->shader = MakeManagedShader(shader);
  res->checker = SetupCheckerUniforms(shader);
  res->lights = FindLightUniforms(shader);
  return Lighting{.res = std::move(res)};
}

z13::Status ReloadLighting(const Lighting& lighting, const AssetSource& assets, std::vector<std::string> defines,
                           const ShaderTextOverride& text) {
  if (!lighting.res) {
    return std::unexpected("there is no lighting shader (no window, or it failed to load)");
  }
  LightingResources& res = *lighting.res;
  if (const z13::Status replaced = ReplaceLightingShader(*res.shader, assets, defines, text); !replaced) {
    return replaced;
  }
  res.checker = SetupCheckerUniforms(*res.shader);
  res.lights = FindLightUniforms(*res.shader);
  res.defines = std::move(defines);
  return {};
}

// Waits out a pending restore: a joining client has Gameplay a frame before Welcome's mode.
void ReconcileSpaceship(flecs::world world, const Lighting& lighting, const AssetSource& assets,
                        const z13::RenderTuning& tuning) {
  if (z13::flecs_tools::IsCatchingUp(world)) {
    return;
  }
  const bool wanted = world.has<gameplay::Gameplay>() && !world.has<z13::station::StationMode>();
  if (wanted && !world.has<RenderModel>()) {
    world.set<RenderModel>(LoadSpaceship(lighting, assets, tuning));
  } else if (!wanted && world.has<RenderModel>()) {
    world.remove<RenderModel>();
  }
}

void ReconcileStationProps(
    flecs::world world, const Lighting& lighting, const AssetSource& assets, const z13::RenderTuning& tuning) {
  if (z13::flecs_tools::IsCatchingUp(world)) {
    return;
  }
  const auto* choice = world.try_get<z13::station::StationSceneChoice>();
  const bool wanted = lighting.res && world.has<z13::station::StationMode>() && choice && choice->scene;
  if (wanted && !world.has<StationProps>()) {
    auto props = StationProps::Load(assets, *choice->scene, *lighting.res->shader, tuning.prop_tree_loose_ratio);
    if (props) {
      log_info("[raylib] {} props in '{}'", props->Count(), *choice->scene);
      world.set<StationProps>(*std::move(props));
    } else {
      log_error("[raylib] props of '{}': {}", *choice->scene, props.error());
    }
  } else if (!wanted && world.has<StationProps>()) {
    world.remove<StationProps>();
  }
}

AvatarModel LoadAvatar(const Lighting& lighting, const z13::RenderTuning& tuning) {
  ::Model model = LoadModelFromMesh(GenMeshOctahedron(tuning.avatar_radius_m));
  const unsigned int borrowed_shader_id = ApplyLightingShader(model, lighting);
  auto res = std::make_shared<ModelResources>();
  res->model = MakeManagedModel(model, borrowed_shader_id);
  return AvatarModel{.res = std::move(res)};
}

}  // namespace z13::raylib
