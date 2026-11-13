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

#include <algorithm>
#include <cctype>
#include <format>
#include <string>

#include "platform/gl_profile.h"

namespace z13::raylib {

namespace {

std::string ForPlatform(const std::string& source, std::span<const std::string> defines) {
  const auto version_end = source.find('\n');
  if (version_end == std::string::npos) {
    return source;
  }
  std::string text(kShaderHeader);
  for (const std::string& name : defines) {
    text += std::format("#define {}\n", name);
  }
  return text + source.substr(version_end + 1);
}

bool IsIdentifier(const std::string& name) {
  return !name.empty() && !std::isdigit(static_cast<unsigned char>(name.front())) &&
         std::ranges::all_of(name, [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; });
}

}  // namespace

std::expected<ShaderSources, std::string> ReadShaderSources(const AssetSource& assets,
                                                            const std::filesystem::path& vertex,
                                                            const std::filesystem::path& fragment,
                                                            std::span<const std::string> defines,
                                                            const ShaderTextOverride& override_text) {
  if (const auto bad = std::ranges::find_if(defines, [](const std::string& name) { return !IsIdentifier(name); });
      bad != defines.end()) {
    return std::unexpected(std::format("'{}' is not a name a #define can take", *bad));
  }
  const auto vertex_source = override_text.vertex ? std::expected<std::string, std::string>(*override_text.vertex)
                                                  : assets.ReadAsset(vertex);
  const auto fragment_source = override_text.fragment ? std::expected<std::string, std::string>(*override_text.fragment)
                                                      : assets.ReadAsset(fragment);
  if (!vertex_source) {
    return std::unexpected(vertex_source.error());
  }
  if (!fragment_source) {
    return std::unexpected(fragment_source.error());
  }
  return ShaderSources {.vertex = ForPlatform(*vertex_source, defines), .fragment = ForPlatform(*fragment_source, defines)};
}

::Shader LoadShaderAsset(const AssetSource& assets, const std::filesystem::path& vertex,
                         const std::filesystem::path& fragment) {
  const auto sources = ReadShaderSources(assets, vertex, fragment);
  if (!sources) {
    return {};
  }
  return LoadShaderFromMemory(sources->vertex.c_str(), sources->fragment.c_str());
}

}  // namespace z13::raylib
