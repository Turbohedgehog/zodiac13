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

#include "gl_volumes.h"

#include "gl_volume_functions.h"

namespace z13::raylib {

namespace {

using z13::building::primitives::CellBox;

struct TexelFormat {
  GLint internal {};
  GLenum format {};
  GLenum type {};
};

TexelFormat FormatOf(VolumeFormat format) {
  return format == VolumeFormat::kLabels ? TexelFormat {GL_R8UI, GL_RED_INTEGER, GL_UNSIGNED_BYTE}
                                         : TexelFormat {GL_RGBA16F, GL_RGBA, GL_FLOAT};
}

// Uploads read a sub-box out of a larger array; afterwards the defaults rlgl expects come back.
void SetUnpack(const GlVolumeFunctions& gl, const Eigen::Vector3i& source_size, const Eigen::Vector3i& skip) {
  gl.pixel_store_i(GL_UNPACK_ALIGNMENT, 1);
  gl.pixel_store_i(GL_UNPACK_ROW_LENGTH, source_size.x());
  gl.pixel_store_i(GL_UNPACK_IMAGE_HEIGHT, source_size.y());
  gl.pixel_store_i(GL_UNPACK_SKIP_PIXELS, skip.x());
  gl.pixel_store_i(GL_UNPACK_SKIP_ROWS, skip.y());
  gl.pixel_store_i(GL_UNPACK_SKIP_IMAGES, skip.z());
}

void ResetUnpack(const GlVolumeFunctions& gl) {
  constexpr GLint kDefaultAlignment = 4;
  SetUnpack(gl, Eigen::Vector3i::Zero(), Eigen::Vector3i::Zero());
  gl.pixel_store_i(GL_UNPACK_ALIGNMENT, kDefaultAlignment);
}

}  // namespace

GlVolumes::GlVolumes() {
  auto functions = std::make_shared<GlVolumeFunctions>();
  functions->tex_image_3d = GlLoad<PFNGLTEXIMAGE3DPROC>("glTexImage3D");
  functions->tex_sub_image_3d = GlLoad<PFNGLTEXSUBIMAGE3DPROC>("glTexSubImage3D");
  functions->active_texture = GlLoad<PFNGLACTIVETEXTUREPROC>("glActiveTexture");
  functions->gen_textures = GlLoad<decltype(functions->gen_textures)>("glGenTextures");
  functions->delete_textures = GlLoad<decltype(functions->delete_textures)>("glDeleteTextures");
  functions->bind_texture = GlLoad<decltype(functions->bind_texture)>("glBindTexture");
  functions->tex_parameter_i = GlLoad<decltype(functions->tex_parameter_i)>("glTexParameteri");
  functions->pixel_store_i = GlLoad<decltype(functions->pixel_store_i)>("glPixelStorei");
  functions->get_integer_v = GlLoad<decltype(functions->get_integer_v)>("glGetIntegerv");
  const bool loaded = functions->tex_image_3d && functions->tex_sub_image_3d && functions->active_texture &&
                      functions->gen_textures && functions->delete_textures && functions->bind_texture &&
                      functions->tex_parameter_i && functions->pixel_store_i && functions->get_integer_v;
  if (loaded) {
    functions_ = std::move(functions);
  }
}

std::optional<Volume> GlVolumes::Create(VolumeFormat format, const Eigen::Vector3i& size) const {
  if (!functions_ || (size.array() <= 0).any()) {
    return std::nullopt;
  }
  const GlVolumeFunctions& gl = *functions_;
  GLuint id {};
  gl.gen_textures(1, &id);
  if (id == 0) {
    return std::nullopt;
  }
  const TexelFormat texel = FormatOf(format);
  gl.bind_texture(GL_TEXTURE_3D, id);
  gl.tex_parameter_i(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  gl.tex_parameter_i(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  gl.tex_parameter_i(GL_TEXTURE_3D, GL_TEXTURE_BASE_LEVEL, 0);
  gl.tex_parameter_i(GL_TEXTURE_3D, GL_TEXTURE_MAX_LEVEL, 0);
  for (const GLenum wrap : {GL_TEXTURE_WRAP_S, GL_TEXTURE_WRAP_T, GL_TEXTURE_WRAP_R}) {
    gl.tex_parameter_i(GL_TEXTURE_3D, wrap, GL_CLAMP_TO_EDGE);
  }
  gl.tex_image_3d(GL_TEXTURE_3D, 0, texel.internal, size.x(), size.y(), size.z(), 0, texel.format, texel.type,
                  nullptr);
  gl.bind_texture(GL_TEXTURE_3D, 0);
  return Volume {.id = id, .size = size, .format = format};
}

void GlVolumes::UploadLabels(const Volume& volume, const CellBox& region, std::span<const uint8_t> labels) const {
  if (!functions_ || volume.id == 0 || (region.extent.array() <= 0).any() ||
      labels.size() != static_cast<size_t>(volume.size.cast<int64_t>().prod())) {
    return;
  }
  const GlVolumeFunctions& gl = *functions_;
  const TexelFormat texel = FormatOf(volume.format);
  gl.bind_texture(GL_TEXTURE_3D, volume.id);
  SetUnpack(gl, volume.size, region.min);
  gl.tex_sub_image_3d(GL_TEXTURE_3D, 0, region.min.x(), region.min.y(), region.min.z(), region.extent.x(),
                      region.extent.y(), region.extent.z(), texel.format, texel.type, labels.data());
  ResetUnpack(gl);
  gl.bind_texture(GL_TEXTURE_3D, 0);
}

void GlVolumes::UploadColors(const Volume& volume, const CellBox& region, std::span<const Eigen::Vector4f> texels) const {
  if (!functions_ || volume.id == 0 || (region.extent.array() <= 0).any() ||
      texels.size() != static_cast<size_t>(region.extent.cast<int64_t>().prod())) {
    return;
  }
  const GlVolumeFunctions& gl = *functions_;
  const TexelFormat texel = FormatOf(volume.format);
  gl.bind_texture(GL_TEXTURE_3D, volume.id);
  SetUnpack(gl, region.extent, Eigen::Vector3i::Zero());
  gl.tex_sub_image_3d(GL_TEXTURE_3D, 0, region.min.x(), region.min.y(), region.min.z(), region.extent.x(),
                      region.extent.y(), region.extent.z(), texel.format, texel.type, texels.data()->data());
  ResetUnpack(gl);
  gl.bind_texture(GL_TEXTURE_3D, 0);
}

void GlVolumes::Bind(const Volume& volume, int unit) const {
  if (!functions_) {
    return;
  }
  functions_->active_texture(static_cast<GLenum>(GL_TEXTURE0 + unit));
  functions_->bind_texture(GL_TEXTURE_3D, volume.id);
  functions_->active_texture(GL_TEXTURE0);
}

void GlVolumes::Delete(const Volume& volume) const {
  if (functions_ && volume.id != 0) {
    functions_->delete_textures(1, &volume.id);
  }
}

int GlVolumes::MaxTextureUnits() const {
  GLint units {};
  if (functions_) {
    functions_->get_integer_v(GL_MAX_TEXTURE_IMAGE_UNITS, &units);
  }
  return units;
}

}  // namespace z13::raylib
