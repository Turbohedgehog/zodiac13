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

#include <z13_module/tools/z13_environment.h>

#include <z13/constants/constants.h>

#include <sago/platform_folders.h>

namespace z13::tools::environment {

std::filesystem::path GetGameDataDirectory() {
  return std::filesystem::path(sago::getDataHome()) / z13::constants::kGameName;
}

std::filesystem::path GetGameInputConfigJsonPath() {
  return GetGameDataDirectory() / "input_config.json";
}

std::filesystem::path GetGameInputConfigJsonPath2() {
  return GetGameDataDirectory() / "input_config_2.json";
}

}  // namespace z13::tools::environment
