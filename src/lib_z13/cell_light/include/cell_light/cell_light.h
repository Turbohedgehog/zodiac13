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

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

#include <Eigen/Dense>

#include <primitives/placement.h>

#include <cell_light/lamp.h>
#include <cell_light/light_edits.h>
#include <cell_light/light_layout.h>

namespace z13::station::light {

// The static lamps' light, flooded through the free cells (light_layout.h) so it is the same
// from wherever it is seen, kept per node and updated where blocks or lamps change.
class CellLight {
 public:
  CellLight();
  CellLight(std::span<const z13::building::primitives::CellBox> opaque, std::span<const Lamp> lamps);

  LightChanges Apply(const LightEdits& edits);

  const LightLayout& Layout() const;
  // Per cell of the layout (CellIndex).
  const std::vector<uint8_t>& Labels() const;
  // The node of a cell; nothing for an opaque cell or one outside the layout.
  std::optional<size_t> NodeAt(const Eigen::Vector3i& cell) const;
  // Per node (NodeIndex), what its lamps give it.
  Eigen::Vector3f LightAt(size_t node) const;
  // The way to its lamps, as long as one side outweighs the others: unit for a single lamp,
  // shorter the more evenly lamps surround it, zero without light.
  Eigen::Vector3f DirectionAt(size_t node) const;
  // One piece of every cube in `cubes`, x fastest, as RGBA texels (alpha unused).
  std::vector<Eigen::Vector4f> Texels(const z13::building::primitives::CellBox& cubes, uint8_t piece,
                                      NodeTexel texel) const;
  std::vector<Lamp> Lamps() const;
  // The lamps whose light gets out, each placed where it shines from (EmitterOf).
  std::vector<Lamp> ShiningLamps() const;
  size_t JoinedCubes() const;

 private:
  class State;

  std::shared_ptr<State> state_;
};

}  // namespace z13::station::light
