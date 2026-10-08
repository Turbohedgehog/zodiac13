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

#include <cstddef>
#include <cstdint>
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

TEST(DrawOrderTest, OpaqueBlocksComeFirstAndWindowsFarthestFirst) {
  const Palette palette = z13::testing::ShippedPalette().value();
  const std::vector<Block> blocks {
      At(kWindowId, {10, 0, 0}), At(kWallId, {40, 0, 0}), At(kWindowId, {30, 0, 0}), At(kWindowId, {20, 0, 0})};

  const DrawOrder order = SortForDrawing(blocks, palette, Eigen::Vector3f::Zero());

  EXPECT_EQ(order.opaque, std::vector<size_t>({1}));
  EXPECT_EQ(order.transparent, std::vector<size_t>({2, 3, 0}));
}

TEST(DrawOrderTest, TheOrderFollowsTheEye) {
  const Palette palette = z13::testing::ShippedPalette().value();
  const std::vector<Block> blocks {At(kWindowId, {10, 0, 0}), At(kWindowId, {30, 0, 0})};

  EXPECT_EQ(SortForDrawing(blocks, palette, Eigen::Vector3f(40.f, 0.f, 0.f)).transparent, std::vector<size_t>({0, 1}));
}

TEST(DrawOrderTest, WithoutAPaletteEveryBlockIsOpaque) {
  const std::vector<Block> blocks {At(kWindowId, {10, 0, 0}), At(kWallId, {20, 0, 0})};

  const DrawOrder order = SortForDrawing(blocks, std::nullopt, Eigen::Vector3f::Zero());

  EXPECT_EQ(order.opaque, std::vector<size_t>({0, 1}));
  EXPECT_TRUE(order.transparent.empty());
}

}  // namespace
}  // namespace z13::building::primitives
