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

#include "platform/gl_info.h"

#include <string>
#include <string_view>

#include <SDL3/SDL.h>

namespace z13::raylib {

namespace {

// From the GL headers, which this module doesn't include: SDL hands over the functions.
constexpr unsigned int kGlRenderer = 0x1F01;
constexpr unsigned int kGlVersion = 0x1F02;
constexpr unsigned int kGlNoError = 0;
// GL keeps one flag per error kind, so a few calls drain it; a lost context could repeat forever.
constexpr size_t kMaxErrors = 16;
#if defined(__ANDROID__)
constexpr std::string_view kFloatColorBufferExtension = "GL_EXT_color_buffer_float";
#endif

using GetStringFunction = const unsigned char* (*)(unsigned int);
using GetErrorFunction = unsigned int (*)();

std::string GlString(unsigned int name) {
  const auto get_string = reinterpret_cast<GetStringFunction>(SDL_GL_GetProcAddress("glGetString"));
  const unsigned char* text = get_string != nullptr ? get_string(name) : nullptr;
  return text != nullptr ? std::string(reinterpret_cast<const char*>(text)) : std::string();
}

}  // namespace

GlInfo QueryGlInfo() {
  return {
      .renderer = GlString(kGlRenderer),
      .version = GlString(kGlVersion),
#if defined(__ANDROID__)
      .float_color_buffer = SDL_GL_ExtensionSupported(kFloatColorBufferExtension.data()),
#else
      .float_color_buffer = true,
#endif
  };
}

std::vector<unsigned int> TakeGlErrors() {
  std::vector<unsigned int> errors;
  const auto get_error = reinterpret_cast<GetErrorFunction>(SDL_GL_GetProcAddress("glGetError"));
  if (get_error == nullptr) {
    return errors;
  }
  for (unsigned int error = get_error(); error != kGlNoError && errors.size() < kMaxErrors; error = get_error()) {
    errors.push_back(error);
  }
  return errors;
}

}  // namespace z13::raylib
