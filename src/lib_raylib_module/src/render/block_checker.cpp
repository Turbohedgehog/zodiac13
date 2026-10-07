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

#include "block_checker.h"

#include <array>
#include <string_view>

#include <z13/components/station.h>

#include "render_components.h"

namespace z13::raylib {

namespace {

constexpr std::string_view kEnabledUniform = "checkerEnabled";
constexpr std::string_view kFirstUniform = "checkerFirst";
constexpr std::string_view kSecondUniform = "checkerSecond";
constexpr std::string_view kCellSizeUniform = "cellSize";
constexpr float kMaxChannel = 255.f;

std::array<float, 4> ToVec4(const z13::building::primitives::Rgba& rgba) {
  return {rgba[0] / kMaxChannel, rgba[1] / kMaxChannel, rgba[2] / kMaxChannel, rgba[3] / kMaxChannel};
}

void SetEnabled(const LightingResources& res, bool enabled) {
  const int value = enabled ? 1 : 0;
  SetShaderValue(*res.shader, res.checker.enabled, &value, SHADER_UNIFORM_INT);
}

}  // namespace

CheckerUniforms SetupCheckerUniforms(const ::Shader& shader) {
  const float cell_size = z13::station::kCellSize;
  SetShaderValue(shader, GetShaderLocation(shader, kCellSizeUniform.data()), &cell_size, SHADER_UNIFORM_FLOAT);
  return {
      .enabled = GetShaderLocation(shader, kEnabledUniform.data()),
      .first = GetShaderLocation(shader, kFirstUniform.data()),
      .second = GetShaderLocation(shader, kSecondUniform.data()),
  };
}

void UseChecker(const Lighting& lighting, const z13::building::primitives::Checker& material) {
  if (!lighting.res) {
    return;
  }
  const LightingResources& res = *lighting.res;
  SetEnabled(res, true);
  SetShaderValue(*res.shader, res.checker.first, ToVec4(material.first).data(), SHADER_UNIFORM_VEC4);
  SetShaderValue(*res.shader, res.checker.second, ToVec4(material.second).data(), SHADER_UNIFORM_VEC4);
}

void StopChecker(const Lighting& lighting) {
  if (lighting.res) {
    SetEnabled(*lighting.res, false);
  }
}

}  // namespace z13::raylib
