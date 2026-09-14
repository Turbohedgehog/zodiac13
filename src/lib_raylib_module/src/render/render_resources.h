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

namespace z13::raylib {

// Skips raylib's shared default shader/texture (unloading it would break every
// other user), and skips unloading once the GL context is already gone.
void SafeUnloadShader(::Shader& shader);
void SafeUnloadTexture(::Texture2D& texture);

// shared_ptr wrapper whose deleter calls the matching SafeUnload*.
std::shared_ptr<::Shader> MakeManagedShader(::Shader shader);
std::shared_ptr<::Texture2D> MakeManagedTexture(::Texture2D texture);

// shared_ptr wrapper whose deleter frees the model, skipping the shader on any
// material still pointing at borrowed_shader_id (owned/freed elsewhere, e.g. Lighting).
std::shared_ptr<::Model> MakeManagedModel(::Model model, unsigned int borrowed_shader_id = 0);

}  // namespace z13::raylib
