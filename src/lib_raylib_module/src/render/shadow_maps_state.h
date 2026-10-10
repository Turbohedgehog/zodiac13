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
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include <Eigen/Dense>
#include <raylib.h>

#include "shadow_maps.h"

namespace z13::raylib {

// What a lamp's shadow slot was last drawn for.
struct SlotView {
  // A light with an owner keeps its slot as it moves.
  std::optional<uint64_t> owner;
  Eigen::Vector3f light = Eigen::Vector3f::Zero();
  // A spot's direction; zero for a light shining every way.
  Eigen::Vector3f direction = Eigen::Vector3f::Zero();
  float radius {};
  uint64_t static_version {};
  // One bit per cube face players were in: those faces are redrawn until they have left.
  uint8_t moving_faces {};
};

// What a directional map was last drawn for.
struct DirectionalView {
  Eigen::Vector3f direction = Eigen::Vector3f::Zero();
  uint64_t static_version {};
  // The casters' bounds, apart: AlignedBox3f has no ==.
  Eigen::Vector3f bounds_min = Eigen::Vector3f::Zero();
  Eigen::Vector3f bounds_max = Eigen::Vector3f::Zero();
  int size {};

  bool operator==(const DirectionalView&) const = default;
};

struct DirectionalShadow {
  // Owns the depth texture.
  unsigned int framebuffer {};
  unsigned int texture {};
  int size {};
  // The size it failed at, so it isn't retried every frame.
  std::optional<int> failed_size;
  std::optional<DirectionalView> drawn;
  Eigen::Matrix4f view_projection = Eigen::Matrix4f::Identity();
};

class ShadowMaps::State {
 public:
  explicit State(::Shader shadow_shader);
  ~State();
  State(const State&) = delete;
  State& operator=(const State&) = delete;

  Status UpdateLamps(std::span<FrameLight> lights, const ShadowCasters& casters, const z13::RenderTuning& tuning);
  ShadowAtlasLayout AtlasLayout() const { return layout_; }
  std::optional<Eigen::Matrix4f> UpdateDirectional(DirectionalMap map, const Eigen::Vector3f& direction,
                                                   const ShadowCasters& casters, const z13::RenderTuning& tuning);
  void Bind() const;

 private:
  Status ResizeAtlas(int slots, int tile_size);
  void ReleaseAtlas();
  // The slot each of `lights` keeps or takes; nothing for those past `new_slots` new ones.
  std::vector<std::optional<size_t>> AssignSlots(std::span<const FrameLight> lights, int new_slots) const;
  // The faces of a kept slot to redraw, and the blocks' version they then show.
  std::pair<uint8_t, uint64_t> FacesToRedraw(const SlotView& drawn, const FrameLight& light,
                                             const ShadowCasters& casters, uint8_t moving_faces, int& redraws_left) const;
  void DrawFaces(size_t slot, const FrameLight& light, const ShadowCasters& casters, uint8_t faces);
  // False if no depth map can be made at `size`.
  bool ResizeDirectional(DirectionalShadow& shadow, int size);
  // Clears and draws the casters `keep` keeps within `scissor`, in pixels, or all of the map.
  void DrawDirectional(const DirectionalShadow& shadow, const ShadowCasters& casters, const CasterFilter& keep,
                       std::optional<Eigen::AlignedBox2i> scissor);
  ::Material Material();

  std::shared_ptr<::Shader> shader_;
  int light_position_location_ {};
  int light_radius_location_ {};
  std::array<::MaterialMap, kMaterialMaps> maps_ {};

  // One framebuffer and depth buffer the size of the atlas.
  unsigned int atlas_framebuffer_ {};
  unsigned int atlas_texture_ {};
  ShadowAtlasLayout layout_;
  std::vector<std::optional<SlotView>> slots_;
  // The slot count and tile size the atlas failed at, so it isn't retried every frame.
  std::optional<Eigen::Vector2i> failed_atlas_;

  std::array<DirectionalShadow, 2> directional_;
};

}  // namespace z13::raylib
