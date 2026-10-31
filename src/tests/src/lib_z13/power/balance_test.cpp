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

#include <vector>

#include <power/balance.h>
#include <power/port_names.h>
#include <z13_tests/shipped_station.h>

namespace z13::utility::power {
namespace {

constexpr int64_t kStep = 6;

std::vector<bool> Satisfied(const Balance& balance) {
  std::vector<bool> satisfied;
  for (const ConsumerSupply& supply : balance.supplies) {
    satisfied.push_back(supply.satisfied);
  }
  return satisfied;
}

TEST(BalanceTest, ProductionServesEveryConsumerItCovers) {
  const Grid grid {.producers = {{.id = 1, .watts = 500}},
                   .consumers = {{.id = 2, .watts = 200}, {.id = 3, .watts = 300}}};

  const Balance balance = Solve(grid, kStep);

  EXPECT_EQ(Satisfied(balance), (std::vector<bool> {true, true}));
  EXPECT_EQ(balance.served, 500);
  EXPECT_EQ(balance.produced, 500);
}

TEST(BalanceTest, ANetworkWithoutProducersServesNothing) {
  const Grid grid {.consumers = {{.id = 2, .watts = 10}}};

  EXPECT_EQ(Satisfied(Solve(grid, kStep)), std::vector<bool> {false});
}

TEST(BalanceTest, ALackCutsTheLowestPrioritiesFirst) {
  const Grid grid {.producers = {{.id = 1, .watts = 300}},
                   .consumers = {{.id = 2, .watts = 200, .priority = 3},
                                 {.id = 3, .watts = 200, .priority = 1},
                                 {.id = 4, .watts = 50, .priority = 2}}};

  const Balance balance = Solve(grid, kStep);

  // Priority 1 takes 200, priority 2 takes 50, priority 3 no longer fits.
  EXPECT_EQ(Satisfied(balance), (std::vector<bool> {false, true, true}));
  EXPECT_EQ(balance.supplies[1].id, 3u) << "supplies keep the order of the consumers";
}

TEST(BalanceTest, WithinAPriorityTheOrderGivenDecides) {
  const Grid grid {.producers = {{.id = 1, .watts = 100}},
                   .consumers = {{.id = 2, .watts = 80, .priority = 1}, {.id = 3, .watts = 80, .priority = 1}}};

  EXPECT_EQ(Satisfied(Solve(grid, kStep)), (std::vector<bool> {true, false}));
}

TEST(BalanceTest, AConsumerThatDoesNotFitDoesNotLetASmallerOneBehindItThrough) {
  const Grid grid {.producers = {{.id = 1, .watts = 100}},
                   .consumers = {{.id = 2, .watts = 150, .priority = 1}, {.id = 3, .watts = 10, .priority = 2}}};

  EXPECT_EQ(Satisfied(Solve(grid, kStep)), (std::vector<bool> {false, false}));
}

TEST(BalanceTest, SurplusChargesBatteriesInOrderUpToTheirRateAndCapacity) {
  const Grid grid {.producers = {{.id = 1, .watts = 500}},
                   .consumers = {{.id = 2, .watts = 100}},
                   .batteries = {{.id = 3, .capacity = 1000, .charge = 0, .rate = 150},
                                 {.id = 4, .capacity = 100000, .charge = 0, .rate = 1000}}};

  const Balance balance = Solve(grid, kStep);

  // 400 W left: 150 W into the first, which has room for 1000 / 6 = 166 W of it, and 250 W into the second.
  EXPECT_EQ(balance.charges, (std::vector<int64_t> {150 * kStep, 250 * kStep}));
}

TEST(BalanceTest, ABatteryDoesNotChargePastItsCapacity) {
  const Grid grid {.producers = {{.id = 1, .watts = 500}},
                   .batteries = {{.id = 3, .capacity = 100, .charge = 70, .rate = 1000}}};

  EXPECT_EQ(Solve(grid, kStep).charges, std::vector<int64_t> {100});
}

TEST(BalanceTest, BatteriesCoverAShortfallAndEmptyAsTheyGo) {
  const Grid grid {.producers = {{.id = 1, .watts = 100}},
                   .consumers = {{.id = 2, .watts = 250}},
                   .batteries = {{.id = 3, .capacity = 5000, .charge = 3000, .rate = 100},
                                 {.id = 4, .capacity = 5000, .charge = 5000, .rate = 1000}}};

  const Balance balance = Solve(grid, kStep);

  // 150 W short: the first gives its 100 W, the second the other 50.
  EXPECT_EQ(Satisfied(balance), std::vector<bool> {true});
  EXPECT_EQ(balance.charges, (std::vector<int64_t> {3000 - 100 * kStep, 5000 - 50 * kStep}));
}

TEST(BalanceTest, ABatteryGivesNoMoreThanItHolds) {
  const Grid grid {.consumers = {{.id = 2, .watts = 100}},
                   .batteries = {{.id = 3, .capacity = 5000, .charge = 5 * kStep, .rate = 1000}}};

  const Balance balance = Solve(grid, kStep);

  EXPECT_EQ(Satisfied(balance), std::vector<bool> {false}) << "it lasts the step at 5 W only";
  EXPECT_EQ(balance.charges, std::vector<int64_t> {5 * kStep});
}

TEST(BalanceTest, ZeroTicksLeavesTheChargesAlone) {
  const Grid grid {.producers = {{.id = 1, .watts = 500}},
                   .consumers = {{.id = 2, .watts = 100}},
                   .batteries = {{.id = 3, .capacity = 1000, .charge = 10, .rate = 100}}};

  const Balance balance = Solve(grid, 0);

  EXPECT_EQ(balance.charges, std::vector<int64_t> {10});
  EXPECT_EQ(Satisfied(balance), std::vector<bool> {true});
}

TEST(PortNamesTest, PrioritiesAreServedLifeSupportFirstAndAnUnknownNameHasNone) {
  EXPECT_LT(PriorityOf("life_support"), PriorityOf("doors"));
  EXPECT_LT(PriorityOf("doors"), PriorityOf("gravity"));
  EXPECT_LT(PriorityOf("gravity"), PriorityOf("lights"));
  EXPECT_LT(PriorityOf("lights"), PriorityOf("other"));
  EXPECT_EQ(PriorityOf("other"), std::optional<uint8_t> {kLowestPriority});
  EXPECT_EQ(PriorityOf("whim"), std::nullopt);
}

TEST(PortNamesTest, APortNeedsItsRolesValuesAndAKnownPriority) {
  using z13::building::primitives::PortSpec;
  const PortSpec producer {.network = "power", .role = "producer", .values = {{.name = "output", .value = 1}}};
  EXPECT_EQ(ValidatePort(producer), std::nullopt);
  EXPECT_TRUE(ValidatePort({.network = "power", .role = "producer"}).has_value());
  EXPECT_TRUE(ValidatePort({.network = "power", .role = "generator"}).has_value());
  EXPECT_EQ(ValidatePort({.network = "power", .role = "conductor"}), std::nullopt);

  const PortSpec consumer {.network = "power",
                           .role = "consumer",
                           .priority = "doors",
                           .values = {{.name = "demand", .value = 1}}};
  EXPECT_EQ(ValidatePort(consumer), std::nullopt);
  PortSpec unknown = consumer;
  unknown.priority = "whim";
  EXPECT_TRUE(ValidatePort(unknown).has_value());

  const PortSpec storage {.network = "power",
                          .role = "storage",
                          .values = {{.name = "capacity", .value = 1}, {.name = "rate", .value = 1}}};
  EXPECT_EQ(ValidatePort(storage), std::nullopt);
  EXPECT_TRUE(ValidatePort({.network = "power", .role = "storage", .values = {{.name = "capacity", .value = 1}}})
                  .has_value());
}

TEST(PortNamesTest, EveryPowerPortOfTheShippedPaletteIsValid) {
  const auto palette = z13::testing::ShippedPalette();
  ASSERT_TRUE(palette.has_value()) << palette.error();

  size_t ports = 0;
  for (const auto& primitive : palette->primitives) {
    for (const auto& port : primitive.ports) {
      EXPECT_EQ(ValidatePort(port), std::nullopt) << primitive.name;
      ++ports;
    }
  }
  EXPECT_GT(ports, 0u);
}

}  // namespace
}  // namespace z13::utility::power
