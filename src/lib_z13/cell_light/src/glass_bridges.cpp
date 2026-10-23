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

#include "glass_bridges.h"

#include <algorithm>

namespace z13::station::light {

namespace {

using z13::building::primitives::CellBox;

constexpr int kAxes = 3;

}  // namespace

void GlassBridges::Add(const GlassBox& glass) {
  panes_.push_back({.id = ++last_pane_, .glass = glass});
}

void GlassBridges::Remove(const GlassBox& glass) {
  const auto found = std::ranges::find(panes_, glass, &Pane::glass);
  if (found != panes_.end()) {
    Forget(*found);
    panes_.erase(found);
  }
}

void GlassBridges::Refresh(const LightNodes& nodes, const CellBox& cells) {
  for (Pane& pane : panes_) {
    if (pane.glass.cells.Grown(Eigen::Vector3i::Ones()).Overlaps(cells)) {
      Rebuild(nodes, pane);
    }
  }
}

void GlassBridges::RefreshAll(const LightNodes& nodes) {
  bridges_.clear();
  for (Pane& pane : panes_) {
    pane.crossings.clear();
    Rebuild(nodes, pane);
  }
}

std::span<const Bridge> GlassBridges::From(size_t node) const {
  const auto found = bridges_.find(static_cast<uint32_t>(node));
  return found == bridges_.end() ? std::span<const Bridge> {} : std::span<const Bridge>(found->second);
}

void GlassBridges::Rebuild(const LightNodes& nodes, Pane& pane) {
  Forget(pane);
  pane.crossings.clear();
  const CellBox& cells = pane.glass.cells;
  const int thinnest = cells.extent.minCoeff();
  for (int axis = 0; axis < kAxes; ++axis) {
    if (cells.extent[axis] != thinnest) {
      continue;
    }
    const int first = (axis + 1) % kAxes;
    const int second = (axis + 2) % kAxes;
    for (int u = 0; u < cells.extent[first]; ++u) {
      for (int v = 0; v < cells.extent[second]; ++v) {
        Eigen::Vector3i before = cells.min;
        before[first] += u;
        before[second] += v;
        Eigen::Vector3i after = before;
        before[axis] = cells.min[axis] - 1;
        after[axis] = cells.End()[axis];
        const auto a = nodes.NodeAt(before);
        const auto b = nodes.NodeAt(after);
        if (a && b && *a != *b) {
          pane.crossings.emplace_back(static_cast<uint32_t>(*a), static_cast<uint32_t>(*b));
        }
      }
    }
  }
  std::ranges::sort(pane.crossings);
  pane.crossings.erase(std::ranges::unique(pane.crossings).begin(), pane.crossings.end());
  for (const auto& [a, b] : pane.crossings) {
    bridges_[a].push_back({.node = b, .transmission = pane.glass.transmission, .pane = pane.id});
    bridges_[b].push_back({.node = a, .transmission = pane.glass.transmission, .pane = pane.id});
  }
}

void GlassBridges::Forget(const Pane& pane) {
  for (const auto& [a, b] : pane.crossings) {
    for (const uint32_t node : {a, b}) {
      if (const auto found = bridges_.find(node); found != bridges_.end()) {
        std::erase_if(found->second, [&pane](const Bridge& bridge) { return bridge.pane == pane.id; });
        if (found->second.empty()) {
          bridges_.erase(found);
        }
      }
    }
  }
}

}  // namespace z13::station::light
