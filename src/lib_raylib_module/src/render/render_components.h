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

#include <memory>

#include <raylib.h>
#include <rlgl.h>

namespace z13::raylib {

// raylib shares a single default shader/texture across untextured materials and
// failed shader compiles; unloading either would break every other user of it.
inline void SafeUnloadShader(::Shader& shader) {
  if (shader.id != 0 && shader.id != rlGetShaderIdDefault()) UnloadShader(shader);
  shader = {};
}

inline void SafeUnloadTexture(::Texture2D& texture) {
  if (texture.id != 0 && texture.id != rlGetTextureIdDefault()) UnloadTexture(texture);
  texture = {};
}

// Per-entity camera, kept in sync with the entity's Eigen::Matrix4f transform.
struct RaylibCamera {
  ::Camera3D camera{};
};

// GPU resources for the skybox. The shader and cubemap are referenced by
// model.materials[0]; the destructor unloads them in the right order (GL context
// must still be alive).
struct SkyboxResources {
  ::Model model{};
  ::Texture2D cubemap{};
  ::Shader shader{};

  SkyboxResources() = default;
  SkyboxResources(const SkyboxResources&) = delete;
  SkyboxResources& operator=(const SkyboxResources&) = delete;

  ~SkyboxResources() {
    SafeUnloadShader(shader);
    SafeUnloadTexture(cubemap);
    if (model.materialCount > 0) {
      model.materials[0].shader.id = 0;
      model.materials[0].maps[MATERIAL_MAP_CUBEMAP].texture.id = 0;
    }
    UnloadModel(model);
  }
};

// Skybox singleton. Empty `res` means the skybox failed to load.
struct Skybox {
  std::shared_ptr<SkyboxResources> res;
};

// GPU resources for a loaded model. UnloadModel frees the meshes, the materials
// and their textures; the world TRS is baked into model.transform at load.
struct ModelResources {
  ::Model model{};
  // A shader shared with the scene (e.g. lighting); UnloadModel must not free it.
  unsigned int borrowed_shader_id = 0;

  ModelResources() = default;
  ModelResources(const ModelResources&) = delete;
  ModelResources& operator=(const ModelResources&) = delete;

  ~ModelResources() {
    if (model.meshCount == 0) return;
    for (int i = 0; i < model.materialCount; ++i) {
      if (model.materials[i].shader.id == borrowed_shader_id) {
        model.materials[i].shader.id = 0;
      }
      // UnloadModel doesn't free material textures; the model owns any texture
      // it actually loaded (LoadMaterialDefault leaves untextured maps pointing
      // at raylib's shared default texture, which must not be freed here).
      for (int map = 0; map <= MATERIAL_MAP_BRDF; ++map) {
        if (model.materials[i].maps[map].texture.id != 0) {
          SafeUnloadTexture(model.materials[i].maps[map].texture);
        }
      }
    }
    UnloadModel(model);
  }
};

// A drawable model. Empty `res` means the model failed to load. Singleton for
// now (the demo spaceship); becomes per-entity when scene content grows.
struct RenderModel {
  std::shared_ptr<ModelResources> res;
};

// Per-entity cube for a z13::building::Brush preview. Follows the entity's
// Eigen::Matrix4f transform.
struct BuildingBlock {
  std::shared_ptr<ModelResources> res;
};

// GPU resources for scene lighting. Singleton.
struct LightingResources {
  ::Shader shader{};

  LightingResources() = default;
  LightingResources(const LightingResources&) = delete;
  LightingResources& operator=(const LightingResources&) = delete;

  ~LightingResources() { SafeUnloadShader(shader); }
};

struct Lighting {
  std::shared_ptr<LightingResources> res;
};

}  // namespace z13::raylib
