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

#include <primitives/placement.h>
#include <z13/components/conduit.h>

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
  z13::building::primitives::CellBox cells;
};

struct Network {
  std::string key;
  std::vector<ItemId> runs;
  std::vector<ItemId> ports;
};

class Networks {
 public:
  // In an order that depends on the cells only: by key, then by the least cell of the network.
  const std::vector<Network>& All() const { return networks_; }
  std::optional<size_t> OfRun(ItemId id) const;
  std::optional<size_t> OfPort(ItemId id) const;

 private:
  friend Networks BuildNetworks(std::span<const ConduitRun> runs, std::span<const Port> ports);

  std::vector<Network> networks_;
  std::unordered_map<ItemId, size_t> of_run_;
  std::unordered_map<ItemId, size_t> of_port_;
};

// Runs of one network that share a face join, and so do a port and the runs touching its cells. Two ports meet
// only through a run; a port no run touches is a network of its own. The result doesn't depend on the order of the
// inputs or on their ids.
Networks BuildNetworks(std::span<const ConduitRun> runs, std::span<const Port> ports);

}  // namespace z13::utility::networks
