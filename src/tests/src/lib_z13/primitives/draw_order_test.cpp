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
#include <cstdint>
#include <iterator>
#include <vector>

#include <Eigen/Dense>

#include <primitives/draw_order.h>
#include <z13_tests/shipped_station.h>

namespace z13::building::primitives {
namespace {

using z13::station::Block;

// assets/station/palette.json
constexpr uint32_t kWallId = 2;
constexpr uint32_t kWindowId = 4;
const Eigen::Vector3i kPanel {8, 1, 8};

Block At(uint32_t type_id, const Eigen::Vector3i& cell) {
  return {.spec = {.type_id = type_id, .size = kPanel}, .cell = cell};
}

TEST(DrawOrderTest, WindowsAreTransparentAndWallsAreNot) {
  const Palette palette = z13::testing::ShippedPalette().value();

  EXPECT_TRUE(IsTransparent(At(kWindowId, {}), palette));
  EXPECT_FALSE(IsTransparent(At(kWallId, {}), palette));
}

TEST(DrawOrderTest, WithoutAPaletteEveryBlockIsOpaque) {
  EXPECT_FALSE(IsTransparent(At(kWindowId, {}), std::nullopt));
}

// A cube of side 1 at `x` along the X axis, filed under key `x`.
ChunkBounds ChunkAt(int x) {
  const Eigen::Vector3f min(static_cast<float>(x), 0.f, 0.f);
  return {.key = {x, 0, 0}, .bounds = {min, min + Eigen::Vector3f::Ones()}};
}

std::vector<int> SortedXs(std::vector<ChunkBounds> chunks, const Eigen::Vector3f& eye) {
  SortFarthestFirst(chunks, eye);
  std::vector<int> xs;
  std::ranges::transform(chunks, std::back_inserter(xs), [](const ChunkBounds& chunk) { return chunk.key.x(); });
  return xs;
}

TEST(DrawOrderTest, ChunksGoFarthestFirst) {
  EXPECT_EQ(SortedXs({ChunkAt(10), ChunkAt(30), ChunkAt(20)}, Eigen::Vector3f::Zero()),
            std::vector<int>({30, 20, 10}));
}

TEST(DrawOrderTest, TheOrderFollowsTheEye) {
  EXPECT_EQ(SortedXs({ChunkAt(10), ChunkAt(30)}, Eigen::Vector3f(40.f, 0.f, 0.f)), std::vector<int>({10, 30}));
}

// Chunk keys come from a hash map, whose order a rehash changes.
TEST(DrawOrderTest, EquallyFarChunksGoByKeyWhateverTheirOrder) {
  const Eigen::Vector3f between(20.5f, 0.5f, 0.5f);

  EXPECT_EQ(SortedXs({ChunkAt(30), ChunkAt(10)}, between), std::vector<int>({10, 30}));
  EXPECT_EQ(SortedXs({ChunkAt(10), ChunkAt(30)}, between), std::vector<int>({10, 30}));
}

}  // namespace
}  // namespace z13::building::primitives
