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
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>

#include <primitives/placement.h>

#include <cell_light/light_edits.h>

#include "light_nodes.h"

namespace z13::station::light {

// Where the light of a node goes through a pane of glass to the node on its other side.
struct Bridge {
  uint32_t node {};
  // The share of the light left after the pane.
  float transmission {};
  uint32_t pane {};
};

// The panes of glass (opaque to the nodes, which keeps the two sides apart) and, per node, the
// bridges across them. A pane bridges along its thinnest axes, from the cell before it to the one after.
class GlassBridges {
 public:
  void Add(const GlassBox& glass);
  // Drops one pane equal to `glass` and its bridges.
  void Remove(const GlassBox& glass);
  // Finds the bridges again of every pane next to `cells`, after those cells were labelled.
  void Refresh(const LightNodes& nodes, const z13::building::primitives::CellBox& cells);
  void RefreshAll(const LightNodes& nodes);

  std::span<const Bridge> From(size_t node) const;

 private:
  struct Pane {
    uint32_t id {};
    GlassBox glass;
    std::vector<std::pair<uint32_t, uint32_t>> crossings;
  };

  void Rebuild(const LightNodes& nodes, Pane& pane);
  void Forget(const Pane& pane);

  std::vector<Pane> panes_;
  std::unordered_map<uint32_t, std::vector<Bridge>> bridges_;
  uint32_t last_pane_ {};
};

}  // namespace z13::station::light
