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

#include "block_textures.h"

#include <primitives/station_assets.h>

#include <lib_core/utils/log.h>

#include "render_resources.h"

namespace z13::raylib {

std::shared_ptr<::Texture2D> BlockTextures::Find(std::string_view name) {
  const std::string key(name);
  if (const auto found = tiles_.find(key); found != tiles_.end()) {
    return found->second;
  }
  std::shared_ptr<::Texture2D> tile;
  const auto bytes = assets_.ReadAsset(z13::building::primitives::TextureFile(name));
  if (!bytes) {
    log_error("[raylib] block texture '{}': {}", name, bytes.error());
  } else {
    ::Image image = LoadImageFromMemory(
        z13::building::primitives::kTextureExtension.data(), reinterpret_cast<const unsigned char*>(bytes->data()),
        static_cast<int>(bytes->size()));
    if (image.data == nullptr) {
      log_error("[raylib] block texture '{}' is not an image", name);
    } else {
      ::Texture2D texture = LoadTextureFromImage(image);
      UnloadImage(image);
      GenTextureMipmaps(&texture);
      SetTextureFilter(texture, TEXTURE_FILTER_TRILINEAR);
      SetTextureWrap(texture, TEXTURE_WRAP_REPEAT);
      tile = MakeManagedTexture(texture);
    }
  }
  tiles_.emplace(key, tile);
  return tile;
}

}  // namespace z13::raylib
