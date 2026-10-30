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
#include <cmath>
#include <limits>

#include "conduit_run.h"

namespace z13::conduit {

namespace {

// The cells a segment passes, in order (Amanatides and Woo); `visit` returns true to stop.
template <class Visit>
void WalkCells(const Eigen::Vector3f& from, const Eigen::Vector3f& to, Visit visit) {
  const Eigen::Vector3f direction = to - from;
  Eigen::Vector3i cell = from.array().floor().cast<int>();
  const Eigen::Vector3i last = to.array().floor().cast<int>();
  Eigen::Vector3i step = Eigen::Vector3i::Zero();
  Eigen::Vector3f next_t = Eigen::Vector3f::Constant(std::numeric_limits<float>::infinity());
  Eigen::Vector3f delta_t = next_t;
  for (Eigen::Index axis = 0; axis < 3; ++axis) {
    if (direction[axis] == 0.f) {
      continue;
    }
    step[axis] = direction[axis] > 0.f ? 1 : -1;
    const float boundary = static_cast<float>(cell[axis] + (step[axis] > 0 ? 1 : 0));
    next_t[axis] = (boundary - from[axis]) / direction[axis];
    delta_t[axis] = 1.f / std::abs(direction[axis]);
  }
  while (!visit(cell) && cell != last) {
    Eigen::Index axis = 0;
    if (next_t.minCoeff(&axis) > 1.f) {
      return;
    }
    cell[axis] += step[axis];
    next_t[axis] += delta_t[axis];
  }
}

}  // namespace

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
  WalkCells(from, to, [&](const Eigen::Vector3i& cell) {
    const std::span<const ConduitInCell> here = At(cell);
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
