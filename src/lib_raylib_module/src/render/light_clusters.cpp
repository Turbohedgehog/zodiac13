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

#include "light_clusters.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <numeric>

#include <rlgl.h>

#include <z13/components/block.h>

#include "../platform/sdl_platform.h"

namespace z13::raylib {

namespace {

// assets/shaders/lighting.fs: kLightTexels.
constexpr int kLightTexels = 5;
constexpr int kIndicesPerTexel = 4;
constexpr size_t kFloatsPerTexel = 4;
// The shader's slot for a light without a shadow.
constexpr float kNoShadow = -1.f;
// The cone of a light shining every way: below any cosine.
constexpr float kNoCone = -2.f;

Eigen::Vector4f WithW(const Eigen::Vector3f& xyz, float w) {
  Eigen::Vector4f texel;
  texel << xyz, w;
  return texel;
}

int SliceOf(float depth, const ClusterLayout& layout) {
  const int slice = static_cast<int>(std::log(depth / layout.near) / layout.log_depth * layout.counts.z());
  return std::clamp(slice, 0, layout.counts.z() - 1);
}

int TileOf(float ndc, int screen, int tiles, int tile) {
  const float pixel = (ndc + 1.f) / 2.f * static_cast<float>(screen);
  return std::clamp(static_cast<int>(std::floor(pixel / static_cast<float>(tile))), 0, tiles - 1);
}

// The box around `box` in view space; one that reaches behind the near plane covers the whole
// screen.
std::optional<ClusterRange> RangeOf(const Eigen::AlignedBox3f& box, const ClusterView& view,
                                    const ClusterLayout& layout) {
  const Eigen::Matrix3f rotation = view.view.topLeftCorner<3, 3>();
  const Eigen::Vector3f centre = (rotation * box.center()) + view.view.topRightCorner<3, 1>();
  const Eigen::Vector3f extent = rotation.cwiseAbs() * (box.sizes() / 2.f);
  // The view looks down its -Z.
  const float nearest = -centre.z() - extent.z();
  const float farthest = -centre.z() + extent.z();
  if (farthest < view.near || nearest > view.far) {
    return std::nullopt;
  }
  Eigen::AlignedBox2f ndc(Eigen::Vector2f::Constant(-1.f), Eigen::Vector2f::Ones());
  if (nearest >= view.near) {
    // Over a positive depth, x and y are most apart at the nearest or the farthest one.
    const Eigen::Array2f from = (centre - extent).head<2>().array();
    const Eigen::Array2f to = (centre + extent).head<2>().array();
    const Eigen::Array2f focal = view.focal.array();
    ndc = ndc.intersection(Eigen::AlignedBox2f((focal * (from / nearest).min(from / farthest)).matrix(),
                                               (focal * (to / nearest).max(to / farthest)).matrix()));
  }
  if (ndc.isEmpty()) {
    return std::nullopt;
  }
  return ClusterRange {
      .min = {TileOf(ndc.min().x(), view.screen.x(), layout.counts.x(), layout.tile),
              TileOf(ndc.min().y(), view.screen.y(), layout.counts.y(), layout.tile),
              SliceOf(std::max(nearest, view.near), layout)},
      .max = {TileOf(ndc.max().x(), view.screen.x(), layout.counts.x(), layout.tile),
              TileOf(ndc.max().y(), view.screen.y(), layout.counts.y(), layout.tile),
              SliceOf(std::min(farthest, view.far), layout)},
  };
}

// Row by row, as the first cluster and how many follow it: a callback per cluster is what
// costs most in a Debug build.
template <typename Visit>
void ForEachRow(const ClusterRange& range, const ClusterLayout& layout, Visit visit) {
  const auto columns = static_cast<size_t>(layout.counts.x());
  const auto rows = static_cast<size_t>(layout.counts.y());
  const auto first_x = static_cast<size_t>(range.min.x());
  const auto width = static_cast<size_t>(range.max.x() - range.min.x() + 1);
  for (auto slice = static_cast<size_t>(range.min.z()); slice <= static_cast<size_t>(range.max.z()); ++slice) {
    for (auto y = static_cast<size_t>(range.min.y()); y <= static_cast<size_t>(range.max.y()); ++y) {
      visit((((slice * rows) + y) * columns) + first_x, width);
    }
  }
}

}  // namespace

LightClusters::~LightClusters() {
  if (!IsGlContextAlive()) {
    return;
  }
  for (const unsigned int texture : textures_) {
    if (texture != 0) {
      rlUnloadTexture(texture);
    }
  }
}

void LightClusters::Update(std::span<const FrameLight> lights, const ClusterView& view,
                           const z13::RenderTuning& tuning) {
  const int tile = tuning.light_cluster_px;
  const auto tiles = [tile](int pixels) { return std::max(1, (pixels + tile - 1) / tile); };
  layout_ = {
      .counts = {tiles(view.screen.x()), tiles(view.screen.y()), tuning.light_cluster_slices},
      .tile = tile,
      .near = tuning.light_cluster_near_m,
      .log_depth = std::log(tuning.light_cluster_far_m / tuning.light_cluster_near_m),
      .forward = view.forward,
  };
  const auto clusters = static_cast<size_t>(layout_.counts.prod());

  // The shader tests a light's reach half a cell in front of each face (assets/shaders/lighting.fs).
  const Eigen::Vector3f facing = Eigen::Vector3f::Constant(z13::station::kCellSize / 2.f);
  ranges_.clear();
  std::ranges::transform(lights, std::back_inserter(ranges_), [&](const FrameLight& light) {
    return RangeOf({light.reach.min() - facing, light.reach.max() + facing}, view, layout_);
  });
  CountLights(tuning.lights_per_cluster);
  offsets_.resize(clusters);
  std::exclusive_scan(counts_.begin(), counts_.end(), offsets_.begin(), 0);

  layout_.headers = static_cast<int>(lights.size()) * kLightTexels;
  layout_.indices = layout_.headers + static_cast<int>(clusters);
  table_.clear();
  for (const FrameLight& light : lights) {
    const std::array texels {
        WithW(light.position, light.radius),
        WithW(light.color, light.shadow_slot ? static_cast<float>(*light.shadow_slot) : kNoShadow),
        WithW(light.reach.min(), 0.f),
        WithW(light.reach.max(), 0.f),
        light.cone ? WithW(light.cone->direction, light.cone->cos_half_angle) : WithW(Eigen::Vector3f::Zero(), kNoCone),
    };
    static_assert(texels.size() == kLightTexels);
    for (const auto& texel : texels) {
      table_.insert(table_.end(), texel.begin(), texel.end());
    }
  }
  const size_t headers = table_.size();
  table_.resize(headers + (clusters * kFloatsPerTexel));
  for (size_t cluster = 0; cluster < clusters; ++cluster) {
    table_[headers + (cluster * kFloatsPerTexel)] = static_cast<float>(offsets_[cluster]);
    table_[headers + (cluster * kFloatsPerTexel) + 1] = static_cast<float>(counts_[cluster]);
  }
  // Lights in the frame's order, so a full cluster drops the farthest.
  filled_.assign(clusters, 0);
  indices_.assign(static_cast<size_t>(offsets_.back() + counts_.back()), 0.f);
  for (size_t i = 0; i < lights.size(); ++i) {
    if (!ranges_[i]) {
      continue;
    }
    ForEachRow(*ranges_[i], layout_, [&](size_t first, size_t width) {
      for (size_t cluster = first; cluster < first + width; ++cluster) {
        if (filled_[cluster] < counts_[cluster]) {
          indices_[static_cast<size_t>(offsets_[cluster] + filled_[cluster]++)] = static_cast<float>(i);
        }
      }
    });
  }
  table_.insert(table_.end(), indices_.begin(), indices_.end());
  UploadTable();
}

// +1 and -1 at the corners of each range, summed along each axis, so the cost is per light and
// per cluster, not their product.
void LightClusters::CountLights(int cap) {
  const auto columns = static_cast<size_t>(layout_.counts.x());
  const auto rows = static_cast<size_t>(layout_.counts.y());
  const auto slices = static_cast<size_t>(layout_.counts.z());
  // One more along each axis, for the -1 past a range's end.
  const size_t row = columns + 1;
  const size_t plane = row * (rows + 1);
  sums_.assign(plane * (slices + 1), 0);
  const Eigen::Vector3i strides(1, static_cast<int>(row), static_cast<int>(plane));
  for (const auto& range : ranges_) {
    if (!range) {
      continue;
    }
    const Eigen::Vector3i past = range->max + Eigen::Vector3i::Ones();
    for (unsigned corner = 0; corner < (1U << 3); ++corner) {
      // Bit i of `corner` picks the far bound along axis i.
      const Eigen::Array3i far_along(corner & 1U, (corner >> 1) & 1U, (corner >> 2) & 1U);
      const Eigen::Vector3i cell = (far_along == 1).select(past, range->min);
      sums_[static_cast<size_t>(cell.dot(strides))] += std::popcount(corner) % 2 == 0 ? 1 : -1;
    }
  }
  for (size_t i = 0; i < sums_.size(); ++i) {
    if (i % row != 0) {
      sums_[i] += sums_[i - 1];
    }
  }
  for (size_t i = 0; i < sums_.size(); ++i) {
    if (i % plane >= row) {
      sums_[i] += sums_[i - row];
    }
  }
  for (size_t i = plane; i < sums_.size(); ++i) {
    sums_[i] += sums_[i - plane];
  }
  counts_.clear();
  for (size_t z = 0; z < slices; ++z) {
    for (size_t y = 0; y < rows; ++y) {
      for (size_t x = 0; x < columns; ++x) {
        counts_.push_back(std::min(sums_[(z * plane) + (y * row) + x], cap));
      }
    }
  }
}

void LightClusters::UploadTable() {
  const size_t row_floats = static_cast<size_t>(kLightTableWidth) * kFloatsPerTexel;
  const int rows = std::max(1, static_cast<int>((table_.size() + row_floats - 1) / row_floats));
  table_.resize(static_cast<size_t>(rows) * row_floats);
  current_ = (current_ + 1) % textures_.size();
  unsigned int& texture = textures_[current_];
  if (rows > texture_rows_[current_]) {
    if (texture != 0) {
      rlUnloadTexture(texture);
    }
    // Room to grow, so it isn't remade each time the lists get longer.
    texture_rows_[current_] = rows * 2;
    texture = rlLoadTexture(nullptr, kLightTableWidth, texture_rows_[current_], RL_PIXELFORMAT_UNCOMPRESSED_R32G32B32A32, 1);
  }
  rlUpdateTexture(texture, 0, 0, kLightTableWidth, rows, RL_PIXELFORMAT_UNCOMPRESSED_R32G32B32A32, table_.data());
}

void LightClusters::Upload(const ::Shader& shader, const LightUniforms& uniforms) const {
  const Eigen::Vector2f depth(layout_.near, layout_.log_depth);
  SetShaderValue(shader, uniforms.cluster_headers, &layout_.headers, SHADER_UNIFORM_INT);
  SetShaderValue(shader, uniforms.cluster_indices, &layout_.indices, SHADER_UNIFORM_INT);
  SetShaderValue(shader, uniforms.cluster_counts, layout_.counts.data(), SHADER_UNIFORM_IVEC3);
  SetShaderValue(shader, uniforms.cluster_tile, &layout_.tile, SHADER_UNIFORM_INT);
  SetShaderValue(shader, uniforms.cluster_depth, depth.data(), SHADER_UNIFORM_VEC2);
  SetShaderValue(shader, uniforms.view_forward, layout_.forward.data(), SHADER_UNIFORM_VEC3);
}

void LightClusters::Bind() const {
  rlActiveTextureSlot(kLightTableUnit);
  rlEnableTexture(textures_[current_]);
  rlActiveTextureSlot(0);
}

}  // namespace z13::raylib
