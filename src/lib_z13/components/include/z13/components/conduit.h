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
#include <cstdint>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

#include <Eigen/Dense>
#include <flecs.h>

#include <z13/components/color.h>

// Cables, wires and pipes laid through the station (docs/station-systems-plan.md, "Проводка"):
// conduit_module lays them, the networks read them, a render module draws them.
namespace z13::conduit {

// One kind of conduit, from assets/station/conduits.json.
struct ConduitKind {
  uint32_t id {};
  std::string name;
  // The network it carries ("power", "data", "gas"): a cell holds one conduit of each network.
  std::string network;
  float thickness_cells {};
  z13::Rgba color {};
};

struct ConduitKinds {
  using Singleton = void;
  std::vector<ConduitKind> kinds;

  std::optional<ConduitKind> Find(uint32_t id) const;
};

enum class ConduitAxis : uint8_t { kX, kY, kZ };

// A straight run of `length` cells from `cell` along `axis`. Conduits are not blocks: they share cells with the
// station's blocks, and rooms, physics and light don't see them.
struct Conduit {
  using State = void;
  uint32_t kind {};
  Eigen::Vector3i cell = Eigen::Vector3i::Zero();
  ConduitAxis axis {};
  int length = 1;

  bool operator==(const Conduit&) const = default;
};

// Where the tool lays: into the cell of the aimed block (a wall, a floor panel), or in front of it.
enum class ConduitDepth : uint8_t { kInside, kOnSurface };

// On a conduit tool item.
struct ConduitTool {
  using State = void;
  uint32_t kind {};
  ConduitDepth depth {};
};

// A player laying a run: the cell where the lay key went down.
struct ConduitDrag {
  using State = void;
  Eigen::Vector3i anchor = Eigen::Vector3i::Zero();
};

// A fixed order of runs by content, so peers and rebuilds see them alike whatever their entity ids.
inline bool ConduitOrder(const Conduit& a, const Conduit& b) {
  const auto key = [](const Conduit& c) {
    return std::make_tuple(c.kind, c.cell.x(), c.cell.y(), c.cell.z(), c.axis, c.length);
  };
  return key(a) < key(b);
}

inline std::vector<Conduit> SortedConduits(const flecs::query<const Conduit>& query) {
  std::vector<Conduit> conduits;
  query.each([&conduits](const Conduit& conduit) { conduits.push_back(conduit); });
  std::ranges::sort(conduits, ConduitOrder);
  return conduits;
}

inline std::optional<ConduitKind> ConduitKinds::Find(uint32_t id) const {
  const auto found = std::ranges::find(kinds, id, &ConduitKind::id);
  return found != kinds.end() ? std::optional(*found) : std::nullopt;
}

}  // namespace z13::conduit
