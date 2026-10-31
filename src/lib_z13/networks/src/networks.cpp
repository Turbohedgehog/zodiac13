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

#include <networks/networks.h>

#include <algorithm>
#include <array>
#include <iterator>
#include <numeric>
#include <string_view>
#include <utility>

namespace z13::utility::networks {

namespace {

using z13::building::primitives::CellBox;
using z13::building::primitives::CellHash;

constexpr int kFaceTouch = 1;

std::array<int, 3> AsArray(const Eigen::Vector3i& v) {
  return {v.x(), v.y(), v.z()};
}

// The order that makes the result independent of the inputs': content first, ids only to break a tie.
struct ItemOrder {
  std::string_view network;
  std::array<int, 3> min {};
  std::array<int, 3> extent {};
  bool is_port {};
  ItemId id {};

  auto operator<=>(const ItemOrder&) const = default;
};

struct Item {
  ItemOrder order;
  CellBox box;
  // The conduit's cells, one by one; empty for a port.
  std::vector<Eigen::Vector3i> cells;
};

CellBox BoxOf(const z13::conduit::Conduit& conduit) {
  Eigen::Vector3i extent = Eigen::Vector3i::Ones();
  extent[std::to_underlying(conduit.axis)] = conduit.length;
  return {.min = conduit.cell, .extent = extent};
}

Item RunItem(const ConduitRun& run) {
  const CellBox box = BoxOf(run.conduit);
  Item item {.order = {.network = run.network, .min = AsArray(box.min), .extent = AsArray(box.extent), .id = run.id},
             .box = box};
  for (int i = 0; i < run.conduit.length; ++i) {
    item.cells.push_back(run.conduit.cell + Eigen::Vector3i::Unit(std::to_underlying(run.conduit.axis)) * i);
  }
  return item;
}

Item PortItem(const Port& port) {
  return {.order = {.network = port.network,
                    .min = AsArray(port.cells.min),
                    .extent = AsArray(port.cells.extent),
                    .is_port = true,
                    .id = port.id},
          .box = port.cells};
}

// Whether the boxes overlap or meet at a face: the cells nearest each other are at most one step apart.
bool Touch(const CellBox& a, const CellBox& b) {
  const Eigen::Vector3i gap_a_to_b = b.min - (a.End() - Eigen::Vector3i::Ones());
  const Eigen::Vector3i gap_b_to_a = a.min - (b.End() - Eigen::Vector3i::Ones());
  const Eigen::Vector3i gap = gap_a_to_b.cwiseMax(gap_b_to_a).cwiseMax(0);
  return gap.sum() <= kFaceTouch;
}

class Groups {
 public:
  explicit Groups(size_t size) : parent_(size) { std::iota(parent_.begin(), parent_.end(), size_t {}); }

  size_t Find(size_t item) {
    while (parent_[item] != item) {
      parent_[item] = parent_[parent_[item]];
      item = parent_[item];
    }
    return item;
  }

  void Join(size_t a, size_t b) { parent_[Find(a)] = Find(b); }

 private:
  std::vector<size_t> parent_;
};

void JoinRuns(std::span<const Item> network, size_t first, Groups& groups) {
  const std::array<Eigen::Vector3i, 7> meeting = {
      Eigen::Vector3i::Zero(), Eigen::Vector3i::UnitX(), -Eigen::Vector3i::UnitX(), Eigen::Vector3i::UnitY(),
      -Eigen::Vector3i::UnitY(), Eigen::Vector3i::UnitZ(), -Eigen::Vector3i::UnitZ()};
  std::unordered_map<Eigen::Vector3i, size_t, CellHash> run_in_cell;
  for (size_t i = 0; i < network.size(); ++i) {
    for (const Eigen::Vector3i& cell : network[i].cells) {
      for (const Eigen::Vector3i& offset : meeting) {
        if (const auto found = run_in_cell.find(cell + offset); found != run_in_cell.end()) {
          groups.Join(first + i, found->second);
        }
      }
      run_in_cell.emplace(cell, first + i);
    }
  }
}

void JoinPorts(std::span<const Item> network, size_t first, Groups& groups) {
  for (size_t port = 0; port < network.size(); ++port) {
    if (!network[port].order.is_port) {
      continue;
    }
    for (size_t run = 0; run < network.size(); ++run) {
      if (!network[run].order.is_port && Touch(network[port].box, network[run].box)) {
        groups.Join(first + port, first + run);
      }
    }
  }
}

}  // namespace

std::optional<size_t> Networks::OfRun(ItemId id) const {
  const auto found = of_run_.find(id);
  return found != of_run_.end() ? std::optional(found->second) : std::nullopt;
}

std::optional<size_t> Networks::OfPort(ItemId id) const {
  const auto found = of_port_.find(id);
  return found != of_port_.end() ? std::optional(found->second) : std::nullopt;
}

Networks BuildNetworks(std::span<const ConduitRun> runs, std::span<const Port> ports) {
  std::vector<Item> items;
  items.reserve(runs.size() + ports.size());
  std::ranges::transform(runs, std::back_inserter(items), RunItem);
  std::ranges::transform(ports, std::back_inserter(items), PortItem);
  std::ranges::sort(items, {}, &Item::order);

  Groups groups(items.size());
  for (size_t first = 0; first < items.size();) {
    const auto rest = std::span<const Item>(items).subspan(first);
    const auto key = rest.front().order.network;
    const auto network = rest.first(static_cast<size_t>(
        std::ranges::find_if(rest, [key](const Item& item) { return item.order.network != key; }) - rest.begin()));
    JoinRuns(network, first, groups);
    JoinPorts(network, first, groups);
    first += network.size();
  }

  Networks result;
  std::unordered_map<size_t, size_t> network_of_group;
  for (size_t i = 0; i < items.size(); ++i) {
    const auto [slot, added] = network_of_group.emplace(groups.Find(i), result.networks_.size());
    if (added) {
      result.networks_.push_back({.key = std::string(items[i].order.network)});
    }
    Network& network = result.networks_[slot->second];
    (items[i].order.is_port ? network.ports : network.runs).push_back(items[i].order.id);
    (items[i].order.is_port ? result.of_port_ : result.of_run_).emplace(items[i].order.id, slot->second);
  }
  return result;
}

}  // namespace z13::utility::networks
