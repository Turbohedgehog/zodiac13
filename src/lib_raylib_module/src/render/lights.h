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

#include <lib_core/utils/asset_source.h>

namespace z13::raylib {

// Texels to a row of the light table (light_clusters.h).
inline constexpr int kLightTableWidth = 1024;
// raylib's MAX_MATERIAL_MAPS (its config.h, not exported).
inline constexpr size_t kMaterialMaps = 12;
// The lighting shader's other textures. DrawMesh binds unit i only for a material map i with a
// texture: lit materials carry just the diffuse map (unit 0) and the skybox its cubemap
// (MATERIAL_MAP_CUBEMAP), so these take the units of the maps no material sets. Past the 12
// maps they needed 18 units, more than the 16 of OpenGL ES 3.0 phones.
inline constexpr int kSunShadowUnit = MATERIAL_MAP_METALNESS;
inline constexpr int kFillShadowUnit = kSunShadowUnit + 1;
inline constexpr int kShadowAtlasUnit = kFillShadowUnit + 1;
inline constexpr int kLightTableUnit = kShadowAtlasUnit + 1;
// The baked lamps (cell_lighting.h).
inline constexpr int kCellLabelsUnit = kLightTableUnit + 1;
inline constexpr int kCellLightUnit = kCellLabelsUnit + 1;
static_assert(kCellLightUnit < MATERIAL_MAP_CUBEMAP, "the skybox's draw unbinds the cubemap's unit");
// Lamps players cast shadows from at once; assets/shaders/lighting.fs sizes its arrays by this.
inline constexpr size_t kMaxPlayerShadowLamps = 32;

// What narrows a point light to a spot.
struct SpotCone {
  // Unit.
  Eigen::Vector3f direction = Eigen::Vector3f::Zero();
  float cos_half_angle {};
};

// A flashlight lights what it reaches; a lamp's light is baked, and its shadow only takes it
// back where a player stands in it.
enum class LightUse : uint8_t { kLights, kPlayerShadows };

// A point light as the shader takes it, in meters.
struct FrameLight {
  Eigen::Vector3f position = Eigen::Vector3f::Zero();
  float radius {};
  // Scaled by the intensity.
  Eigen::Vector3f color = Eigen::Vector3f::Zero();
  // What it may light; with a shadow, the shadow stops it at the walls within it.
  Eigen::AlignedBox3f reach;
  Eigen::AlignedBox3f radius_box;
  LightUse use {};
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
  int cluster_headers {};
  int cluster_indices {};
  int cluster_counts {};
  int cluster_tile {};
  int cluster_depth {};
  int view_forward {};
  int shadow_tile_size {};
  int shadow_columns {};
  DirectionalUniforms sun;
  DirectionalUniforms fill;
  int ambient_sky {};
  int ambient_ground {};
  int cell_light_enabled {};
  int cell_origin {};
  int cell_cubes {};
  int player_shadow_count {};
  int player_shadow_lamps {};
  int player_shadow_colors {};
};

// Loads assets/shaders/lighting.{vs,fs} with its samplers on their units. Returns an empty
// Shader (id 0) on compile failure.
::Shader LoadLightingShader(const AssetSource& assets);
LightUniforms FindLightUniforms(const ::Shader& shader);

void UploadShadowAtlas(const ::Shader& shader, const LightUniforms& uniforms, const ShadowAtlasLayout& atlas);
void UploadSceneLight(const ::Shader& shader, const LightUniforms& uniforms, const SceneLight& light);
// The first kMaxPlayerShadowLamps of the lamps among `lights` that have a shadow slot.
void UploadPlayerShadows(const ::Shader& shader, const LightUniforms& uniforms, std::span<const FrameLight> lights);

}  // namespace z13::raylib
