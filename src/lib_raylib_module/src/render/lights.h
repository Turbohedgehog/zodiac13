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

#include <raylib.h>

namespace z13::raylib {

// Minimal port of raylib's example `rlights` helper: up to 4 Blinn-Phong lights
// driven by assets/shaders/lighting.{vs,fs}.
inline constexpr int kMaxLights = 4;

enum class LightKind : int { Directional = 0, Point = 1 };

struct Light {
  LightKind kind = LightKind::Directional;
  bool enabled = true;
  ::Vector3 position{};
  ::Vector3 target{};
  ::Color color = WHITE;

  int enabled_loc = -1;
  int type_loc = -1;
  int position_loc = -1;
  int target_loc = -1;
  int color_loc = -1;
};

// Loads assets/shaders/lighting.{vs,fs} and wires the standard raylib material
// locs (MVP / model / normal / map-diffuse / colDiffuse) plus `viewPos` and a
// low `ambient` term. Returns an empty Shader (id 0) on compile failure.
::Shader LoadLightingShader();

// Binds `light` to slot `index` of `shader` (fills its uniform locs) and uploads
// its current values.
void SetupLight(Light& light, const ::Shader& shader, int index);

// Re-uploads a light's mutable values (call after changing position/color/etc.).
void UpdateLight(const ::Shader& shader, const Light& light);

}  // namespace z13::raylib
