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

#include "station_graph/graph_assets.h"

#include <format>

namespace z13::editor::graph {

std::filesystem::path PropertiesFile() {
  return std::filesystem::path(kStationDir) / kGraphDir / kPropertiesFileName;
}

std::filesystem::path GraphFile(std::string_view name) {
  return std::filesystem::path(kStationDir) / kGraphDir / std::format("{}{}", name, kGraphExtension);
}

}  // namespace z13::editor::graph
