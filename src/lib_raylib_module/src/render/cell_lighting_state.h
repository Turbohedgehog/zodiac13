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
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include <cell_light/cell_light.h>
#include <primitives/cell_box.h>
#include <primitives/geometry.h>

#include "cell_lighting.h"
#include "gl_volumes.h"

namespace z13::raylib {

struct SealedBoxesKey {
  z13::building::primitives::BlockShapeKey shape;
  z13::station::Orientation orientation {};

  bool operator==(const SealedBoxesKey&) const = default;
};

struct SealedBoxesKeyHash {
  size_t operator()(const SealedBoxesKey& key) const;
};

class CellLighting::State {
 public:
  State() = default;
  ~State();
  State(const State&) = delete;
  State& operator=(const State&) = delete;

  bool SyncedWith(z13::building::primitives::OptionalPalette palette, const EmergencyLamps& emergency) const;
  Status Sync(std::span<const z13::station::Block> blocks, z13::building::primitives::OptionalPalette palette,
              const EmergencyLamps& emergency);
  const std::optional<std::string>& Failure() const { return failure_; }
  const std::vector<z13::station::light::Lamp>& ShiningLamps() const { return shining_; }

  void Upload(const ::Shader& shader, const LightUniforms& uniforms, const Eigen::Matrix4f& from_world) const;
  void Bind() const;

 private:
  // Whether the light was made for this palette, whatever lamps it has been given since.
  bool BuiltFrom(z13::building::primitives::OptionalPalette palette) const;
  // New textures for the light's layout, filled whole.
  Status Remake();
  void UploadLabels(const z13::building::primitives::CellBox& cells) const;
  void UploadLight(const z13::building::primitives::CellBox& cubes) const;
  void ReleaseVolumes();
  // Cached by shape and turn, then moved to the block's cell.
  std::vector<z13::building::primitives::CellBox> SealedBoxesOf(const z13::station::Block& block,
                                                                z13::building::primitives::OptionalPalette palette);

  std::optional<GlVolumes> gl_;
  std::optional<z13::station::light::CellLight> light_;
  std::optional<Volume> labels_;
  // Per node texel kind, kPiecesPerNode stacks of the cubes along z.
  std::optional<Volume> colors_;
  // What the light was built from, sorted, to tell what changed.
  std::vector<z13::building::primitives::CellBox> opaque_;
  std::vector<z13::station::light::GlassBox> glass_;
  std::vector<z13::station::light::Lamp> lamps_;
  std::vector<z13::station::light::Lamp> shining_;
  std::unordered_map<SealedBoxesKey, std::vector<z13::building::primitives::CellBox>, SealedBoxesKeyHash> sealed_;
  // Sum of the blocks' hashes, so their order doesn't matter.
  uint64_t blocks_fingerprint_ {};
  std::optional<uint64_t> palette_hash_;
  std::vector<z13::utility::DimmedLamp> dimmed_;
  int emergency_percent_ {};
  // Why the textures couldn't be made; the lamps then light nothing.
  std::optional<std::string> failure_;
};

}  // namespace z13::raylib
