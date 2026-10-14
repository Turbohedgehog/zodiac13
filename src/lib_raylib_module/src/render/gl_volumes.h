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

#include <cstdint>
#include <memory>
#include <optional>
#include <span>

#include <Eigen/Dense>

#include <primitives/placement.h>

namespace z13::raylib {

struct GlVolumeFunctions;

enum class VolumeFormat : uint8_t {
  // A byte per texel, read as an unsigned integer.
  kLabels,
  // Four half floats per texel, uploaded as floats.
  kColors,
};

// A 3D texture: rlgl has none, so these go to OpenGL directly, through the entry points of
// the current context (SDL owns it).
struct Volume {
  unsigned int id {};
  Eigen::Vector3i size = Eigen::Vector3i::Zero();
  VolumeFormat format {};
};

class GlVolumes {
 public:
  // Loads the entry points; needs the GL context current.
  GlVolumes();

  // Nothing if the driver refused it.
  std::optional<Volume> Create(VolumeFormat format, const Eigen::Vector3i& size) const;
  // `region` of a kLabels volume from `labels`, which spans all of it.
  void UploadLabels(const Volume& volume, const z13::building::primitives::CellBox& region,
                    std::span<const uint8_t> labels) const;
  // `region` of a kColors volume from `texels`, which holds just that region, x fastest.
  void UploadColors(const Volume& volume, const z13::building::primitives::CellBox& region,
                    std::span<const Eigen::Vector4f> texels) const;
  void Bind(const Volume& volume, int unit) const;
  void Delete(const Volume& volume) const;
  int MaxTextureUnits() const;

 private:
  std::shared_ptr<const GlVolumeFunctions> functions_;
};

}  // namespace z13::raylib
