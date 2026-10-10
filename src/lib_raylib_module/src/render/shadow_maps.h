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
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <vector>

#include <Eigen/Dense>
#include <raylib.h>

#include <lib_core/utils/status.h>
#include <z13_settings/settings.h>

#include "lights.h"

namespace z13::raylib {

// Whether to draw a caster with these bounds, in meters.
using CasterFilter = std::function<bool(const Eigen::AlignedBox3f& bounds)>;

// A player, drawn into each face of a lamp's shadow its box is in.
struct MovingCaster {
  uint64_t entity {};
  Eigen::Matrix4f transform = Eigen::Matrix4f::Identity();
  Eigen::AlignedBox3f box;
};

// What casts shadows, in meters.
struct ShadowCasters {
  std::function<void(const CasterFilter& filter, const ::Material& material)> draw_static;
  // Changes whenever the opaque blocks do.
  uint64_t static_version {};
  // Where the blocks changed since a static_version.
  std::function<Eigen::AlignedBox3f(uint64_t version)> changed_since;
  // Of every block; nothing without blocks.
  std::optional<Eigen::AlignedBox3f> static_bounds;
  std::function<void(const MovingCaster& caster, const ::Material& material)> draw_moving;
  std::vector<MovingCaster> moving;
};

enum class DirectionalMap : uint8_t { kSun, kFill };

// The lamps' shadows, a cube of tiles each in one atlas, and the depth maps of the sun and
// the fill over the station, each redrawn only when what it shows changed. Singleton exempt
// from the "no pointers" rule (see CLAUDE.md); never state.
class ShadowMaps {
 public:
  using Singleton = void;

  // Without the shadow shader (id 0) nothing casts shadows.
  explicit ShadowMaps(::Shader shadow_shader = {});

  // Gives the first `tuning.shadow_lights` of `lights` a shadow slot each, kept across frames;
  // up to `tuning.shadow_updates_per_frame` lamps a frame take a new slot or redraw for changed
  // blocks, the rest wait. Fails once if the atlas can't be made.
  Status UpdateLamps(std::span<FrameLight> lights, const ShadowCasters& casters, const z13::RenderTuning& tuning);
  ShadowAtlasLayout AtlasLayout() const;

  // The map's view-projection, for the lighting shader; nothing without blocks.
  std::optional<Eigen::Matrix4f> UpdateDirectional(DirectionalMap map, const Eigen::Vector3f& direction,
                                                   const ShadowCasters& casters, const z13::RenderTuning& tuning);

  // On their texture units (lights.h), for the lighting shader.
  void Bind() const;

 private:
  class State;

  std::shared_ptr<State> state_;
};

// assets/shaders/shadow.{vs,fs}; id 0 on compile failure.
::Shader LoadShadowShader();

}  // namespace z13::raylib
