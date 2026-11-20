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

#include <string>
#include <vector>

#include <station_graph/graph_decks.h>
#include <z13_tests/shipped_station.h>

namespace z13::editor::graph {
namespace {

constexpr int kRoomCells = 12;
constexpr int kUpperLevel = 20;

Node RoomAt(std::string id, int x, int level) {
  return {.id = std::move(id), .box = {.origin = {x, 0, level}, .size = {kRoomCells, kRoomCells}}};
}

PropertySchema Schema() {
  const auto schema = z13::testing::ShippedPropertySchema();
  EXPECT_TRUE(schema) << schema.error();
  return schema.value_or(PropertySchema {});
}

TEST(GraphDecksTest, EachLevelHasOneCutAndTheCutsAscend) {
  const Graph graph {.nodes = {RoomAt("upper", 0, kUpperLevel), RoomAt("a", 0, 0), RoomAt("b", kRoomCells, 0)}};

  const std::vector<float> cuts = DeckCuts(graph, Schema());

  ASSERT_EQ(cuts.size(), 2U);
  EXPECT_LT(cuts[0], cuts[1]);
  EXPECT_GT(cuts[0], 0.f);
  EXPECT_GT(cuts[1], static_cast<float>(kUpperLevel));
}

TEST(GraphDecksTest, SteppingWalksTheDecksAndPastTheHighestOneTheViewIsWhole) {
  const std::vector<float> cuts = {10.f, 30.f};

  const auto first = SteppedCut(cuts, std::nullopt, -1);
  ASSERT_TRUE(first);
  EXPECT_FLOAT_EQ(*first, 30.f);
  EXPECT_FLOAT_EQ(*SteppedCut(cuts, first, -1), 10.f);
  EXPECT_FLOAT_EQ(*SteppedCut(cuts, 10.f, -1), 10.f);
  EXPECT_FLOAT_EQ(*SteppedCut(cuts, 10.f, 1), 30.f);
  EXPECT_FALSE(SteppedCut(cuts, 30.f, 1));
  EXPECT_FALSE(SteppedCut(cuts, std::nullopt, 1));
}

TEST(GraphDecksTest, ACutThatIsNoLongerADeckCountsAsTheDeckAboveIt) {
  const std::vector<float> cuts = {10.f, 30.f};

  EXPECT_FALSE(SteppedCut(cuts, 20.f, 1));
  EXPECT_FLOAT_EQ(*SteppedCut(cuts, 20.f, -1), 10.f);
}

TEST(GraphDecksTest, WithoutDecksThereIsNothingToCut) {
  EXPECT_FALSE(SteppedCut({}, std::nullopt, -1));
}

}  // namespace
}  // namespace z13::editor::graph
