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

#include "asset_path.h"

#include <filesystem>

#include <boost/dll/runtime_symbol_info.hpp>

#ifndef Z13_ASSETS_DIR
#define Z13_ASSETS_DIR "assets"
#endif

namespace z13::raylib {

std::string AssetPath(std::string_view relative) {
  const std::filesystem::path exe_dir = boost::dll::program_location().parent_path().string();
  return (exe_dir / Z13_ASSETS_DIR / relative).string();
}

}  // namespace z13::raylib
