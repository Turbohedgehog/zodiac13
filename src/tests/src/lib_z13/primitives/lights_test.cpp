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
#include <vector>

#include <Eigen/Dense>

#include <primitives/lights.h>
#include <z13_tests/shipped_station.h>

namespace z13::building::primitives {
namespace {

using z13::station::Block;
using z13::station::Orientation;

// assets/station/palette.json
constexpr uint32_t kWallId = 2;
constexpr uint32_t kLampId = 10;
const Eigen::Vector3i kLampSize {2, 2, 1};
const Eigen::Vector3i kLampCell {10, 10, 10};

Block LampAt(Orientation orientation) {
  return {.spec = {.type_id = kLampId, .size = kLampSize, .orientation = orientation}, .cell = kLampCell};
}

TEST(LightsTest, ALampShinesFromBelowItsBottomFace) {
  const Palette palette = z13::testing::ShippedPalette().value();
  const std::vector<Block> blocks {
      LampAt(Orientation::kFacePosXUpPosZ),
      {.spec = {.type_id = kWallId, .size = {4, 1, 4}}, .cell = {0, 0, 0}},
  };

  const std::vector<PointLight> lights = LightsOf(blocks, palette);

  ASSERT_EQ(lights.size(), 1U);
  EXPECT_TRUE(lights[0].position.isApprox(Eigen::Vector3f(11.f, 11.f, 9.5f)));
  EXPECT_EQ(lights[0].cell, Eigen::Vector3i(11, 11, 9));
  EXPECT_EQ(lights[0].source, palette.Find(kLampId)->get().light);
}

TEST(LightsTest, ALampTurnedOverShinesUp) {
  const Palette palette = z13::testing::ShippedPalette().value();

  const std::vector<PointLight> lights = LightsOf(std::vector {LampAt(Orientation::kFacePosXUpNegZ)}, palette);

  ASSERT_EQ(lights.size(), 1U);
  EXPECT_TRUE(lights[0].position.isApprox(Eigen::Vector3f(11.f, 11.f, 11.5f)));
}

TEST(LightsTest, WithoutAPaletteNothingShines) {
  EXPECT_TRUE(LightsOf(std::vector {LampAt(Orientation::kFacePosXUpPosZ)}, std::nullopt).empty());
}

}  // namespace
}  // namespace z13::building::primitives
