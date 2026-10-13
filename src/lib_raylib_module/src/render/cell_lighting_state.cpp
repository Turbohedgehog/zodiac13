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

#include "cell_lighting_state.h"

#include <algorithm>
#include <array>
#include <format>
#include <iterator>
#include <utility>

#include <primitives/draw_order.h>
#include <primitives/lights.h>

#include "../platform/sdl_platform.h"

namespace z13::raylib {

namespace {

using z13::building::primitives::CellBox;
using z13::station::light::CellLight;
using z13::station::light::Lamp;
using z13::station::light::LightEdits;
using z13::station::light::NodeTexel;
using z13::station::light::kPiecesPerNode;

constexpr std::array kNodeTexels {NodeTexel::kLight, NodeTexel::kDirection};

bool BoxBefore(const CellBox& a, const CellBox& b) {
  const auto key = [](const CellBox& box) {
    return std::array {box.min.x(), box.min.y(), box.min.z(), box.extent.x(), box.extent.y(), box.extent.z()};
  };
  return key(a) < key(b);
}

bool LampBefore(const Lamp& a, const Lamp& b) {
  const auto key = [](const Lamp& lamp) {
    return std::array {lamp.position.x(), lamp.position.y(), lamp.position.z(), lamp.color.x(),
                       lamp.color.y(),    lamp.color.z(),    lamp.radius};
  };
  return key(a) < key(b);
}

// What `after` has that `before` lacks; both sorted by `before_order`.
template <typename T, typename Order>
std::vector<T> Missing(const std::vector<T>& after, const std::vector<T>& before, Order before_order) {
  std::vector<T> missing;
  std::ranges::set_difference(after, before, std::back_inserter(missing), before_order);
  return missing;
}

// The light's volume of colors: per texel kind and piece, the cubes stacked along z.
CellBox ColorRegion(const CellBox& cubes, const Eigen::Vector3i& all_cubes, size_t kind, uint8_t piece) {
  CellBox region = cubes;
  region.min.z() += static_cast<int>((kind * kPiecesPerNode) + piece) * all_cubes.z();
  return region;
}

}  // namespace

CellLighting::State::~State() {
  if (IsGlContextAlive()) {
    ReleaseVolumes();
  }
}

bool CellLighting::State::SyncedWith(z13::building::primitives::OptionalPalette palette) const {
  return light_ && palette_hash_ == (palette ? std::optional(palette->get().hash) : std::nullopt);
}

Status CellLighting::State::Sync(std::span<const z13::station::Block> blocks,
                                 z13::building::primitives::OptionalPalette palette) {
  if (failed_) {
    return {};
  }
  std::vector<CellBox> opaque;
  for (const z13::station::Block& block : blocks) {
    if (!z13::building::primitives::IsTransparent(block, palette)) {
      opaque.push_back(z13::building::primitives::OccupiedCells(block));
    }
  }
  std::ranges::sort(opaque, BoxBefore);
  std::vector<Lamp> lamps;
  std::ranges::transform(z13::building::primitives::LightsOf(blocks, palette), std::back_inserter(lamps),
                         z13::station::light::LampOf);
  std::ranges::sort(lamps, LampBefore);

  if (!SyncedWith(palette)) {
    light_.emplace(opaque, lamps);
    palette_hash_ = palette ? std::optional(palette->get().hash) : std::nullopt;
    opaque_ = std::move(opaque);
    lamps_ = std::move(lamps);
    return Remake();
  }
  const LightEdits edits {
      .opaque_added = Missing(opaque, opaque_, BoxBefore),
      .opaque_removed = Missing(opaque_, opaque, BoxBefore),
      .lamps_added = Missing(lamps, lamps_, LampBefore),
      .lamps_removed = Missing(lamps_, lamps, LampBefore),
  };
  opaque_ = std::move(opaque);
  lamps_ = std::move(lamps);
  const z13::station::light::LightChanges changes = light_->Apply(edits);
  if (changes.resized) {
    return Remake();
  }
  if (changes.labels) {
    UploadLabels(*changes.labels);
  }
  if (changes.light) {
    UploadLight(*changes.light);
  }
  return {};
}

Status CellLighting::State::Remake() {
  if (!gl_) {
    gl_.emplace();
  }
  ReleaseVolumes();
  const z13::station::light::LightLayout& layout = light_->Layout();
  if (layout.CubeCount() == 0) {
    return {};
  }
  if (gl_->MaxTextureUnits() <= kCellLightUnit) {
    failed_ = true;
    return std::unexpected(std::format("cell light needs {} texture units, the driver has {}", kCellLightUnit + 1,
                                       gl_->MaxTextureUnits()));
  }
  const Eigen::Vector3i colors_size(layout.cubes.x(), layout.cubes.y(),
                                    layout.cubes.z() * kPiecesPerNode * static_cast<int>(kNodeTexels.size()));
  labels_ = gl_->Create(VolumeFormat::kLabels, layout.Cells());
  colors_ = gl_->Create(VolumeFormat::kColors, colors_size);
  if (!labels_ || !colors_) {
    ReleaseVolumes();
    failed_ = true;
    return std::unexpected(std::format("can't make the cell light textures for {} x {} x {} cubes", layout.cubes.x(),
                                       layout.cubes.y(), layout.cubes.z()));
  }
  UploadLabels(layout.CellBounds());
  UploadLight({.min = Eigen::Vector3i::Zero(), .extent = layout.cubes});
  return {};
}

void CellLighting::State::UploadLabels(const CellBox& cells) const {
  if (!labels_) {
    return;
  }
  const z13::station::light::LightLayout& layout = light_->Layout();
  const CellBox local {.min = cells.min - layout.origin, .extent = cells.extent};
  gl_->UploadLabels(*labels_, local, light_->Labels());
}

void CellLighting::State::UploadLight(const CellBox& cubes) const {
  if (!colors_) {
    return;
  }
  const Eigen::Vector3i& all_cubes = light_->Layout().cubes;
  for (size_t kind = 0; kind < kNodeTexels.size(); ++kind) {
    for (uint8_t piece = 0; piece < kPiecesPerNode; ++piece) {
      gl_->UploadColors(*colors_, ColorRegion(cubes, all_cubes, kind, piece),
                        light_->Texels(cubes, piece, kNodeTexels[kind]));
    }
  }
}

void CellLighting::State::ReleaseVolumes() {
  const auto release = [this](std::optional<Volume>& volume) {
    if (volume && gl_) {
      gl_->Delete(*volume);
    }
    volume.reset();
  };
  release(labels_);
  release(colors_);
}

void CellLighting::State::Upload(const ::Shader& shader, const LightUniforms& uniforms) const {
  const int enabled = labels_ && colors_ ? 1 : 0;
  SetShaderValue(shader, uniforms.cell_light_enabled, &enabled, SHADER_UNIFORM_INT);
  if (enabled == 0) {
    return;
  }
  const z13::station::light::LightLayout& layout = light_->Layout();
  SetShaderValue(shader, uniforms.cell_origin, layout.origin.data(), SHADER_UNIFORM_IVEC3);
  SetShaderValue(shader, uniforms.cell_cubes, layout.cubes.data(), SHADER_UNIFORM_IVEC3);
}

void CellLighting::State::Bind() const {
  if (labels_ && colors_) {
    gl_->Bind(*labels_, kCellLabelsUnit);
    gl_->Bind(*colors_, kCellLightUnit);
  }
}

}  // namespace z13::raylib
