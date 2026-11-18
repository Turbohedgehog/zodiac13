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
#include <string>
#include <vector>

#include <Eigen/Core>
#include <raylib.h>

#include <cell_light/lamp.h>
#include <z13/components/utility/lamps.h>
#include <lib_core/utils/status.h>
#include <primitives/geometry.h>
#include <rooms/parallel_for.h>
#include <z13/components/station/block.h>

#include "lights.h"

namespace z13::raylib {

// The lamps that shine as emergency light and how brightly (PowerTuning::emergency_light_percent).
struct EmergencyLamps {
  std::span<const z13::utility::DimmedLamp> dimmed;
  int percent {};
};

// What a sync needs besides the blocks: where the floods may run side by side, and where the player is, in cells, so
// the lamps around them are lit first.
struct SyncContext {
  z13::station::rooms::ParallelFor parallel = z13::station::rooms::Serial;
  Eigen::Vector3f focus = Eigen::Vector3f::Zero();
};

// The station's lamps baked into its free cells (cell_light/cell_light.h) and handed to the
// lighting shader as two 3D textures, updated where blocks or lamps change. Singleton exempt
// from the "no pointers" rule (see CLAUDE.md); never state.
class CellLighting {
 public:
  using Singleton = void;

  CellLighting();

  bool SyncedWith(z13::building::primitives::OptionalPalette palette, const EmergencyLamps& emergency) const;
  // Needs the GL context. Fails once when the textures can't be made; the lamps then light nothing.
  // Lights a few lamps a call, the nearest to `context.focus` first, until SyncedWith.
  Status Sync(std::span<const z13::station::Block> blocks, z13::building::primitives::OptionalPalette palette,
              const EmergencyLamps& emergency, const SyncContext& context);
  // Whether only lamps are left to light: LightLamps then does what Sync would, without collecting the blocks.
  bool OnlyLampsPending(z13::building::primitives::OptionalPalette palette, const EmergencyLamps& emergency) const;
  Status LightLamps(const SyncContext& context);
  // Why the textures couldn't be made, for the pause menu's diagnostics.
  std::optional<std::string> Failure() const;
  // In cells, where each shines from (CellLight::ShiningLamps).
  const std::vector<z13::station::light::Lamp>& ShiningLamps() const;

  // `from_world` takes the world's meters to the meters of the grid this light was baked for.
  void Upload(const ::Shader& shader, const LightUniforms& uniforms, const Eigen::Matrix4f& from_world) const;
  // On kCellLabelsUnit and kCellLightUnit (lights.h), for the lighting shader.
  void Bind() const;

 private:
  class State;

  std::shared_ptr<State> state_;
};

}  // namespace z13::raylib
