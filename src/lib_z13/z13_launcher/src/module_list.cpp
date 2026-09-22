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

#include <z13_launcher/module_list.h>

#include <format>
#include <fstream>
#include <sstream>

#include <yaml-cpp/yaml.h>

#include <lib_core/log.h>

namespace z13 {

std::expected<std::vector<std::string>, std::string> ParseModuleList(const std::string& yaml_text) {
  std::vector<std::string> modules;

  try {
    const auto root = YAML::Load(yaml_text);
    const auto modules_node = root["modules"];
    if (!modules_node || !modules_node.IsSequence()) {
      return std::unexpected("ParseModuleList: no 'modules' sequence");
    }

    for (const auto& entry : modules_node) {
      if (entry.IsScalar()) {
        modules.push_back(entry.as<std::string>());
      } else if (entry.IsMap() && entry["path"]) {
        modules.push_back(entry["path"].as<std::string>());
      } else {
        log_warn("ParseModuleList: skipping malformed module entry");
      }
    }
  } catch (const YAML::Exception& ex) {
    return std::unexpected(std::format("ParseModuleList: failed to parse module list: {}", ex.what()));
  }

  return modules;
}

std::expected<std::vector<std::string>, std::string> ReadModuleList(const std::filesystem::path& config_path) {
  if (!std::filesystem::exists(config_path)) {
    return std::unexpected(std::format("ReadModuleList: config file '{}' does not exist", config_path.string()));
  }

  std::ifstream file(config_path);
  std::ostringstream contents;
  contents << file.rdbuf();
  return ParseModuleList(contents.str());
}

}  // namespace z13
