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

#include <string>
#include <vector>

namespace z13::raylib {

// What the GL context reports, shown on the pause menu: a phone has no log to read.
struct GlInfo {
  std::string renderer;
  std::string version;
  // Whether float textures can be drawn into (the lamps' shadow atlas): core on desktop GL,
  // EXT_color_buffer_float on OpenGL ES 3.0.
  bool float_color_buffer {};
};

// Needs a current context.
GlInfo QueryGlInfo();

// The pending GL error codes, cleared; GL_NO_ERROR (0) never.
std::vector<unsigned int> TakeGlErrors();

}  // namespace z13::raylib
