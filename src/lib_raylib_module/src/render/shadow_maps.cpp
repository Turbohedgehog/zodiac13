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

#include "shadow_maps.h"

#include <filesystem>
#include <string_view>

#include "../tools/asset_path.h"
#include "render_resources.h"
#include "shadow_maps_state.h"

namespace z13::raylib {

namespace {

const std::filesystem::path kShadowVertexShaderPath = "shaders/shadow.vs";
const std::filesystem::path kShadowFragmentShaderPath = "shaders/shadow.fs";
// Every shader raylib links has it; a failed link leaves none.
constexpr std::string_view kMvpUniform = "mvp";

}  // namespace

::Shader LoadShadowShader() {
  ::Shader shader =
      LoadShader(AssetPath(kShadowVertexShaderPath).c_str(), AssetPath(kShadowFragmentShaderPath).c_str());
  if (GetShaderLocation(shader, kMvpUniform.data()) < 0) {
    SafeUnloadShader(shader);
    return {};
  }
  return shader;
}

ShadowMaps::ShadowMaps(::Shader shadow_shader) {
  if (shadow_shader.id != 0) {
    state_ = std::make_shared<State>(shadow_shader);
  }
}

Status ShadowMaps::UpdateLamps(
    std::span<FrameLight> lights, const ShadowCasters& casters, const z13::RenderTuning& tuning) {
  return state_ ? state_->UpdateLamps(lights, casters, tuning) : Status {};
}

ShadowAtlasLayout ShadowMaps::AtlasLayout() const {
  return state_ ? state_->AtlasLayout() : ShadowAtlasLayout {};
}

std::optional<Eigen::Matrix4f> ShadowMaps::UpdateDirectional(DirectionalMap map, const Eigen::Vector3f& direction,
                                                             const ShadowCasters& casters,
                                                             const z13::RenderTuning& tuning) {
  return state_ ? state_->UpdateDirectional(map, direction, casters, tuning) : std::nullopt;
}

void ShadowMaps::Bind() const {
  if (state_) {
    state_->Bind();
  }
}

}  // namespace z13::raylib
