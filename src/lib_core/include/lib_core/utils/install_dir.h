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

namespace z13 {

// Where config/ and assets/ live: next to the executable; on Android, the working
// directory the app unpacks them into (src/zodiac13/android_main.cpp).
std::filesystem::path InstallDir();

// The shared library a module list entry ("modules/x/x") names: next to the executable,
// with the platform's extension; on Android, lib<x>.so in the app's library directory.
std::filesystem::path ModuleLibraryPath(const std::filesystem::path& entry);

}  // namespace z13
