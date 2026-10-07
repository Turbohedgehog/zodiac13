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
#include <string_view>

#include <lib_core/utils/status.h>

namespace z13 {

// The whole file, byte for byte.
std::expected<std::string, std::string> ReadFile(const std::filesystem::path& path);

// Replaces the file with `contents`, creating its parent directories.
Status WriteFile(const std::filesystem::path& path, std::string_view contents);

}  // namespace z13
