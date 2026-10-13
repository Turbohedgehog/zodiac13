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
#include <vector>

#include <raylib.h>

#include <cell_light/lamp.h>
#include <lib_core/utils/status.h>
#include <primitives/geometry.h>
#include <z13/components/block.h>

#include "lights.h"

namespace z13::raylib {

// The station's lamps baked into its free cells (cell_light/cell_light.h) and handed to the
// lighting shader as two 3D textures, updated where blocks or lamps change. Singleton exempt
// from the "no pointers" rule (see CLAUDE.md); never state.
class CellLighting {
 public:
  using Singleton = void;

  CellLighting();

  bool SyncedWith(z13::building::primitives::OptionalPalette palette) const;
  // Needs the GL context. Fails once when the textures can't be made; the lamps then light nothing.
  Status Sync(std::span<const z13::station::Block> blocks, z13::building::primitives::OptionalPalette palette);
  // In cells.
  const std::vector<z13::station::light::Lamp>& Lamps() const;

  void Upload(const ::Shader& shader, const LightUniforms& uniforms) const;
  // On kCellLabelsUnit and kCellLightUnit (lights.h), for the lighting shader.
  void Bind() const;

 private:
  class State;

  std::shared_ptr<State> state_;
};

}  // namespace z13::raylib
