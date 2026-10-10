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

#include <algorithm>
#include <array>
#include <filesystem>
#include <format>
#include <string>
#include <string_view>
#include <vector>

#include <spdlog/spdlog.h>

#include "../tools/asset_path.h"
#include "../tools/math_convert.h"
#include "render_resources.h"

namespace z13::raylib {

namespace {

const std::filesystem::path kLightingVertexShaderPath = "shaders/lighting.vs";
const std::filesystem::path kLightingFragmentShaderPath = "shaders/lighting.fs";

// Uniform names in assets/shaders/lighting.fs.
constexpr std::string_view kViewPosUniform = "viewPos";
constexpr std::string_view kLightCountUniform = "lightCount";
constexpr std::string_view kPositionRadiusUniform = "lightPositionRadius";
constexpr std::string_view kColorShadowUniform = "lightColorShadow";
constexpr std::string_view kReachMinUniform = "lightReachMin";
constexpr std::string_view kReachMaxUniform = "lightReachMax";
constexpr std::string_view kDirectionConeUniform = "lightDirectionCone";
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

// The shader's slot for a light without a shadow.
constexpr float kNoShadow = -1.f;
// The cone of a light shining every way: below any cosine.
constexpr float kNoCone = -2.f;

using Vec4 = std::array<float, 4>;

int Location(const ::Shader& shader, std::string_view name) {
  return GetShaderLocation(shader, std::string(name).c_str());
}

void SetInt(const ::Shader& shader, int location, int value) {
  SetShaderValue(shader, location, &value, SHADER_UNIFORM_INT);
}

void SetVec3(const ::Shader& shader, int location, const Eigen::Vector3f& value) {
  SetShaderValue(shader, location, value.data(), SHADER_UNIFORM_VEC3);
}

void SetVec4Array(const ::Shader& shader, int location, const std::vector<Vec4>& values) {
  SetShaderValueV(shader, location, values.data(), SHADER_UNIFORM_VEC4, static_cast<int>(values.size()));
}

Vec4 WithW(const Eigen::Vector3f& xyz, float w) {
  return {xyz.x(), xyz.y(), xyz.z(), w};
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
  ::Shader shader = LoadShader(AssetPath(kLightingVertexShaderPath).c_str(),
                               AssetPath(kLightingFragmentShaderPath).c_str());
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
  return shader;
}

LightUniforms FindLightUniforms(const ::Shader& shader) {
  return {
      .count = Location(shader, kLightCountUniform),
      .position_radius = Location(shader, kPositionRadiusUniform),
      .color_shadow = Location(shader, kColorShadowUniform),
      .reach_min = Location(shader, kReachMinUniform),
      .reach_max = Location(shader, kReachMaxUniform),
      .direction_cone = Location(shader, kDirectionConeUniform),
      .shadow_tile_size = Location(shader, kShadowTileSizeUniform),
      .shadow_columns = Location(shader, kShadowColumnsUniform),
      .sun = FindDirectional(shader, kSunPrefix),
      .fill = FindDirectional(shader, kFillPrefix),
      .ambient_sky = Location(shader, kAmbientSkyUniform),
      .ambient_ground = Location(shader, kAmbientGroundUniform),
  };
}

void UploadLights(const ::Shader& shader, const LightUniforms& uniforms, std::span<const FrameLight> lights,
                  const ShadowAtlasLayout& atlas) {
  const std::span<const FrameLight> shown = lights.first(std::min(lights.size(), static_cast<size_t>(kMaxLights)));
  SetInt(shader, uniforms.count, static_cast<int>(shown.size()));
  SetInt(shader, uniforms.shadow_tile_size, atlas.tile_size);
  SetInt(shader, uniforms.shadow_columns, atlas.columns);
  if (shown.empty()) {
    return;
  }
  std::vector<Vec4> position_radius;
  std::vector<Vec4> color_shadow;
  std::vector<Vec4> reach_min;
  std::vector<Vec4> reach_max;
  std::vector<Vec4> direction_cone;
  for (const FrameLight& light : shown) {
    position_radius.push_back(WithW(light.position, light.radius));
    color_shadow.push_back(
        WithW(light.color, light.shadow_slot ? static_cast<float>(*light.shadow_slot) : kNoShadow));
    reach_min.push_back(WithW(light.reach.min(), 0.f));
    reach_max.push_back(WithW(light.reach.max(), 0.f));
    direction_cone.push_back(light.cone ? WithW(light.cone->direction, light.cone->cos_half_angle)
                                        : WithW(Eigen::Vector3f::Zero(), kNoCone));
  }
  SetVec4Array(shader, uniforms.position_radius, position_radius);
  SetVec4Array(shader, uniforms.color_shadow, color_shadow);
  SetVec4Array(shader, uniforms.reach_min, reach_min);
  SetVec4Array(shader, uniforms.reach_max, reach_max);
  SetVec4Array(shader, uniforms.direction_cone, direction_cone);
}

void UploadSceneLight(const ::Shader& shader, const LightUniforms& uniforms, const SceneLight& light) {
  UploadDirectional(shader, uniforms.sun, light.sun);
  UploadDirectional(shader, uniforms.fill, light.fill);
  SetVec3(shader, uniforms.ambient_sky, light.ambient_sky);
  SetVec3(shader, uniforms.ambient_ground, light.ambient_ground);
}

}  // namespace z13::raylib
