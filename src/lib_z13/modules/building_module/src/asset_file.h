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

#include <filesystem>

namespace z13::building {

// `relative` under assets/, which sits two levels above this plugin
// (bin/modules/building_module/) wherever it runs: the game, a dedicated server or the
// test runner.
std::filesystem::path AssetFile(const std::filesystem::path& relative);

}  // namespace z13::building
