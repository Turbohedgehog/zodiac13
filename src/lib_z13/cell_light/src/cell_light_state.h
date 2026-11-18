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
#include <rooms/parallel_for.h>

#include "glass_bridges.h"
#include "lamp_flood.h"
#include "light_nodes.h"

namespace z13::station::light {

class CellLight::State {
 public:
  State() = default;
  State(std::span<const z13::building::primitives::CellBox> opaque, std::span<const GlassBox> glass,
        std::span<const Lamp> lamps, z13::station::rooms::ParallelFor parallel);

  LightChanges Apply(const LightEdits& edits);

  const LightNodes& Nodes() const { return nodes_; }
  Eigen::Vector3f LightAt(size_t node) const;
  Eigen::Vector3f DirectionAt(size_t node) const;
  std::vector<CellTexel> CellTexels(const z13::building::primitives::CellBox& cells) const;
  std::vector<Eigen::Vector4f> Texels(const z13::building::primitives::CellBox& cubes, uint8_t piece,
                                      NodeTexel texel) const;
  std::vector<Lamp> Lamps() const;
  std::vector<Lamp> ShiningLamps() const;

 private:
  // Labels every cube and floods every lamp of a new layout, the blocks kept from `nodes`.
  void Rebuild(const LightLayout& layout, const LightNodes& nodes, const LightEdits& edits, std::vector<Lamp> lamps);
  // Works out what a lamp reaches; reads the nodes only, so lamps can be measured side by side.
  void Measure(LampState& lamp, FloodMarks& marks) const;
  // Floods the lamps of `lamps_` at these indices: measured in parallel, then added in order, so the sums are the same
  // as one by one.
  void Flood(std::span<const size_t> indices);
  // Measures the lamps at these indices in parallel; adds nothing.
  void MeasureAll(std::span<const size_t> indices);
  // Adds only what differs between `lamp` and `before`; `before`'s nodes inside `relabelled` were cleared and count as 0.
  void AddChange(const LampState& lamp, const LampState& before, const z13::building::primitives::CellBox& relabelled);
  bool ReachesCubes(const LampState& lamp, const z13::building::primitives::CellBox& cubes) const;
  // Takes the lamp's light back from every node outside `kept_cubes`.
  void TakeBack(const LampState& lamp, const z13::building::primitives::CellBox& kept_cubes);
  // Adds the lamp's light to every node outside `kept_cubes`, or takes it back for sign -1.
  void Add(const LampState& lamp, float sign, const z13::building::primitives::CellBox& kept_cubes);
  void GroupByRange(LampState& lamp) const;
  void AddContributions(const Eigen::Vector3f& color, const Eigen::Vector3f& position,
                        std::span<const Contribution> contributions, float sign,
                        const z13::building::primitives::CellBox& kept_cubes);
  void ClearCubes(const z13::building::primitives::CellBox& cubes);
  // The tiles of `cubes` holding a cube touched since the last call, which forgets them.
  std::vector<z13::building::primitives::CellBox> TakeTouchedTiles(const z13::building::primitives::CellBox& cubes);
  // The aligned upload tiles next to a cube where a node started or stopped being lit; remembers the lit nodes.
  std::vector<z13::building::primitives::CellBox> LitFlips(
      std::span<const z13::building::primitives::CellBox> tiles);

  LightNodes nodes_;
  GlassBridges bridges_;
  std::vector<Eigen::Vector3f> light_;
  // The luminance-weighted sum of the directions to each node's lamps, and that luminance.
  std::vector<Eigen::Vector3f> directions_;
  std::vector<float> luminance_;
  // Per cube: whether its light was touched since the renderer last took the changes.
  std::vector<uint8_t> touched_;
  // Per node: whether it was lit when the renderer last took the changes; a flip changes what nearby opaque cells borrow.
  std::vector<uint8_t> lit_;
  std::vector<LampState> lamps_;
  // Per node, zero between calls: scratch of `AddChange`.
  std::vector<float> weight_change_;
  // Per chunk of lamps measured side by side.
  std::vector<FloodMarks> marks_;
  z13::station::rooms::ParallelFor parallel_ = z13::station::rooms::Serial;
};

}  // namespace z13::station::light
