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

#include "lights.h"

#include <filesystem>
#include <format>
#include <string>
#include <string_view>

#include <spdlog/spdlog.h>

#include "../tools/shader_asset.h"
#include "../tools/math_convert.h"
#include "render_resources.h"

namespace z13::raylib {

namespace {

const std::filesystem::path kLightingVertexShaderPath = "shaders/lighting.vs";
const std::filesystem::path kLightingFragmentShaderPath = "shaders/lighting.fs";

// Uniform names in assets/shaders/lighting.fs.
constexpr std::string_view kViewPosUniform = "viewPos";
constexpr std::string_view kLightTableUniform = "lightTable";
constexpr std::string_view kLightTableWidthUniform = "lightTableWidth";
constexpr std::string_view kClusterHeadersUniform = "clusterHeaders";
constexpr std::string_view kClusterIndicesUniform = "clusterIndices";
constexpr std::string_view kClusterCountsUniform = "clusterCounts";
constexpr std::string_view kClusterTileUniform = "clusterTile";
constexpr std::string_view kClusterDepthUniform = "clusterDepth";
constexpr std::string_view kViewForwardUniform = "viewForward";
constexpr std::string_view kShadowAtlasUniform = "shadowAtlas";
constexpr std::string_view kShadowTileSizeUniform = "shadowTileSize";
constexpr std::string_view kShadowColumnsUniform = "shadowColumns";
// Each directional light's uniforms are its prefix and these.
constexpr std::string_view kSunPrefix = "sun";
constexpr std::string_view kFillPrefix = "fill";
constexpr std::string_view kEnabledSuffix = "Enabled";
constexpr std::string_view kDirectionSuffix = "Direction";
constexpr std::string_view kColorSuffix = "Color";
constexpr std::string_view kShadowEnabledSuffix = "ShadowEnabled";
constexpr std::string_view kViewProjectionSuffix = "ViewProjection";
constexpr std::string_view kShadowSuffix = "Shadow";
constexpr std::string_view kAmbientSkyUniform = "ambient";
constexpr std::string_view kAmbientGroundUniform = "ambientGround";

int Location(const ::Shader& shader, std::string_view name) {
  return GetShaderLocation(shader, std::string(name).c_str());
}

void SetInt(const ::Shader& shader, int location, int value) {
  SetShaderValue(shader, location, &value, SHADER_UNIFORM_INT);
}

void SetVec3(const ::Shader& shader, int location, const Eigen::Vector3f& value) {
  SetShaderValue(shader, location, value.data(), SHADER_UNIFORM_VEC3);
}

int Location(const ::Shader& shader, std::string_view prefix, std::string_view suffix) {
  return Location(shader, std::format("{}{}", prefix, suffix));
}

DirectionalUniforms FindDirectional(const ::Shader& shader, std::string_view prefix) {
  return {
      .enabled = Location(shader, prefix, kEnabledSuffix),
      .direction = Location(shader, prefix, kDirectionSuffix),
      .color = Location(shader, prefix, kColorSuffix),
      .shadow_enabled = Location(shader, prefix, kShadowEnabledSuffix),
      .view_projection = Location(shader, prefix, kViewProjectionSuffix),
  };
}

void UploadDirectional(const ::Shader& shader, const DirectionalUniforms& uniforms,
                       const std::optional<DirectionalLight>& light) {
  SetInt(shader, uniforms.enabled, light ? 1 : 0);
  const bool shadow = light && light->shadow_view_projection;
  SetInt(shader, uniforms.shadow_enabled, shadow ? 1 : 0);
  if (light) {
    SetVec3(shader, uniforms.direction, light->direction);
    SetVec3(shader, uniforms.color, light->color);
  }
  if (shadow) {
    SetShaderValueMatrix(shader, uniforms.view_projection, EigenToRaylibMatrix(*light->shadow_view_projection));
  }
}

}  // namespace

::Shader LoadLightingShader() {
  ::Shader shader = LoadShaderAsset(kLightingVertexShaderPath, kLightingFragmentShaderPath);
  if (Location(shader, kViewPosUniform) < 0) {
    spdlog::error("[raylib] lighting shader failed to compile");
    SafeUnloadShader(shader);
    return {};
  }
  shader.locs[SHADER_LOC_VECTOR_VIEW] = Location(shader, kViewPosUniform);
  // Each sampler on a unit of its own, away from the texture raylib binds on unit 0.
  SetInt(shader, Location(shader, kSunPrefix, kShadowSuffix), kSunShadowUnit);
  SetInt(shader, Location(shader, kFillPrefix, kShadowSuffix), kFillShadowUnit);
  SetInt(shader, Location(shader, kShadowAtlasUniform), kShadowAtlasUnit);
  SetInt(shader, Location(shader, kLightTableUniform), kLightTableUnit);
  SetInt(shader, Location(shader, kLightTableWidthUniform), kLightTableWidth);
  return shader;
}

LightUniforms FindLightUniforms(const ::Shader& shader) {
  return {
      .cluster_headers = Location(shader, kClusterHeadersUniform),
      .cluster_indices = Location(shader, kClusterIndicesUniform),
      .cluster_counts = Location(shader, kClusterCountsUniform),
      .cluster_tile = Location(shader, kClusterTileUniform),
      .cluster_depth = Location(shader, kClusterDepthUniform),
      .view_forward = Location(shader, kViewForwardUniform),
      .shadow_tile_size = Location(shader, kShadowTileSizeUniform),
      .shadow_columns = Location(shader, kShadowColumnsUniform),
      .sun = FindDirectional(shader, kSunPrefix),
      .fill = FindDirectional(shader, kFillPrefix),
      .ambient_sky = Location(shader, kAmbientSkyUniform),
      .ambient_ground = Location(shader, kAmbientGroundUniform),
  };
}

void UploadShadowAtlas(const ::Shader& shader, const LightUniforms& uniforms, const ShadowAtlasLayout& atlas) {
  SetInt(shader, uniforms.shadow_tile_size, atlas.tile_size);
  SetInt(shader, uniforms.shadow_columns, atlas.columns);
}

void UploadSceneLight(const ::Shader& shader, const LightUniforms& uniforms, const SceneLight& light) {
  UploadDirectional(shader, uniforms.sun, light.sun);
  UploadDirectional(shader, uniforms.fill, light.fill);
  SetVec3(shader, uniforms.ambient_sky, light.ambient_sky);
  SetVec3(shader, uniforms.ambient_ground, light.ambient_ground);
}

}  // namespace z13::raylib
