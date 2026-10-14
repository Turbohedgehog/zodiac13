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

#include <SDL3/SDL_video.h>

namespace z13::raylib {

// The GL context and GLSL dialect: desktop OpenGL 3.3 core, or OpenGL ES 3.0 on Android
// (docs/android-plan.md). Shader assets are written for 3.3; kShaderHeader replaces their
// #version line.
#if defined(__ANDROID__)
constexpr int kGlMajorVersion = 3;
constexpr int kGlMinorVersion = 0;
constexpr SDL_GLProfile kGlProfile = SDL_GL_CONTEXT_PROFILE_ES;
constexpr std::string_view kGlslVersion = "#version 300 es";
// ES has no default float precision in fragment shaders, nor for these sampler types.
constexpr std::string_view kShaderHeader =
    "#version 300 es\n"
    "precision highp float;\n"
    "precision highp int;\n"
    "precision highp sampler2D;\n"
    "precision highp samplerCube;\n";
#else
constexpr int kGlMajorVersion = 3;
constexpr int kGlMinorVersion = 3;
constexpr SDL_GLProfile kGlProfile = SDL_GL_CONTEXT_PROFILE_CORE;
constexpr std::string_view kGlslVersion = "#version 330";
constexpr std::string_view kShaderHeader = "#version 330\n";
#endif

}  // namespace z13::raylib
