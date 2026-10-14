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

#include "shader_asset.h"

#include <string>

#include "platform/gl_profile.h"

namespace z13::raylib {

namespace {

// The asset's text with its #version line replaced by kShaderHeader.
std::string ForPlatform(const std::string& source) {
  const auto version_end = source.find('\n');
  if (version_end == std::string::npos) {
    return source;
  }
  return std::string(kShaderHeader) + source.substr(version_end + 1);
}

}  // namespace

::Shader LoadShaderAsset(const AssetSource& assets, const std::filesystem::path& vertex,
                         const std::filesystem::path& fragment) {
  const auto vertex_source = assets.ReadAsset(vertex);
  const auto fragment_source = assets.ReadAsset(fragment);
  if (!vertex_source || !fragment_source) {
    return {};
  }
  return LoadShaderFromMemory(ForPlatform(*vertex_source).c_str(), ForPlatform(*fragment_source).c_str());
}

}  // namespace z13::raylib
