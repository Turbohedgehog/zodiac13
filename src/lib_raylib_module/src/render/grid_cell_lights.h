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

#include <map>
#include <set>

#include <z13/components/station/grid_pose.h>

#include "cell_lighting.h"

namespace z13::raylib {

// The baked light of every grid but the main one (CellLighting is the main grid's), each in its grid's own cells.
// Singleton exempt from the "no pointers" rule (see CLAUDE.md); never state.
class GridCellLights {
 public:
  using Singleton = void;

  // The light of `grid`, made empty if it has none yet.
  CellLighting& Make(z13::station::GridId grid) { return lights_[grid]; }
  // A light that shines nowhere for a grid without one.
  const CellLighting& Of(z13::station::GridId grid) const;
  // Drops the light of every grid not among `grids`.
  void Keep(const std::set<z13::station::GridId>& grids);
  bool SyncedWith(z13::building::primitives::OptionalPalette palette, const EmergencyLamps& emergency) const;

 private:
  std::map<z13::station::GridId, CellLighting> lights_;
  CellLighting none_;
};

}  // namespace z13::raylib
