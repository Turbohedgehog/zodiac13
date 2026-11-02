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

#include <algorithm>
#include <tuple>
#include <vector>

#include <Eigen/Dense>

#include <z13/components/station/grid.h>

// The lamps that shine as emergency light (docs/station-systems-plan.md, "Электричество"), set by power_module and
// read by the renderer.
namespace z13::utility {

// On a station that has had power, the lamps its networks cannot supply glow dim and red instead of going dark.
// Latched: a station without producers (a scene nobody wired) keeps its lamps lit, and destroying the last reactor
// does not light them again.
struct LampsFollowPower {
  using State = void;
  using Singleton = void;
  bool on {};

  bool operator==(const LampsFollowPower&) const = default;
};

struct DimmedLamp {
  z13::station::GridId grid = z13::station::kMainGrid;
  // The lamp block's own cell (Block::cell).
  Eigen::Vector3i cell = Eigen::Vector3i::Zero();

  bool operator==(const DimmedLamp&) const = default;
};

inline bool DimmedLampBefore(const DimmedLamp& first, const DimmedLamp& second) {
  return std::tie(first.grid, first.cell.x(), first.cell.y(), first.cell.z()) <
         std::tie(second.grid, second.cell.x(), second.cell.y(), second.cell.z());
}

// By DimmedLampBefore. Derived, never state.
struct DimmedLamps {
  using Singleton = void;
  std::vector<DimmedLamp> list;

  bool operator==(const DimmedLamps&) const = default;
};

inline bool IsDimmed(const std::vector<DimmedLamp>& sorted, const DimmedLamp& lamp) {
  return std::ranges::binary_search(sorted, lamp, DimmedLampBefore);
}

}  // namespace z13::utility
