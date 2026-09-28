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
#include <cmath>
#include <cstdint>

#include <z13/components/player_color.h>

namespace z13::gameplay {
namespace {

constexpr uint32_t kCheckedIds = 256;
constexpr uint32_t kTypicalSessionSize = 16;
// Far enough apart in RGB to tell two players in the same session apart at a glance.
constexpr float kMinSessionDistance = 0.1f;
constexpr float kMinDistinctDistance = 1e-3f;

float Distance(const Rgb& a, const Rgb& b) {
  return std::hypot(a[0] - b[0], a[1] - b[1], a[2] - b[2]);
}

float MinPairwiseDistance(uint32_t count) {
  float min_distance = 3.f;
  for (uint32_t a = 0; a < count; ++a) {
    for (uint32_t b = a + 1; b < count; ++b) {
      min_distance = std::min(min_distance, Distance(PlayerColor(a), PlayerColor(b)));
    }
  }
  return min_distance;
}

TEST(PlayerColorTest, IsStableAndInRange) {
  for (uint32_t id = 0; id < kCheckedIds; ++id) {
    const Rgb color = PlayerColor(id);
    EXPECT_EQ(color, PlayerColor(id));
    for (const float channel : color) {
      EXPECT_GE(channel, 0.f);
      EXPECT_LE(channel, 1.f);
    }
  }
}

TEST(PlayerColorTest, FirstIdsAreAllDistinct) {
  EXPECT_GT(MinPairwiseDistance(kCheckedIds), kMinDistinctDistance);
}

TEST(PlayerColorTest, PlayersOfOneSessionAreClearlyDistinguishable) {
  EXPECT_GT(MinPairwiseDistance(kTypicalSessionSize), kMinSessionDistance);
}

}  // namespace
}  // namespace z13::gameplay
