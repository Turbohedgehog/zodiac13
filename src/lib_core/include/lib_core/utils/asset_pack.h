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

#include <cstdint>
#include <expected>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <lib_core/utils/pack_reader.h>

namespace z13 {

// Where the build puts the archive, next to the executable; the tools and CMake name it too.
inline constexpr std::string_view kAssetPackFile = "z13.pack";

struct PackEntry {
  std::uint64_t offset {};
  std::uint64_t size {};
};

// The archive tools/pack_assets.py writes: every file of config/ and assets/ by its path
// from the root ("assets/station/palette.json"). Only the index is kept in memory.
class AssetPack {
 public:
  static std::expected<AssetPack, std::string> Open(std::unique_ptr<PackReader> reader);

  std::expected<std::string, std::string> Read(const std::string& path) const;
  // The names of the files directly in `directory`, sorted.
  std::vector<std::string> List(const std::string& directory) const;
  std::size_t Count() const { return index_.size(); }

 private:
  AssetPack(std::unique_ptr<PackReader> reader, std::map<std::string, PackEntry> index);

  std::unique_ptr<PackReader> reader_;
  std::map<std::string, PackEntry> index_;
};

}  // namespace z13
