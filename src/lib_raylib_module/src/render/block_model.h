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

#include <memory>

#include <raylib.h>

#include <z13_primitives/geometry.h>

namespace z13::raylib {

// Uploads `mesh` (in cells) as a model in meters; `lighting_shader` is borrowed by its
// material, and id 0 keeps raylib's default.
std::shared_ptr<::Model> LoadBlockModel(const z13::building::primitives::Mesh& mesh, ::Shader lighting_shader);

}  // namespace z13::raylib
