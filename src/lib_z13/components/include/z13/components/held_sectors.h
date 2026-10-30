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

#include <optional>
#include <vector>

#include <Eigen/Dense>

namespace z13::station {

struct SealedBounds {
  Eigen::Vector3i min = Eigen::Vector3i::Zero();
  Eigen::Vector3i extent = Eigen::Vector3i::Zero();

  bool operator==(const SealedBounds&) const = default;
};

// The cells the sealed blocks span, none without any; kept by the rooms of a world that holds them all.
struct StationBounds {
  using Singleton = void;
  std::optional<SealedBounds> sealed;

  bool operator==(const StationBounds&) const = default;
};

// The sectors (cubes of `sector_cells` cells) whose blocks this world holds, sorted, when it holds only those: a client
// under interest (docs/interest-design.md). The rooms take the edge of what it holds for a wall inside the server's
// `station` bounds; outside them both sides see the vacuum.
struct HeldSectors {
  using Singleton = void;
  std::optional<std::vector<Eigen::Vector3i>> sectors;
  int sector_cells {};
  std::optional<SealedBounds> station;

  bool operator==(const HeldSectors&) const = default;
};

}  // namespace z13::station
