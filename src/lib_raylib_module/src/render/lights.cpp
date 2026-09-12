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

#include <array>
#include <cstdio>
#include <string>

#include <spdlog/spdlog.h>

#include "../tools/asset_path.h"
#include "render_components.h"

namespace z13::raylib {

namespace {

constexpr std::array<float, 4> kAmbient{0.25f, 0.25f, 0.30f, 1.0f};

std::string LightUniform(int index, const char* field) {
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "lights[%d].%s", index, field);
  return buffer;
}

}  // namespace

::Shader LoadLightingShader() {
  ::Shader shader = LoadShader(AssetPath("shaders/lighting.vs").c_str(),
                               AssetPath("shaders/lighting.fs").c_str());
  if (GetShaderLocation(shader, "viewPos") < 0) {
    spdlog::error("[raylib] lighting shader failed to compile");
    SafeUnloadShader(shader);
    return ::Shader{};
  }

  shader.locs[SHADER_LOC_VECTOR_VIEW] = GetShaderLocation(shader, "viewPos");

  const int ambient_loc = GetShaderLocation(shader, "ambient");
  SetShaderValue(shader, ambient_loc, kAmbient.data(), SHADER_UNIFORM_VEC4);
  return shader;
}

void SetupLight(Light& light, const ::Shader& shader, int index) {
  light.enabled_loc = GetShaderLocation(shader, LightUniform(index, "enabled").c_str());
  light.type_loc = GetShaderLocation(shader, LightUniform(index, "type").c_str());
  light.position_loc = GetShaderLocation(shader, LightUniform(index, "position").c_str());
  light.target_loc = GetShaderLocation(shader, LightUniform(index, "target").c_str());
  light.color_loc = GetShaderLocation(shader, LightUniform(index, "color").c_str());
  UpdateLight(shader, light);
}

void UpdateLight(const ::Shader& shader, const Light& light) {
  const int enabled = light.enabled ? 1 : 0;
  const int type = static_cast<int>(light.kind);
  SetShaderValue(shader, light.enabled_loc, &enabled, SHADER_UNIFORM_INT);
  SetShaderValue(shader, light.type_loc, &type, SHADER_UNIFORM_INT);

  const std::array<float, 3> position{light.position.x, light.position.y, light.position.z};
  const std::array<float, 3> target{light.target.x, light.target.y, light.target.z};
  SetShaderValue(shader, light.position_loc, position.data(), SHADER_UNIFORM_VEC3);
  SetShaderValue(shader, light.target_loc, target.data(), SHADER_UNIFORM_VEC3);

  const std::array<float, 4> color{light.color.r / 255.f, light.color.g / 255.f,
                                   light.color.b / 255.f, light.color.a / 255.f};
  SetShaderValue(shader, light.color_loc, color.data(), SHADER_UNIFORM_VEC4);
}

}  // namespace z13::raylib
