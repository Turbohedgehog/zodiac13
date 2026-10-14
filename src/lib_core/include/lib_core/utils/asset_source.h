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
#include <string>
#include <vector>

namespace z13 {

// The game's shipped files, assets/ and config/, read by their path inside either
// ("station/palette.json") wherever they lie: a directory today, an archive with
// f/asset-pack (docs/asset-pack-plan.md). Readers never see where.
class AssetSource {
 public:
  AssetSource() = default;
  // `root` holds assets/ and config/.
  explicit AssetSource(std::filesystem::path root);

  std::expected<std::string, std::string> ReadAsset(const std::filesystem::path& relative) const;
  std::expected<std::string, std::string> ReadConfig(const std::filesystem::path& relative) const;
  // The names of the files directly in assets/`relative_dir`, sorted; none if it's missing.
  std::vector<std::string> ListAssets(const std::filesystem::path& relative_dir) const;

 private:
  std::filesystem::path root_;
};

// The world's AssetSource, set by Core::CreateWorld before any module loads a file.
struct Assets {
  using Singleton = void;
  AssetSource source;
};

}  // namespace z13
