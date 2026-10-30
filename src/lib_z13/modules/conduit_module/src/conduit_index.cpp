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


#include "conduit_index.h"

#include <algorithm>

#include <grid/cell_walk.h>

#include "conduit_run.h"

namespace z13::conduit {

void ConduitIndex::Insert(ConduitId id, std::string_view network, const Conduit& conduit) {
  for (const Eigen::Vector3i& cell : CellsOf(conduit)) {
    by_cell_[cell].push_back({.network = std::string(network), .id = id});
  }
}

void ConduitIndex::Erase(ConduitId id, const Conduit& conduit) {
  for (const Eigen::Vector3i& cell : CellsOf(conduit)) {
    const auto found = by_cell_.find(cell);
    if (found == by_cell_.end()) {
      continue;
    }
    std::erase_if(found->second, [id](const ConduitInCell& entry) { return entry.id == id; });
    if (found->second.empty()) {
      by_cell_.erase(found);
    }
  }
}

bool ConduitIndex::Overlaps(std::string_view network, const Conduit& conduit) const {
  return std::ranges::any_of(CellsOf(conduit), [&](const Eigen::Vector3i& cell) {
    return std::ranges::contains(At(cell), network, &ConduitInCell::network);
  });
}

std::span<const ConduitInCell> ConduitIndex::At(const Eigen::Vector3i& cell) const {
  const auto found = by_cell_.find(cell);
  return found != by_cell_.end() ? std::span<const ConduitInCell>(found->second) : std::span<const ConduitInCell>();
}

std::optional<ConduitId> ConduitIndex::Raycast(
    const Eigen::Vector3f& from, const Eigen::Vector3f& to, std::string_view network) const {
  std::optional<ConduitId> hit;
  z13::building::grid::WalkCells(from, to, [&](const z13::building::grid::CellStep& at) {
    const std::span<const ConduitInCell> here = At(at.cell);
    if (here.empty()) {
      return false;
    }
    const auto same = std::ranges::find(here, network, &ConduitInCell::network);
    hit = same != here.end() ? same->id : std::ranges::min(here, {}, &ConduitInCell::network).id;
    return true;
  });
  return hit;
}

}  // namespace z13::conduit
