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
#include <optional>
#include <span>

#include <Eigen/Dense>
#include <raylib.h>

namespace z13::raylib {

// assets/shaders/lighting.fs: MAX_LIGHTS.
inline constexpr int kMaxLights = 32;
// raylib's MAX_MATERIAL_MAPS (its config.h, not exported): DrawMesh binds this many units.
inline constexpr size_t kMaterialMaps = 12;
// Texture units of the shadow maps, past those DrawMesh binds; GL 3.3 has at least 16.
inline constexpr int kSunShadowUnit = kMaterialMaps;
inline constexpr int kFillShadowUnit = kSunShadowUnit + 1;
inline constexpr int kShadowAtlasUnit = kFillShadowUnit + 1;

// What narrows a point light to a spot.
struct SpotCone {
  // Unit.
  Eigen::Vector3f direction = Eigen::Vector3f::Zero();
  float cos_half_angle {};
};

// A point light as the shader takes it, in meters.
struct FrameLight {
  Eigen::Vector3f position = Eigen::Vector3f::Zero();
  float radius {};
  // Scaled by the intensity.
  Eigen::Vector3f color = Eigen::Vector3f::Zero();
  // Without a shadow, it lights only this box, its room (rooms/light_reach.h); with one, its
  // shadow stops it at the walls and lets it through openings in all of radius_box.
  Eigen::AlignedBox3f reach;
  Eigen::AlignedBox3f radius_box;
  std::optional<int> shadow_slot;
  std::optional<SpotCone> cone;
  // The entity carrying it, whose own shape doesn't shadow it: a player's flashlight.
  std::optional<uint64_t> owner;
};

struct DirectionalLight {
  // Unit, the way the light travels.
  Eigen::Vector3f direction = Eigen::Vector3f::Zero();
  Eigen::Vector3f color = Eigen::Vector3f::Zero();
  // Maps meters into the light's shadow map; without one it lights everything.
  std::optional<Eigen::Matrix4f> shadow_view_projection;
};

// Light that doesn't come from lamps: the sun, the fill from the other side, and an ambient
// term from above and below.
struct SceneLight {
  std::optional<DirectionalLight> sun;
  std::optional<DirectionalLight> fill;
  Eigen::Vector3f ambient_sky = Eigen::Vector3f::Zero();
  Eigen::Vector3f ambient_ground = Eigen::Vector3f::Zero();
};

// How the lamps' shadow atlas is tiled (shadow_maps.h).
struct ShadowAtlasLayout {
  int tile_size {};
  int columns {};
};

struct DirectionalUniforms {
  int enabled {};
  int direction {};
  int color {};
  int shadow_enabled {};
  int view_projection {};
};

// Locations of the light uniforms in assets/shaders/lighting.fs.
struct LightUniforms {
  int count {};
  int position_radius {};
  int color_shadow {};
  int reach_min {};
  int reach_max {};
  int direction_cone {};
  int shadow_tile_size {};
  int shadow_columns {};
  DirectionalUniforms sun;
  DirectionalUniforms fill;
  int ambient_sky {};
  int ambient_ground {};
};

// Loads assets/shaders/lighting.{vs,fs} with its shadow samplers on their units. Returns an
// empty Shader (id 0) on compile failure.
::Shader LoadLightingShader();
LightUniforms FindLightUniforms(const ::Shader& shader);

// At most kMaxLights of `lights`.
void UploadLights(const ::Shader& shader, const LightUniforms& uniforms, std::span<const FrameLight> lights,
                  const ShadowAtlasLayout& atlas);
void UploadSceneLight(const ::Shader& shader, const LightUniforms& uniforms, const SceneLight& light);

}  // namespace z13::raylib
