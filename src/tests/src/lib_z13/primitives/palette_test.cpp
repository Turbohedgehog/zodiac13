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

#include <format>
#include <string>
#include <string_view>

#include <primitives/palette.h>
#include <z13_tests/shipped_station.h>

namespace z13::building::primitives {
namespace {

// A palette of the given comma-separated entries.
std::string Palette(std::string_view entries) {
  return std::format(R"({{"primitives": [{}]}})", entries);
}

// One primitive; the defaults make a valid stretchable box.
std::string Entry(uint32_t id, std::string_view name, std::string_view min = R"({"x": 1, "y": 1, "z": 1})",
                  std::string_view max = R"({"x": 4, "y": 1, "z": 4})", std::string_view shape = R"("Box", "shape": {})") {
  return std::format(
      R"({{"id": {}, "name": "{}", "shape_type": {}, "min_size": {}, "max_size": {},
           "material": {{"first": {{"r": 1, "g": 2, "b": 3, "a": 255}}, "second": {{"r": 4, "g": 5, "b": 6, "a": 255}}}},
           "flags": "Solid"}})",
      id, name, shape, min, max);
}

TEST(PaletteTest, ShippedPaletteParses) {
  const auto palette = z13::testing::ShippedPalette();
  ASSERT_TRUE(palette.has_value()) << palette.error();

  EXPECT_FALSE(palette->primitives.empty());
  const auto door = palette->Find(3);
  ASSERT_TRUE(door.has_value());
  EXPECT_EQ(door->get().shape.kind, ShapeKind::kDoorFrame);
  EXPECT_TRUE(door->get().Has(PrimitiveFlags::Solid));
  EXPECT_FALSE(palette->Find(0).has_value());
}

TEST(PaletteTest, ParsesSizesFlagsAndColors) {
  const auto palette = ParsePalette(Palette(Entry(7, "Wall")));
  ASSERT_TRUE(palette.has_value()) << palette.error();

  const Primitive& wall = palette->primitives.front();
  EXPECT_EQ(wall.id, 7u);
  EXPECT_EQ(wall.max_size, Eigen::Vector3i(4, 1, 4));
  EXPECT_EQ(wall.material.second, (Rgba {4, 5, 6, 255}));
  EXPECT_TRUE(wall.Has(PrimitiveFlags::Solid));
  EXPECT_FALSE(wall.Has(PrimitiveFlags::GasSealing));
}

// A glowing box with `light`, a LightSource in JSON.
std::string Lamp(std::string_view light) {
  return std::format(
      R"({{"id": 1, "name": "Lamp", "shape_type": "Box", "shape": {{}}, "min_size": {{"x": 2, "y": 2, "z": 1}},
           "max_size": {{"x": 2, "y": 2, "z": 1}},
           "material": {{"first": {{"r": 9, "g": 9, "b": 9, "a": 255}}, "second": {{"r": 9, "g": 9, "b": 9, "a": 255}},
                         "emissive": true}},
           "light": {}}})",
      light);
}

TEST(PaletteTest, ParsesALampsLightAndGlow) {
  const auto palette =
      ParsePalette(Palette(Lamp(R"({"color": {"r": 255, "g": 240, "b": 200, "a": 255}, "radius_cells": 40,
                                    "intensity": 1.5})")));
  ASSERT_TRUE(palette.has_value()) << palette.error();

  const Primitive& lamp = palette->primitives.front();
  EXPECT_TRUE(lamp.material.emissive);
  ASSERT_TRUE(lamp.light.has_value());
  EXPECT_EQ(*lamp.light, (LightSource {.color = {255, 240, 200, 255}, .radius_cells = 40, .intensity = 1.5f}));
  EXPECT_FALSE(ParsePalette(Palette(Entry(1, "Wall")))->primitives.front().light.has_value());
}

TEST(PaletteTest, RejectsALightThatReachesNothing) {
  EXPECT_FALSE(ParsePalette(Palette(Lamp(R"({"color": {"r": 1, "g": 1, "b": 1, "a": 255}, "radius_cells": 0})")))
                   .has_value());
  EXPECT_FALSE(ParsePalette(Palette(Lamp(R"({"color": {"r": 1, "g": 1, "b": 1, "a": 255}, "radius_cells": 4,
                                             "intensity": 0})")))
                   .has_value());
  EXPECT_FALSE(ParsePalette(Palette(Lamp(R"({"radius_cells": 4})"))).has_value());
}

TEST(PaletteTest, HashDependsOnContentNotLayout) {
  const std::string entry = Entry(1, "Floor");
  const auto compact = ParsePalette(Palette(entry));
  const auto spaced = ParsePalette(std::format("{{\n  \"primitives\" :\n  [ {} ]\n}}\n", entry));
  const auto renamed = ParsePalette(Palette(Entry(1, "Plate")));
  ASSERT_TRUE(compact && spaced && renamed);

  EXPECT_EQ(compact->hash, spaced->hash);
  EXPECT_NE(compact->hash, renamed->hash);
}

TEST(PaletteTest, RejectsDuplicateIdsAndNames) {
  EXPECT_FALSE(ParsePalette(Palette(std::format("{}, {}", Entry(1, "A"), Entry(1, "B")))).has_value());
  EXPECT_FALSE(ParsePalette(Palette(std::format("{}, {}", Entry(1, "A"), Entry(2, "A")))).has_value());
}

TEST(PaletteTest, RejectsBadSizeLimits) {
  constexpr std::string_view kEmpty = R"({"x": 0, "y": 1, "z": 1})";
  constexpr std::string_view kHuge = R"({"x": 257, "y": 1, "z": 1})";
  constexpr std::string_view kSmall = R"({"x": 1, "y": 1, "z": 1})";
  constexpr std::string_view kLarge = R"({"x": 4, "y": 1, "z": 4})";

  EXPECT_FALSE(ParsePalette(Palette(Entry(1, "A", kEmpty, kLarge))).has_value());
  EXPECT_FALSE(ParsePalette(Palette(Entry(1, "A", kSmall, kHuge))).has_value());
  EXPECT_FALSE(ParsePalette(Palette(Entry(1, "A", kLarge, kSmall))).has_value());
}

TEST(PaletteTest, RejectsADoorThatCannotBeBuilt) {
  constexpr std::string_view kDoorSize = R"({"x": 6, "y": 1, "z": 10})";
  const std::string fits = Entry(1, "Door", kDoorSize, kDoorSize,
                                 R"("DoorFrame", "shape": {"opening_width": 4, "opening_height": 8})");
  const std::string too_wide = Entry(1, "Door", kDoorSize, kDoorSize,
                                     R"("DoorFrame", "shape": {"opening_width": 6, "opening_height": 8})");

  EXPECT_TRUE(ParsePalette(Palette(fits)).has_value());
  EXPECT_FALSE(ParsePalette(Palette(too_wide)).has_value());
}

// Width 7 in a 6..8 range would leave unequal posts.
TEST(PaletteTest, RejectsAStretchableDoor) {
  const std::string stretchable = Entry(1, "Door", R"({"x": 6, "y": 1, "z": 10})", R"({"x": 8, "y": 1, "z": 10})",
                                        R"("DoorFrame", "shape": {"opening_width": 4, "opening_height": 8})");

  EXPECT_FALSE(ParsePalette(Palette(stretchable)).has_value());
}

TEST(PaletteTest, RejectsAShapeTypeWithoutItsShape) {
  constexpr std::string_view kDoorSize = R"({"x": 6, "y": 1, "z": 10})";

  EXPECT_FALSE(ParsePalette(Palette(Entry(1, "Door", kDoorSize, kDoorSize, R"("DoorFrame")"))).has_value());
  EXPECT_FALSE(ParsePalette(Palette(Entry(1, "Box", kDoorSize, kDoorSize, R"("Box")"))).has_value());
}

TEST(PaletteTest, RejectsMalformedPalettes) {
  EXPECT_FALSE(ParsePalette("not json").has_value());
  EXPECT_FALSE(ParsePalette(Palette("")).has_value());
  EXPECT_FALSE(ParsePalette(R"({"primitives": [{"id": 1, "name": "NoShape"}]})").has_value());
  EXPECT_FALSE(ParsePalette(R"({"primitives": [], "unknown_field": 1})").has_value());
}

}  // namespace
}  // namespace z13::building::primitives
