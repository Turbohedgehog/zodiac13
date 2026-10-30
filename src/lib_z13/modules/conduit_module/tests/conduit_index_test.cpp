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

#include <Eigen/Dense>

#include <z13_tests/shipped_station.h>

#include "../src/conduit_index.h"
#include "../src/conduit_kinds.h"
#include "../src/conduit_run.h"

namespace z13::conduit {
namespace {

constexpr uint32_t kCable = 1;
constexpr ConduitId kFirst = 10;
constexpr ConduitId kSecond = 11;

TEST(ConduitRunTest, ARunGoesAlongTheAxisItsEndsAreFurthestApartOn) {
  const Conduit run = RunBetween(kCable, {4, 1, 0}, {1, 2, 0});

  EXPECT_EQ(run.axis, ConduitAxis::kX);
  EXPECT_EQ(run.cell, Eigen::Vector3i(1, 1, 0));
  EXPECT_EQ(run.length, 4);
  EXPECT_EQ(CellsOf(run).back(), Eigen::Vector3i(4, 1, 0));
}

TEST(ConduitIndexTest, ACellHoldsOneConduitOfEachNetwork) {
  ConduitIndex index;
  index.Insert(kFirst, "power", RunBetween(kCable, {0, 0, 0}, {0, 0, 5}));

  EXPECT_TRUE(index.Overlaps("power", RunBetween(kCable, {-2, 0, 3}, {2, 0, 3})));
  EXPECT_FALSE(index.Overlaps("gas", RunBetween(kCable, {-2, 0, 3}, {2, 0, 3})));
  EXPECT_FALSE(index.Overlaps("power", RunBetween(kCable, {1, 0, 0}, {1, 0, 5})));
}

TEST(ConduitIndexTest, ARayFindsTheFirstConduitPreferringTheToolsNetwork) {
  ConduitIndex index;
  const Conduit far = RunBetween(kCable, {6, 0, 0}, {6, 0, 0});
  index.Insert(kFirst, "power", RunBetween(kCable, {3, 0, 0}, {3, 0, 0}));
  index.Insert(kSecond, "gas", RunBetween(kCable, {3, 0, 0}, {3, 0, 0}));
  index.Insert(kSecond + 1, "power", far);
  const Eigen::Vector3f from {0.5f, 0.5f, 0.5f};
  const Eigen::Vector3f to {9.5f, 0.5f, 0.5f};

  EXPECT_EQ(index.Raycast(from, to, "power"), kFirst);
  // Without the tool's network the first network by name wins, whatever the ids.
  EXPECT_EQ(index.Raycast(from, to, "data"), kSecond);
  index.Erase(kFirst, RunBetween(kCable, {3, 0, 0}, {3, 0, 0}));
  index.Erase(kSecond, RunBetween(kCable, {3, 0, 0}, {3, 0, 0}));
  EXPECT_EQ(index.Raycast(from, to, "power"), kSecond + 1);
}

TEST(ConduitKindsTest, TheShippedKindsParse) {
  const auto json = z13::testing::SourceAssets().ReadAsset(ConduitKindsFile());
  ASSERT_TRUE(json.has_value()) << json.error();

  const auto kinds = ParseConduitKinds(*json);

  ASSERT_TRUE(kinds.has_value()) << kinds.error();
  EXPECT_TRUE(kinds->Find(kCable).has_value());
}

TEST(ConduitKindsTest, ARepeatedIdOrAKindWithoutANetworkIsRefused) {
  EXPECT_FALSE(ParseConduitKinds(R"({"kinds": [
      {"id": 1, "name": "A", "network": "power", "color": [1, 2, 3, 4]},
      {"id": 1, "name": "B", "network": "gas", "color": [1, 2, 3, 4]}]})").has_value());
  EXPECT_FALSE(ParseConduitKinds(R"({"kinds": [{"id": 1, "name": "A", "color": [1, 2, 3, 4]}]})").has_value());
}

}  // namespace
}  // namespace z13::conduit
