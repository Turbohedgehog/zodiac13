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
#include <vector>

#include <sectors/interest_map.h>

namespace z13::net::sectors {
namespace {

constexpr ClientId kClient = 3;
constexpr ClientId kOther = 4;
constexpr InterestShape kShape {.radius = 1, .margin = 1};
// The sectors along an axis from the center out to the shape's reach.
constexpr int kReach = 2;
constexpr size_t kCubeOfReach = 5 * 5 * 5;
constexpr size_t kLayerOfReach = 5 * 5;

TEST(InterestMapTest, TheDistanceIsTheLargestDifferenceAlongAnAxis) {
  EXPECT_EQ(SectorDistance({0, 0, 0}, {2, -1, 1}), 2);
  EXPECT_EQ(SectorDistance({3, 3, 3}, {3, 3, 3}), 0);
}

TEST(InterestMapTest, AClientEntersTheCubeAroundItsSector) {
  InterestMap map;

  const InterestChange change = map.Update(kClient, {10, 0, -4}, kShape);

  EXPECT_EQ(change.entered.size(), kCubeOfReach);
  EXPECT_TRUE(change.left.empty());
  EXPECT_EQ(map.SectorCount(kClient), kCubeOfReach);
  EXPECT_TRUE(map.Covers(kClient, {12, 2, -2}));
  EXPECT_FALSE(map.Covers(kClient, {13, 0, -4}));
}

TEST(InterestMapTest, OnlyTheInnerRingIsShown) {
  InterestMap map;
  map.Update(kClient, {0, 0, 0}, kShape);

  EXPECT_TRUE(map.IsInner(kClient, {1, -1, 1}));
  EXPECT_FALSE(map.IsInner(kClient, {2, 0, 0})) << "the margin is simulated, not shown";
  EXPECT_TRUE(map.Covers(kClient, {2, 0, 0}));
}

TEST(InterestMapTest, AnUnchangedClientChangesNothing) {
  InterestMap map;
  map.Update(kClient, {0, 0, 0}, kShape);

  const InterestChange change = map.Update(kClient, {0, 0, 0}, kShape);

  EXPECT_TRUE(change.entered.empty());
  EXPECT_TRUE(change.left.empty());
}

TEST(InterestMapTest, StepsToANeighbourEnterALayerAndKeepTheOldOne) {
  InterestMap map;
  map.Update(kClient, {0, 0, 0}, kShape);

  const InterestChange change = map.Update(kClient, {1, 0, 0}, kShape);

  EXPECT_EQ(change.entered.size(), kLayerOfReach);
  EXPECT_TRUE(change.left.empty()) << "the far layer is held one sector longer";
  EXPECT_TRUE(map.Covers(kClient, {-kReach, 0, 0}));
}

TEST(InterestMapTest, CrossingABorderBackAndForthDoesNotFlicker) {
  InterestMap map;
  map.Update(kClient, {0, 0, 0}, kShape);
  map.Update(kClient, {1, 0, 0}, kShape);

  const InterestChange back = map.Update(kClient, {0, 0, 0}, kShape);

  EXPECT_TRUE(back.entered.empty());
  EXPECT_TRUE(back.left.empty());
}

TEST(InterestMapTest, TwoStepsAwayLeaveTheFarLayer) {
  InterestMap map;
  map.Update(kClient, {0, 0, 0}, kShape);
  map.Update(kClient, {1, 0, 0}, kShape);

  const InterestChange change = map.Update(kClient, {2, 0, 0}, kShape);

  EXPECT_EQ(change.left.size(), kLayerOfReach);
  EXPECT_FALSE(map.Covers(kClient, {-kReach, 0, 0}));
  EXPECT_EQ(map.SectorCount(kClient), kCubeOfReach + kLayerOfReach);
}

TEST(InterestMapTest, ChangesComeInSectorOrder) {
  InterestMap map;

  const InterestChange change = map.Update(kClient, {0, 0, 0}, kShape);

  EXPECT_TRUE(std::ranges::is_sorted(change.entered, [](const SectorKey& a, const SectorKey& b) {
    return std::lexicographical_compare(a.data(), a.data() + 3, b.data(), b.data() + 3);
  }));
}

TEST(InterestMapTest, ASectorListsTheClientsSubscribedToIt) {
  InterestMap map;
  map.Update(kClient, {0, 0, 0}, kShape);
  map.Update(kOther, {2, 0, 0}, kShape);

  const auto both = map.SubscribersOf({1, 0, 0});
  const auto one = map.SubscribersOf({-2, 0, 0});

  EXPECT_EQ(both.size(), 2u);
  EXPECT_EQ(std::vector<ClientId>(one.begin(), one.end()), std::vector {kClient});
  EXPECT_TRUE(map.SubscribersOf({20, 0, 0}).empty());
}

TEST(InterestMapTest, ARemovedClientLeavesEverySector) {
  InterestMap map;
  map.Update(kClient, {0, 0, 0}, kShape);
  map.Update(kOther, {2, 0, 0}, kShape);

  map.Remove(kClient);

  EXPECT_FALSE(map.Covers(kClient, {0, 0, 0}));
  EXPECT_EQ(map.Clients(), std::vector {kOther});
  EXPECT_EQ(map.SubscribersOf({1, 0, 0}).size(), 1u);
  EXPECT_TRUE(map.SubscribersOf({-2, 0, 0}).empty());
}

TEST(InterestMapTest, ASmallerShapeLeavesWhatIsBeyondItsHysteresis) {
  InterestMap map;
  map.Update(kClient, {0, 0, 0}, kShape);

  const InterestChange change = map.Update(kClient, {0, 0, 0}, {.radius = 0, .margin = 0});

  EXPECT_EQ(map.SectorCount(kClient), 27u) << "the sector itself and one ring of hysteresis";
  EXPECT_FALSE(change.left.empty());
  EXPECT_TRUE(map.Covers(kClient, {1, 0, 0}));
  EXPECT_FALSE(map.Covers(kClient, {2, 0, 0}));
}

}  // namespace
}  // namespace z13::net::sectors
