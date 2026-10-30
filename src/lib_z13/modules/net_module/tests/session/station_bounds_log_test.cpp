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

#include <optional>

#include "../../src/station_bounds_log.h"

namespace z13::net {
namespace {

using z13::station::SealedBounds;

const SealedBounds kSmall {.extent = Eigen::Vector3i::Constant(2)};
const SealedBounds kLarge {.extent = Eigen::Vector3i::Constant(4)};

TEST(StationBoundsLogTest, BoundsHoldFromTheirTickUntilTheNext) {
  BoundsByTick log;
  EXPECT_TRUE(SetBounds(log, 10, kSmall));
  EXPECT_FALSE(SetBounds(log, 12, kSmall));
  EXPECT_TRUE(SetBounds(log, 20, kLarge));

  EXPECT_EQ(BoundsAt(log, 9), std::nullopt);
  EXPECT_EQ(BoundsAt(log, 15), kSmall);
  EXPECT_EQ(BoundsAt(log, 25), kLarge);
}

// A replay that keeps the old bounds at tick 20 drops what the first run logged there.
TEST(StationBoundsLogTest, ATickSetAgainReplacesTheLaterEntries) {
  BoundsByTick log;
  SetBounds(log, 10, kSmall);
  SetBounds(log, 20, kLarge);
  SetBounds(log, 30, std::nullopt);

  EXPECT_TRUE(SetBounds(log, 20, kSmall));

  EXPECT_EQ(BoundsAt(log, 35), kSmall);
  EXPECT_EQ(log.size(), 2U);
}

TEST(StationBoundsLogTest, SettlingKeepsTheEntryThatHoldsAtTheTick) {
  BoundsByTick log;
  SetBounds(log, 10, kSmall);
  SetBounds(log, 20, kLarge);
  SetBounds(log, 30, kSmall);

  SettleBounds(log, 25);

  EXPECT_EQ(log.size(), 2U);
  EXPECT_EQ(BoundsAt(log, 25), kLarge);
}

}  // namespace
}  // namespace z13::net
