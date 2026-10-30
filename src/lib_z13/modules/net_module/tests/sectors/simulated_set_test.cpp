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

#include <cstdint>
#include <unordered_set>
#include <vector>

#include "../../src/simulated_set.h"

namespace z13::net {
namespace {

constexpr SimulatedBudget kBudget {.max_players = 2, .slack = 1, .radius_m = 10., .hysteresis_m = 2.};

using Players = std::unordered_set<uint32_t>;

TEST(SimulatedSetTest, TheNearestWithinTheRadiusAreChosen) {
  const std::vector<SimulatedCandidate> candidates = {
      {.player_id = 1, .distance_m = 8.}, {.player_id = 2, .distance_m = 3.}, {.player_id = 3, .distance_m = 5.},
      {.player_id = 4, .distance_m = 11.}};

  EXPECT_EQ(ChooseSimulated(candidates, {}, kBudget), (Players {2, 3}));
}

TEST(SimulatedSetTest, EqualDistancesAreRankedById) {
  const std::vector<SimulatedCandidate> candidates = {
      {.player_id = 9, .distance_m = 4.}, {.player_id = 5, .distance_m = 4.}, {.player_id = 7, .distance_m = 4.}};

  EXPECT_EQ(ChooseSimulated(candidates, {}, kBudget), (Players {5, 7}));
}

TEST(SimulatedSetTest, ASimulatedPlayerStaysWhileItRanksWithinTheSlack) {
  const std::vector<SimulatedCandidate> candidates = {
      {.player_id = 1, .distance_m = 1.}, {.player_id = 2, .distance_m = 2.}, {.player_id = 3, .distance_m = 3.}};

  // 3 ranks third: within max + slack when simulated already, outside the budget when not.
  EXPECT_EQ(ChooseSimulated(candidates, {1, 2, 3}, kBudget), (Players {1, 2, 3}));
  EXPECT_EQ(ChooseSimulated(candidates, {1, 2}, kBudget), (Players {1, 2}));
}

TEST(SimulatedSetTest, ASimulatedPlayerBeyondTheSlackLeavesAndAnotherTakesItsPlace) {
  const std::vector<SimulatedCandidate> candidates = {
      {.player_id = 1, .distance_m = 1.}, {.player_id = 2, .distance_m = 2.}, {.player_id = 3, .distance_m = 3.},
      {.player_id = 4, .distance_m = 4.}};

  EXPECT_EQ(ChooseSimulated(candidates, {4, 1}, kBudget), (Players {1, 2}));
}

TEST(SimulatedSetTest, ASimulatedPlayerKeepsItsPlaceJustBeyondTheRadius) {
  const std::vector<SimulatedCandidate> candidates = {
      {.player_id = 1, .distance_m = 11.}, {.player_id = 2, .distance_m = 11.}, {.player_id = 3, .distance_m = 13.}};

  EXPECT_EQ(ChooseSimulated(candidates, {1, 3}, kBudget), (Players {1}));
}

TEST(SimulatedSetTest, ANewcomerTakesASimulatedPlayersPlaceOnlyWhenNearerByTheRankHysteresis) {
  constexpr SimulatedBudget kCrowd {.max_players = 2, .radius_m = 10., .rank_hysteresis_m = 1.5};
  const Players current {7, 9};
  const auto with_newcomer_at = [](double distance_m) {
    return std::vector<SimulatedCandidate> {
        {.player_id = 7, .distance_m = 4.}, {.player_id = 9, .distance_m = 4.1},
        {.player_id = 5, .distance_m = distance_m}};
  };

  EXPECT_EQ(ChooseSimulated(with_newcomer_at(3.), current, kCrowd), current);
  EXPECT_EQ(ChooseSimulated(with_newcomer_at(2.), current, kCrowd), (Players {5, 7}));
}

}  // namespace
}  // namespace z13::net
