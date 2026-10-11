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

#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <vector>

#include <Eigen/Dense>
#include <raylib.h>

#include <z13_settings/settings.h>

#include "lights.h"

namespace z13::raylib {

// The view the clusters split, in meters.
struct ClusterView {
  // World to view space, which looks down its -Z.
  Eigen::Matrix4f view = Eigen::Matrix4f::Identity();
  // The projection's x and y scales.
  Eigen::Vector2f focal = Eigen::Vector2f::Zero();
  // Unit.
  Eigen::Vector3f forward = Eigen::Vector3f::Zero();
  // In pixels.
  Eigen::Vector2i screen = Eigen::Vector2i::Zero();
  // The clip planes.
  float near {};
  float far {};
};

// Where the shader finds what LightClusters::Update laid out (assets/shaders/lighting.fs).
struct ClusterLayout {
  Eigen::Vector3i counts = Eigen::Vector3i::Ones();
  int tile {};
  float near {};
  // log(far / near), the depth the slices split.
  float log_depth {};
  Eigen::Vector3f forward = Eigen::Vector3f::Zero();
  int headers {};
  int indices {};
};

// The clusters a light covers, bounds included.
struct ClusterRange {
  Eigen::Vector3i min = Eigen::Vector3i::Zero();
  Eigen::Vector3i max = Eigen::Vector3i::Zero();
};

// Forward+ lighting: the frame's lights in one table texture with, for each cluster of the
// view (a screen tile times a slice of depth), the lights reaching it, so a fragment loops over
// those alone however many the frame has.
class LightClusters {
 public:
  LightClusters() = default;
  ~LightClusters();
  LightClusters(const LightClusters&) = delete;
  LightClusters& operator=(const LightClusters&) = delete;

  // A cluster keeps the first `tuning.lights_per_cluster` of `lights` reaching it.
  void Update(std::span<const FrameLight> lights, const ClusterView& view, const z13::RenderTuning& tuning);
  void Upload(const ::Shader& shader, const LightUniforms& uniforms) const;
  // On kLightTableUnit, for the lighting shader.
  void Bind() const;

 private:
  // counts_ from ranges_, each at most `cap`.
  void CountLights(int cap);
  void UploadTable();

  ClusterLayout layout_;
  // RGBA texels.
  std::vector<float> table_;
  // Kept between frames so an update allocates nothing.
  std::vector<std::optional<ClusterRange>> ranges_;
  std::vector<int> sums_;
  std::vector<int> counts_;
  std::vector<int> offsets_;
  std::vector<int> filled_;
  std::vector<float> indices_;
  // Written in turn, so a frame doesn't wait for the GPU to finish reading the last one.
  std::array<unsigned int, 2> textures_ {};
  std::array<int, 2> texture_rows_ {};
  size_t current_ {};
};

}  // namespace z13::raylib
