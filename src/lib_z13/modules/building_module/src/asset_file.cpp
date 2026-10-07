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

#include "asset_file.h"

#include <boost/dll/runtime_symbol_info.hpp>

namespace z13::building {

namespace {

const std::filesystem::path kAssetsDir = "assets";

}  // namespace

std::filesystem::path AssetFile(const std::filesystem::path& relative) {
  const std::filesystem::path plugin_dir = boost::dll::this_line_location().parent_path().string();
  return plugin_dir.parent_path().parent_path() / kAssetsDir / relative;
}

}  // namespace z13::building
