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

#include <string_view>

#include <z13/components/station.h>

#include "../tools/math_convert.h"
#include "render_components.h"

namespace z13::raylib {

namespace {

constexpr std::string_view kEnabledUniform = "checkerEnabled";
constexpr std::string_view kEmissiveUniform = "checkerEmissive";
constexpr std::string_view kFirstUniform = "checkerFirst";
constexpr std::string_view kSecondUniform = "checkerSecond";
constexpr std::string_view kCellSizeUniform = "cellSize";

void SetFlag(const LightingResources& res, int location, bool on) {
  const int value = on ? 1 : 0;
  SetShaderValue(*res.shader, location, &value, SHADER_UNIFORM_INT);
}

}  // namespace

CheckerUniforms SetupCheckerUniforms(const ::Shader& shader) {
  const float cell_size = z13::station::kCellSize;
  SetShaderValue(shader, GetShaderLocation(shader, kCellSizeUniform.data()), &cell_size, SHADER_UNIFORM_FLOAT);
  return {
      .enabled = GetShaderLocation(shader, kEnabledUniform.data()),
      .emissive = GetShaderLocation(shader, kEmissiveUniform.data()),
      .first = GetShaderLocation(shader, kFirstUniform.data()),
      .second = GetShaderLocation(shader, kSecondUniform.data()),
  };
}

void UseChecker(const Lighting& lighting, const z13::building::primitives::Checker& material) {
  if (!lighting.res) {
    return;
  }
  const LightingResources& res = *lighting.res;
  SetFlag(res, res.checker.enabled, true);
  SetFlag(res, res.checker.emissive, material.emissive);
  SetShaderValue(*res.shader, res.checker.first, ToUnitColor(material.first).data(), SHADER_UNIFORM_VEC4);
  SetShaderValue(*res.shader, res.checker.second, ToUnitColor(material.second).data(), SHADER_UNIFORM_VEC4);
}

void StopChecker(const Lighting& lighting) {
  if (lighting.res) {
    SetFlag(*lighting.res, lighting.res->checker.enabled, false);
    SetFlag(*lighting.res, lighting.res->checker.emissive, false);
  }
}

}  // namespace z13::raylib
