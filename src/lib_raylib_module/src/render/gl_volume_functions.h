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

// Only for the .cpp files calling OpenGL directly: SDL's GL headers bring in windows.h, whose
// names clash with raylib's.
#include <SDL3/SDL_opengl.h>
#include <SDL3/SDL_video.h>

namespace z13::raylib {

// An entry point of the current context.
template <typename Function>
Function GlLoad(const char* name) {
  return reinterpret_cast<Function>(SDL_GL_GetProcAddress(name));
}

// The OpenGL entry points 3D textures need, from SDL_GL_GetProcAddress.
struct GlVolumeFunctions {
  PFNGLTEXIMAGE3DPROC tex_image_3d {};
  PFNGLTEXSUBIMAGE3DPROC tex_sub_image_3d {};
  PFNGLACTIVETEXTUREPROC active_texture {};
  void(APIENTRY* gen_textures)(GLsizei, GLuint*) {};
  void(APIENTRY* delete_textures)(GLsizei, const GLuint*) {};
  void(APIENTRY* bind_texture)(GLenum, GLuint) {};
  void(APIENTRY* tex_parameter_i)(GLenum, GLenum, GLint) {};
  void(APIENTRY* pixel_store_i)(GLenum, GLint) {};
  void(APIENTRY* get_integer_v)(GLenum, GLint*) {};
};

}  // namespace z13::raylib
