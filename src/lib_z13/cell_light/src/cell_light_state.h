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
#include <span>
#include <vector>

#include <Eigen/Dense>

#include <cell_light/cell_light.h>

#include "glass_bridges.h"
#include "lamp_flood.h"
#include "light_nodes.h"

namespace z13::station::light {

class CellLight::State {
 public:
  State() = default;
  State(std::span<const z13::building::primitives::CellBox> opaque, std::span<const GlassBox> glass,
        std::span<const Lamp> lamps);

  LightChanges Apply(const LightEdits& edits);

  const LightNodes& Nodes() const { return nodes_; }
  Eigen::Vector3f LightAt(size_t node) const;
  Eigen::Vector3f DirectionAt(size_t node) const;
  std::vector<Eigen::Vector4f> Texels(const z13::building::primitives::CellBox& cubes, uint8_t piece,
                                      NodeTexel texel) const;
  std::vector<Lamp> Lamps() const;
  std::vector<Lamp> ShiningLamps() const;

 private:
  // Labels every cube and floods every lamp of a new layout, the blocks kept from `nodes`.
  void Rebuild(const LightLayout& layout, const LightNodes& nodes, const LightEdits& edits, std::vector<Lamp> lamps);
  void Flood(LampState& lamp);
  // Takes the lamp's light back from every node outside `kept_cubes`.
  void TakeBack(const LampState& lamp, const z13::building::primitives::CellBox& kept_cubes);
  // Adds the lamp's light to every node outside `kept_cubes`, or takes it back for sign -1.
  void Add(const LampState& lamp, float sign, const z13::building::primitives::CellBox& kept_cubes);
  void ClearCubes(const z13::building::primitives::CellBox& cubes);

  LightNodes nodes_;
  GlassBridges bridges_;
  std::vector<Eigen::Vector3f> light_;
  // The luminance-weighted sum of the directions to each node's lamps, and that luminance.
  std::vector<Eigen::Vector3f> directions_;
  std::vector<float> luminance_;
  std::vector<LampState> lamps_;
  FloodMarks marks_;
};

}  // namespace z13::station::light
