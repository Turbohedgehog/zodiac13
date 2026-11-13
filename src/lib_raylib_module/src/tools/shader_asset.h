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

#include <expected>
#include <filesystem>
#include <optional>
#include <span>
#include <string>

#include <raylib.h>

#include <lib_core/utils/asset_source.h>

namespace z13::raylib {

// A pair's sources for this platform's GLSL dialect: the #version line is replaced by kShaderHeader.
struct ShaderSources {
  std::string vertex;
  std::string fragment;
};

// Text standing in for a stage's file: a debug edit that is not on disk.
struct ShaderTextOverride {
  std::optional<std::string> vertex;
  std::optional<std::string> fragment;
};

// The pair's files (or the overrides), each with a #define line per name of `defines` after the header; a
// name must be an identifier.
std::expected<ShaderSources, std::string> ReadShaderSources(const AssetSource& assets,
                                                            const std::filesystem::path& vertex,
                                                            const std::filesystem::path& fragment,
                                                            std::span<const std::string> defines = {},
                                                            const ShaderTextOverride& override_text = {});

// Loads a shader pair from assets/ for this platform's GLSL dialect (kShaderHeader).
// A missing file or a failed compile gives a shader without the pair's uniforms.
::Shader LoadShaderAsset(const AssetSource& assets, const std::filesystem::path& vertex,
                         const std::filesystem::path& fragment);

}  // namespace z13::raylib
