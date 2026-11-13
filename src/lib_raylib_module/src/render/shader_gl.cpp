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

#include "shader_gl.h"

#include <algorithm>
#include <format>
#include <string_view>
#include <vector>

#include "gl_volume_functions.h"

namespace z13::raylib {

namespace {

constexpr GLsizei kLogBytes = 4096;

std::string InfoLog(GLuint object, PFNGLGETSHADERIVPROC get_iv, PFNGLGETSHADERINFOLOGPROC get_log) {
  GLint length {};
  get_iv(object, GL_INFO_LOG_LENGTH, &length);
  if (length <= 1) {
    return {};
  }
  std::string text(static_cast<size_t>(std::min(length, kLogBytes)), '\0');
  GLsizei written {};
  get_log(object, static_cast<GLsizei>(text.size()), &written, text.data());
  text.resize(static_cast<size_t>(written));
  return text;
}

}  // namespace

std::string ShaderBuildLog(const std::string& vertex, const std::string& fragment) {
  const auto create_shader = GlLoad<PFNGLCREATESHADERPROC>("glCreateShader");
  const auto shader_source = GlLoad<PFNGLSHADERSOURCEPROC>("glShaderSource");
  const auto compile_shader = GlLoad<PFNGLCOMPILESHADERPROC>("glCompileShader");
  const auto get_shader_iv = GlLoad<PFNGLGETSHADERIVPROC>("glGetShaderiv");
  const auto get_shader_log = GlLoad<PFNGLGETSHADERINFOLOGPROC>("glGetShaderInfoLog");
  const auto delete_shader = GlLoad<PFNGLDELETESHADERPROC>("glDeleteShader");
  const auto create_program = GlLoad<PFNGLCREATEPROGRAMPROC>("glCreateProgram");
  const auto attach_shader = GlLoad<PFNGLATTACHSHADERPROC>("glAttachShader");
  const auto link_program = GlLoad<PFNGLLINKPROGRAMPROC>("glLinkProgram");
  const auto get_program_iv = GlLoad<PFNGLGETPROGRAMIVPROC>("glGetProgramiv");
  const auto get_program_log = GlLoad<PFNGLGETPROGRAMINFOLOGPROC>("glGetProgramInfoLog");
  const auto delete_program = GlLoad<PFNGLDELETEPROGRAMPROC>("glDeleteProgram");
  if (!create_shader || !shader_source || !compile_shader || !get_shader_iv || !get_shader_log || !delete_shader ||
      !create_program || !attach_shader || !link_program || !get_program_iv || !get_program_log || !delete_program) {
    return "the GL entry points for the build log are missing";
  }
  std::string log;
  std::vector<GLuint> stages;
  const auto compile = [&](GLenum kind, std::string_view name, const std::string& text) {
    const GLuint stage = create_shader(kind);
    const char* source = text.c_str();
    shader_source(stage, 1, &source, nullptr);
    compile_shader(stage);
    GLint compiled {};
    get_shader_iv(stage, GL_COMPILE_STATUS, &compiled);
    if (compiled == GL_FALSE) {
      log += std::format("{} shader: {}\n", name, InfoLog(stage, get_shader_iv, get_shader_log));
    }
    stages.push_back(stage);
  };
  compile(GL_VERTEX_SHADER, "vertex", vertex);
  compile(GL_FRAGMENT_SHADER, "fragment", fragment);
  if (log.empty()) {
    const GLuint program = create_program();
    for (const GLuint stage : stages) {
      attach_shader(program, stage);
    }
    link_program(program);
    GLint linked {};
    get_program_iv(program, GL_LINK_STATUS, &linked);
    if (linked == GL_FALSE) {
      log = std::format("link: {}\n", InfoLog(program, get_program_iv, get_program_log));
    }
    delete_program(program);
  }
  for (const GLuint stage : stages) {
    delete_shader(stage);
  }
  return log;
}

z13::Status CopyProgram(unsigned int target, unsigned int source) {
  const auto get_program_iv = GlLoad<PFNGLGETPROGRAMIVPROC>("glGetProgramiv");
  const auto get_binary = GlLoad<PFNGLGETPROGRAMBINARYPROC>("glGetProgramBinary");
  const auto program_binary = GlLoad<PFNGLPROGRAMBINARYPROC>("glProgramBinary");
  if (!get_program_iv || !get_binary || !program_binary) {
    return std::unexpected("this GL cannot copy a program binary");
  }
  GLint length {};
  get_program_iv(source, GL_PROGRAM_BINARY_LENGTH, &length);
  if (length <= 0) {
    return std::unexpected("the new program has no binary");
  }
  std::vector<char> binary(static_cast<size_t>(length));
  GLsizei written {};
  GLenum format {};
  get_binary(source, length, &written, &format, binary.data());
  program_binary(target, format, binary.data(), written);
  GLint linked {};
  get_program_iv(target, GL_LINK_STATUS, &linked);
  if (linked == GL_FALSE) {
    return std::unexpected("the driver refused the program binary");
  }
  return {};
}

}  // namespace z13::raylib
