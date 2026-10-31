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
#include <random>
#include <vector>

#include <Eigen/Dense>

#include <networks/networks.h>

namespace z13::utility::networks {
namespace {

using z13::conduit::Conduit;
using z13::conduit::ConduitAxis;

constexpr uint32_t kCable = 1;

ConduitRun RunOf(ItemId id, const char* network, Eigen::Vector3i cell, ConduitAxis axis, int length) {
  return {.id = id, .network = network, .conduit = {.kind = kCable, .cell = cell, .axis = axis, .length = length}};
}

Port PortAt(ItemId id, const char* network, Eigen::Vector3i cell, Eigen::Vector3i extent = Eigen::Vector3i::Ones()) {
  return {.id = id, .network = network, .cells = {.min = cell, .extent = extent}};
}

TEST(NetworksTest, RunsEndToEndOrTurningAtAFaceAreOneNetwork) {
  const std::vector runs = {RunOf(1, "power", {0, 0, 0}, ConduitAxis::kX, 4), RunOf(2, "power", {4, 0, 0}, ConduitAxis::kX, 2),
                            RunOf(3, "power", {5, 1, 0}, ConduitAxis::kY, 3)};

  const Networks networks = BuildNetworks(runs, {});

  ASSERT_EQ(networks.All().size(), 1u);
  EXPECT_EQ(networks.All().front().runs.size(), 3u);
}

TEST(NetworksTest, ARunSeparatedByACellOrOnlyTouchingAtAnEdgeIsAnotherNetwork) {
  const std::vector runs = {RunOf(1, "power", {0, 0, 0}, ConduitAxis::kX, 2), RunOf(2, "power", {3, 0, 0}, ConduitAxis::kX, 2),
                            RunOf(3, "power", {1, 1, 1}, ConduitAxis::kX, 1)};

  const Networks networks = BuildNetworks(runs, {});

  EXPECT_EQ(networks.All().size(), 3u);
  EXPECT_NE(networks.OfRun(1), networks.OfRun(2));
  EXPECT_NE(networks.OfRun(1), networks.OfRun(3));
}

TEST(NetworksTest, NetworksOfDifferentKindsNeverJoin) {
  const std::vector runs = {RunOf(1, "power", {0, 0, 0}, ConduitAxis::kX, 3), RunOf(2, "data", {0, 1, 0}, ConduitAxis::kX, 3)};

  const Networks networks = BuildNetworks(runs, {});

  ASSERT_EQ(networks.All().size(), 2u);
  EXPECT_EQ(networks.All()[*networks.OfRun(1)].key, "power");
  EXPECT_EQ(networks.All()[*networks.OfRun(2)].key, "data");
}

TEST(NetworksTest, APortJoinsARunInItsCellsOrNextToThem) {
  const std::vector runs = {RunOf(1, "power", {0, 0, 0}, ConduitAxis::kX, 3)};
  const std::vector ports = {
      PortAt(10, "power", {1, 0, 0}),                               // in the run's cell
      PortAt(11, "power", {1, 1, 0}),                               // across a face
      PortAt(12, "power", {1, 1, 1}),                               // diagonal to the run: only an edge
      PortAt(13, "power", {3, 0, 0}),                               // past the end, across the end face
      PortAt(14, "power", {-1, -1, 0}, {4, 4, 1}),                  // a block around the run
      PortAt(15, "gas", {1, 1, 0})};                                // another network's

  const Networks networks = BuildNetworks(runs, ports);

  const auto joined = networks.OfRun(1);
  EXPECT_EQ(networks.OfPort(10), joined);
  EXPECT_EQ(networks.OfPort(11), joined);
  EXPECT_NE(networks.OfPort(12), joined);
  EXPECT_EQ(networks.OfPort(13), joined);
  EXPECT_EQ(networks.OfPort(14), joined);
  EXPECT_NE(networks.OfPort(15), joined);
}

TEST(NetworksTest, TwoPortsMeetOnlyThroughARunAndAPortAloneIsANetwork) {
  const std::vector runs = {RunOf(1, "power", {0, 0, 0}, ConduitAxis::kX, 5)};
  const std::vector ports = {PortAt(10, "power", {0, 1, 0}), PortAt(11, "power", {4, 1, 0}), PortAt(12, "power", {0, 3, 0}),
                             PortAt(13, "power", {0, 4, 0})};

  const Networks networks = BuildNetworks(runs, ports);

  EXPECT_EQ(networks.OfPort(10), networks.OfPort(11));
  EXPECT_NE(networks.OfPort(12), networks.OfPort(13));
  EXPECT_NE(networks.OfPort(12), networks.OfPort(10));
  EXPECT_EQ(networks.All().size(), 3u);
}

// The same cells in any order, under any ids, make the same networks in the same order.
TEST(NetworksTest, TheResultFollowsTheCellsNotTheOrderOrTheIds) {
  std::vector<ConduitRun> runs = {RunOf(0, "power", {0, 0, 0}, ConduitAxis::kX, 4), RunOf(0, "power", {4, 0, 0}, ConduitAxis::kZ, 3),
                           RunOf(0, "power", {9, 9, 9}, ConduitAxis::kY, 2), RunOf(0, "gas", {0, 1, 0}, ConduitAxis::kX, 4)};
  std::vector<Port> ports = {PortAt(0, "power", {2, 1, 0}), PortAt(0, "power", {9, 8, 9}), PortAt(0, "gas", {7, 7, 7})};
  const auto keyed = [](const Networks& networks) {
    std::vector<std::pair<std::string, std::pair<size_t, size_t>>> shape;
    for (const Network& network : networks.All()) {
      shape.push_back({network.key, {network.runs.size(), network.ports.size()}});
    }
    return shape;
  };
  std::mt19937 random(7);
  const auto reference = [&] {
    ItemId id = 1;
    for (ConduitRun& run : runs) { run.id = id++; }
    for (Port& port : ports) { port.id = id++; }
    return keyed(BuildNetworks(runs, ports));
  }();

  for (int round = 0; round < 20; ++round) {
    std::ranges::shuffle(runs, random);
    std::ranges::shuffle(ports, random);
    ItemId id = 100 + static_cast<ItemId>(random() % 1000);
    for (ConduitRun& run : runs) { run.id = id++; }
    for (Port& port : ports) { port.id = id++; }

    EXPECT_EQ(keyed(BuildNetworks(runs, ports)), reference);
  }
}

}  // namespace
}  // namespace z13::utility::networks
