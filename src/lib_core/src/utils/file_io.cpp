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

#include <lib_core/utils/file_io.h>

#include <format>
#include <fstream>
#include <iterator>
#include <system_error>

namespace z13 {

std::expected<std::string, std::string> ReadFile(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file.is_open()) {
    return std::unexpected(std::format("cannot open '{}'", path.string()));
  }
  return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

Status WriteFile(const std::filesystem::path& path, std::string_view contents) {
  const auto directory = path.parent_path();
  if (std::error_code error; !directory.empty() && !std::filesystem::create_directories(directory, error) && error) {
    return std::unexpected(std::format("cannot create '{}': {}", directory.string(), error.message()));
  }
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  if (!file.is_open() || !file.write(contents.data(), static_cast<std::streamsize>(contents.size()))) {
    return std::unexpected(std::format("cannot write '{}'", path.string()));
  }
  return {};
}

}  // namespace z13
