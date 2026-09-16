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

#include <string_view>

#include <raylib.h>

namespace z13::raylib {

// Loads an assimp-supported model from <assets>/<relative_path>, with node
// transforms baked into vertices. Empty Model (meshCount == 0) on failure.
::Model LoadModelFromAsset(std::string_view relative_path);

}  // namespace z13::raylib
