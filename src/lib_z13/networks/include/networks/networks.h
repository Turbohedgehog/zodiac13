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

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include <primitives/cell_box.h>
#include <z13/components/station/conduit.h>
#include <z13/components/station/grid_id.h>

namespace z13::utility::networks {

// A caller's id for a run or a port: how it finds its network again.
using ItemId = uint64_t;

struct ConduitRun {
  ItemId id {};
  std::string network;
  z13::conduit::Conduit conduit;
};

// A block's connection to a network: it joins the runs in its cells and in the cells touching them.
struct Port {
  ItemId id {};
  std::string network;
  z13::station::GridId grid = z13::station::kMainGrid;
  z13::building::primitives::CellBox cells;
};

// Where two grids meet (a dock): what touches the cell `a` of grid `a_grid` in a network joins what touches the cell
// `b` of grid `b_grid` in it, as a cable laid through the door would.
struct Link {
  std::string network;
  z13::station::GridId a_grid = z13::station::kMainGrid;
  Eigen::Vector3i a_cell = Eigen::Vector3i::Zero();
  z13::station::GridId b_grid = z13::station::kMainGrid;
  Eigen::Vector3i b_cell = Eigen::Vector3i::Zero();
};

struct Network {
  std::string key;
  std::vector<ItemId> runs;
  std::vector<ItemId> ports;
};

class Networks {
 public:
  // In an order that depends on the cells only: by key, then by the grid and the least cell of the network's first
  // item.
  const std::vector<Network>& All() const { return networks_; }
  std::optional<size_t> OfRun(ItemId id) const;
  std::optional<size_t> OfPort(ItemId id) const;

 private:
  friend Networks BuildNetworks(std::span<const ConduitRun> runs, std::span<const Port> ports,
                                std::span<const Link> links);

  std::vector<Network> networks_;
  std::unordered_map<ItemId, size_t> of_run_;
  std::unordered_map<ItemId, size_t> of_port_;
};

// Runs of one network and grid that share a face join, and so do a port and the runs touching its cells. Two ports
// meet only through a run; a port no run touches is a network of its own. A link then joins what touches its two
// cells, across grids. The result doesn't depend on the order of the inputs or on their ids.
Networks BuildNetworks(std::span<const ConduitRun> runs, std::span<const Port> ports,
                       std::span<const Link> links = {});

}  // namespace z13::utility::networks
