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

namespace z13::raylib {

// Per-entity camera, kept in sync with the entity's Eigen::Matrix4f transform.
struct RaylibCamera {
  ::Camera3D camera{};
};

// GPU resources for the skybox. shader/cubemap/model are independently owned
// (render_resources.h); the model's deleter clears its own references to them
// before UnloadModel, so destruction order between the three doesn't matter.
struct SkyboxResources {
  std::shared_ptr<::Shader> shader;
  std::shared_ptr<::Texture2D> cubemap;
  std::shared_ptr<::Model> model;
};

// Skybox singleton. Empty `res` means the skybox failed to load.
struct Skybox {
  std::shared_ptr<SkyboxResources> res;
};

// GPU resources for a loaded model (see MakeManagedModel, render_resources.h).
// The world TRS is baked into model->transform at load.
struct ModelResources {
  std::shared_ptr<::Model> model;
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

// GPU resources for scene lighting. Singleton (see MakeManagedShader, render_resources.h).
struct LightingResources {
  std::shared_ptr<::Shader> shader;
};

struct Lighting {
  std::shared_ptr<LightingResources> res;
};

}  // namespace z13::raylib
