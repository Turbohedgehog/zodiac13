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

#include "conduit_cells.h"

#include <algorithm>
#include <utility>

namespace z13::conduit_render {

bool ConduitCells::Rebuild(std::vector<z13::conduit::Conduit> conduits, const z13::conduit::ConduitKinds& kinds) {
  if (conduits == built_) {
    return false;
  }
  built_ = std::move(conduits);
  by_cell_.clear();
  for (const z13::conduit::Conduit& conduit : built_) {
    const auto kind = kinds.Find(conduit.kind);
    if (!kind) {
      continue;
    }
    const auto axis = static_cast<int>(conduit.axis);
    for (int step = 0; step < conduit.length; ++step) {
      Eigen::Vector3i cell = conduit.cell;
      cell[axis] += step;
      by_cell_[cell].push_back({.network = kind->network, .axis = conduit.axis});
    }
  }
  return true;
}

bool ConduitCells::Turns(std::string_view network, const Eigen::Vector3i& cell, z13::conduit::ConduitAxis axis) const {
  const auto found = by_cell_.find(cell);
  return found != by_cell_.end() && std::ranges::any_of(found->second, [&](const Occupant& occupant) {
           return occupant.network == network && occupant.axis != axis;
         });
}

}  // namespace z13::conduit_render
