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

#include <filesystem>
#include <fstream>
#include <sstream>

#include <z13_primitives/palette.h>

#include "../../z13_module/tests/support/z13_test_world.h"

namespace z13::station {
namespace {

using z13::primitives::BlockPalette;

const std::filesystem::path kShippedPalettePath = std::filesystem::path(Z13_SOURCE_ASSETS_DIR) / "station" / "palette.json";

TEST(PaletteSystemTest, WorldGetsTheShippedPalette) {
  std::ifstream stream(kShippedPalettePath);
  std::ostringstream shipped;
  shipped << stream.rdbuf();
  const auto expected = z13::primitives::ParsePalette(shipped.str());
  ASSERT_TRUE(expected.has_value()) << expected.error();

  z13::testing::Z13TestWorld test_world;

  ASSERT_TRUE(test_world.World().has<BlockPalette>());
  EXPECT_EQ(test_world.World().get<BlockPalette>().palette.hash, expected->hash);
}

}  // namespace
}  // namespace z13::station
