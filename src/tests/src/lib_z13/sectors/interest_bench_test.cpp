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

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <iostream>
#include <random>
#include <string_view>
#include <vector>

#include <Eigen/Dense>

#include <sectors/interest_map.h>
#include <z13/components/block.h>
#include <z13_settings/building_tuning.h>
#include <z13_settings/net_tuning.h>

// How much of the relayed commands a client's interest would let through, for some layouts of 200
// players: DISABLED, run with --gtest_also_run_disabled_tests (plan: network-scaling-plan.md).
namespace z13::net::sectors {
namespace {

constexpr int kPlayers = 200;
constexpr uint32_t kSeed = 2026;
// The station's footprint and the height of its decks, in meters.
constexpr float kStationMeters = 200.f;
constexpr float kDeckMeters = 12.f;
constexpr float kHallMeters = 20.f;
constexpr int kClusters = 8;
constexpr float kClusterMeters = 12.f;

using Layout = std::vector<Eigen::Vector3f>;

Eigen::Vector3f RandomIn(std::mt19937& random, const Eigen::Vector3f& low, const Eigen::Vector3f& size) {
  std::uniform_real_distribution<float> unit(0.f, 1.f);
  return low + Eigen::Vector3f(size.x() * unit(random), size.y() * unit(random), size.z() * unit(random));
}

Layout Crowd(std::mt19937& random) {
  Layout layout;
  for (int i = 0; i < kPlayers; ++i) {
    layout.push_back(RandomIn(random, Eigen::Vector3f::Zero(), {kHallMeters, kHallMeters, 4.f}));
  }
  return layout;
}

Layout Spread(std::mt19937& random) {
  Layout layout;
  for (int i = 0; i < kPlayers; ++i) {
    layout.push_back(RandomIn(random, Eigen::Vector3f::Zero(), {kStationMeters, kStationMeters, kDeckMeters}));
  }
  return layout;
}

Layout Clusters(std::mt19937& random) {
  Layout layout;
  std::vector<Eigen::Vector3f> centers;
  for (int i = 0; i < kClusters; ++i) {
    centers.push_back(RandomIn(random, Eigen::Vector3f::Zero(), {kStationMeters, kStationMeters, kDeckMeters}));
  }
  for (int i = 0; i < kPlayers; ++i) {
    layout.push_back(RandomIn(random, centers[static_cast<size_t>(i % kClusters)], {kClusterMeters, kClusterMeters, 4.f}));
  }
  return layout;
}

Eigen::Vector3i CellOf(const Eigen::Vector3f& meters) {
  return (meters / z13::station::kCellSize).array().floor().cast<int>();
}

// The share of (sender, other client) pairs a command of the sender reaches, as CountRelay counts them.
double NeededShare(const Layout& layout, const InterestShape& shape) {
  const z13::NetTuning tuning;
  const z13::BuildingTuning building;
  const int sector_cells = static_cast<int>(tuning.sector_size_cells);
  const int reach_cells = static_cast<int>(
      std::ceil(std::max(building.destroy_reach_distance / z13::station::kCellSize, building.wall_pick_reach_cells)));
  InterestMap map;
  for (size_t i = 0; i < layout.size(); ++i) {
    map.Update(static_cast<ClientId>(i), SectorOf(CellOf(layout[i]), sector_cells), shape);
  }
  uint64_t needed = 0;
  for (size_t sender = 0; sender < layout.size(); ++sender) {
    const Eigen::Vector3i cell = CellOf(layout[sender]);
    const auto touched = SectorsTouched(
        {.min = cell - Eigen::Vector3i::Constant(reach_cells), .extent = Eigen::Vector3i::Constant((2 * reach_cells) + 1)},
        sector_cells);
    for (size_t other = 0; other < layout.size(); ++other) {
      needed += other != sender && std::ranges::any_of(touched, [&](const SectorKey& sector) {
        return map.Covers(static_cast<ClientId>(other), sector);
      });
    }
  }
  return static_cast<double>(needed) / static_cast<double>(layout.size() * (layout.size() - 1));
}

TEST(InterestBench, DISABLED_ShareOfRelaysAClientNeedsByLayout) {
  constexpr std::array<InterestShape, 4> kShapes {
      {{.radius = 0, .margin = 0}, {.radius = 1, .margin = 0}, {.radius = 1, .margin = 1}, {.radius = 2, .margin = 1}}};
  std::mt19937 random(kSeed);
  const std::array<std::pair<std::string_view, Layout>, 3> layouts {{
      {"crowd in one hall", Crowd(random)}, {"spread over the station", Spread(random)}, {"8 clusters", Clusters(random)}}};
  std::cout << "layout | radius+margin -> share of (sender, client) pairs that need the command\n";
  for (const auto& [name, layout] : layouts) {
    std::string row = std::format("{:26}", name);
    for (const InterestShape& shape : kShapes) {
      row += std::format(" | {}+{}: {:5.1f}%", shape.radius, shape.margin, 100. * NeededShare(layout, shape));
    }
    std::cout << row << "\n";
  }
}

}  // namespace
}  // namespace z13::net::sectors
