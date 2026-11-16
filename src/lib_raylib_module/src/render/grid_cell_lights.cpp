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

#include "grid_cell_lights.h"

#include <algorithm>

namespace z13::raylib {

const CellLighting& GridCellLights::Of(z13::station::GridId grid) const {
  const auto found = lights_.find(grid);
  return found != lights_.end() ? found->second : none_;
}

void GridCellLights::Keep(const std::set<z13::station::GridId>& grids) {
  std::erase_if(lights_, [&grids](const auto& entry) { return !grids.contains(entry.first); });
}

bool GridCellLights::SyncedWith(z13::building::primitives::OptionalPalette palette,
                                const EmergencyLamps& emergency) const {
  return std::ranges::all_of(lights_, [&](const auto& entry) { return entry.second.SyncedWith(palette, emergency); });
}

}  // namespace z13::raylib
