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
#include <string>
#include <string_view>
#include <unordered_map>

#include <raylib.h>

#include <lib_core/utils/asset_source.h>

namespace z13::raylib {

// The tiles blocks are textured with (assets/textures/station/), loaded on first use and kept.
class BlockTextures {
 public:
  explicit BlockTextures(AssetSource assets = {}) : assets_(std::move(assets)) {}

  // The tile `name` names; none when its file is missing or isn't an image.
  std::shared_ptr<::Texture2D> Find(std::string_view name);

 private:
  AssetSource assets_;
  // A tile that failed to load stays empty, so it is not retried every frame.
  std::unordered_map<std::string, std::shared_ptr<::Texture2D>> tiles_;
};

}  // namespace z13::raylib
