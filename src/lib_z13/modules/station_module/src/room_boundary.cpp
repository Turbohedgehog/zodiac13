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

#include "room_boundary.h"

#include <algorithm>

namespace z13::station::outline {

using z13::building::grid::BlockId;

std::optional<std::pair<BlockId, std::reference_wrapper<const BlockFaces>>> RoomBoundary::ObliqueAt(const Cell& cell) {
  const auto id = source_.get().index.get().At(cell);
  if (!id) {
    return std::nullopt;
  }
  auto found = faces_.find(*id);
  if (found == faces_.end()) {
    const auto block = source_.get().block_of(*id);
    found = faces_.emplace(*id, block ? FacesOf(*block, source_.get().palette.get()) : BlockFaces {}).first;
  }
  if (!found->second.oblique) {
    return std::nullopt;
  }
  return std::pair {*id, std::cref(found->second)};
}

std::vector<std::reference_wrapper<const BlockFaces>> RoomBoundary::ObliqueAround(const Cell& cell) {
  std::vector<BlockId> seen;
  std::vector<std::reference_wrapper<const BlockFaces>> around;
  for (int dx = -1; dx <= 1; ++dx) {
    for (int dy = -1; dy <= 1; ++dy) {
      const auto wall = ObliqueAt(cell + Cell(dx, dy, 0));
      if (wall && std::ranges::find(seen, wall->first) == seen.end()) {
        seen.push_back(wall->first);
        around.push_back(wall->second);
      }
    }
  }
  return around;
}

}  // namespace z13::station::outline
