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

#include <lib_core/utils/asset_source.h>

#include <algorithm>
#include <system_error>
#include <utility>

#include <lib_core/utils/file_io.h>

namespace z13 {

namespace {

const std::filesystem::path kAssetsDir = "assets";
const std::filesystem::path kConfigDir = "config";

}  // namespace

AssetSource::AssetSource(std::filesystem::path root) : root_(std::move(root)) {}

std::expected<std::string, std::string> AssetSource::ReadAsset(const std::filesystem::path& relative) const {
  return ReadFile(root_ / kAssetsDir / relative);
}

std::expected<std::string, std::string> AssetSource::ReadConfig(const std::filesystem::path& relative) const {
  return ReadFile(root_ / kConfigDir / relative);
}

std::vector<std::string> AssetSource::ListAssets(const std::filesystem::path& relative_dir) const {
  std::vector<std::string> names;
  std::error_code error;
  for (const auto& entry : std::filesystem::directory_iterator(root_ / kAssetsDir / relative_dir, error)) {
    if (entry.is_regular_file(error)) {
      names.push_back(entry.path().filename().string());
    }
  }
  std::ranges::sort(names);
  return names;
}

}  // namespace z13
