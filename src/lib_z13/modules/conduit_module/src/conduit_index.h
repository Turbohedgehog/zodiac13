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

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <Eigen/Dense>

#include <primitives/placement.h>
#include <z13/components/conduit.h>

namespace z13::conduit {

// A conduit's flecs entity id; the index itself doesn't depend on flecs.
using ConduitId = uint64_t;

struct ConduitInCell {
  std::string network;
  ConduitId id {};
};

// Which conduits pass through which cells, at most one of a network per cell. Built for the tick's edits from the
// Conduit components: there are few conduits next to the blocks, and nothing has to follow them between ticks.
class ConduitIndex {
 public:
  void Insert(ConduitId id, std::string_view network, const Conduit& conduit);
  void Erase(ConduitId id, const Conduit& conduit);
  // Some cell of `conduit` has one of `network` already.
  bool Overlaps(std::string_view network, const Conduit& conduit) const;
  // The conduits in `cell`, in no particular order.
  std::span<const ConduitInCell> At(const Eigen::Vector3i& cell) const;
  // The conduit in the first cell along the segment (both ends in cells) that has any: the one of `network` there,
  // else the one of the first network by name (entity ids differ between peers).
  std::optional<ConduitId> Raycast(
      const Eigen::Vector3f& from, const Eigen::Vector3f& to, std::string_view network) const;

 private:
  std::unordered_map<Eigen::Vector3i, std::vector<ConduitInCell>, z13::building::primitives::CellHash> by_cell_;
};

}  // namespace z13::conduit
