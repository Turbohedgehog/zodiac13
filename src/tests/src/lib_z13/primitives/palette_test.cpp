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

#include <format>
#include <optional>
#include <string>
#include <string_view>

#include <primitives_generated.h>

#include <primitives/palette.h>
#include <primitives/station_assets.h>
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

TEST(PaletteTest, EveryTextureOfTheShippedPaletteHasAFile) {
  const auto palette = z13::testing::ShippedPalette();
  ASSERT_TRUE(palette.has_value()) << palette.error();

  const z13::AssetSource assets = z13::testing::SourceAssets();
  for (const Primitive& primitive : palette->primitives) {
    if (primitive.material.texture) {
      EXPECT_TRUE(assets.ReadAsset(TextureFile(*primitive.material.texture)).has_value())
          << primitive.name << ": no texture " << *primitive.material.texture;
    }
  }
}

TEST(PaletteTest, ATextureNamesTheTileOfAMaterial) {
  const auto palette = ParsePalette(R"({"primitives": [{"id": 1, "name": "Wall", "shape_type": "Box", "shape": {},
      "min_size": {"x": 1, "y": 1, "z": 1}, "max_size": {"x": 4, "y": 1, "z": 4},
      "material": {"first": {"r": 1, "g": 2, "b": 3, "a": 255}, "second": {"r": 4, "g": 5, "b": 6, "a": 255},
                   "texture": "wall_maint"}, "flags": "Solid"}]})");
  ASSERT_TRUE(palette.has_value()) << palette.error();
  EXPECT_EQ(palette->Find(1)->get().material.texture, std::optional<std::string> {"wall_maint"});
}

TEST(PaletteTest, AShortNameComesFromThePaletteOrTheNamesCapitals) {
  const auto palette = ParsePalette(Palette(std::format("{}, {}", Entry(1, "CornerSlope"), Entry(2, "Wall"))));
  ASSERT_TRUE(palette.has_value()) << palette.error();
  EXPECT_EQ(palette->primitives[0].short_name, "CS");
  EXPECT_EQ(palette->primitives[1].short_name, "Wa");

  const auto shipped = z13::testing::ShippedPalette();
  ASSERT_TRUE(shipped.has_value()) << shipped.error();
  EXPECT_TRUE(std::ranges::none_of(shipped->primitives, [](const Primitive& p) { return p.short_name.empty(); }));
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

std::string Glass(std::string_view extra) {
  return std::format(
      R"({{"id": 1, "name": "Window", "shape_type": "Box", "shape": {{}}, "min_size": {{"x": 1, "y": 1, "z": 1}},
           "max_size": {{"x": 4, "y": 1, "z": 4}},
           "material": {{"first": {{"r": 9, "g": 9, "b": 9, "a": 90}}, "second": {{"r": 9, "g": 9, "b": 9, "a": 90}}}},
           "flags": "Solid Transparent"{}}})",
      extra);
}

TEST(PaletteTest, GlassPassesAllLightUnlessItSaysOtherwise) {
  const auto plain = ParsePalette(Palette(Glass("")));
  const auto tinted = ParsePalette(Palette(Glass(R"(, "light_transmission": 0.1)")));
  ASSERT_TRUE(plain.has_value()) << plain.error();
  ASSERT_TRUE(tinted.has_value()) << tinted.error();

  EXPECT_FLOAT_EQ(plain->primitives.front().light_transmission, 1.f);
  EXPECT_FLOAT_EQ(tinted->primitives.front().light_transmission, 0.1f);
}

TEST(PaletteTest, RejectsALightTransmissionOutsideZeroToOne) {
  EXPECT_FALSE(ParsePalette(Palette(Glass(R"(, "light_transmission": -0.5)"))).has_value());
  EXPECT_FALSE(ParsePalette(Palette(Glass(R"(, "light_transmission": 1.5)"))).has_value());
}

TEST(PaletteTest, ShippedGlassLetsThroughOnlyAShareOfLight) {
  const auto palette = z13::testing::ShippedPalette();
  ASSERT_TRUE(palette.has_value()) << palette.error();

  for (const Primitive& primitive : palette->primitives) {
    if (primitive.Has(PrimitiveFlags::Transparent)) {
      EXPECT_LT(primitive.light_transmission, 1.f) << primitive.name;
    }
  }
}

// A switch-sized box with `interaction`, an Interaction in JSON.
std::string Interactive(std::string_view interaction) {
  return std::format(
      R"({{"id": 1, "name": "Switch", "shape_type": "Box", "shape": {{}}, "min_size": {{"x": 1, "y": 1, "z": 2}},
           "max_size": {{"x": 1, "y": 1, "z": 2}},
           "material": {{"first": {{"r": 9, "g": 9, "b": 9, "a": 255}}, "second": {{"r": 9, "g": 9, "b": 9, "a": 255}}}},
           "flags": "Solid Switch", "interaction": {}}})",
      interaction);
}

TEST(PaletteTest, ParsesHowABlockIsInteractedWith) {
  const auto palette = ParsePalette(Palette(Interactive(R"({"methods": "Hold Presence", "sensor_cells": 8})")));
  ASSERT_TRUE(palette.has_value()) << palette.error();

  const Primitive& primitive = palette->primitives.front();
  EXPECT_TRUE(primitive.Has(PrimitiveFlags::Switch));
  ASSERT_TRUE(primitive.interaction.has_value());
  EXPECT_FALSE(primitive.interaction->Accepts(InteractionMethods::Use));
  EXPECT_TRUE(primitive.interaction->Accepts(InteractionMethods::Hold));
  EXPECT_TRUE(primitive.interaction->Accepts(InteractionMethods::Presence));
  EXPECT_EQ(primitive.interaction->sensor_cells, 8);
  EXPECT_FALSE(ParsePalette(Palette(Entry(1, "Wall")))->primitives.front().interaction.has_value());
}

TEST(PaletteTest, RejectsAnInteractionWithoutAMethodOrWithAStraySensor) {
  EXPECT_FALSE(ParsePalette(Palette(Interactive(R"({})"))).has_value());
  EXPECT_FALSE(ParsePalette(Palette(Interactive(R"({"methods": "Presence"})"))).has_value());
  EXPECT_FALSE(ParsePalette(Palette(Interactive(R"({"methods": "Use", "sensor_cells": 4})"))).has_value());
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

std::string WithPorts(uint32_t id, std::string_view name, std::string_view ports) {
  std::string entry = Entry(id, name);
  entry.insert(entry.rfind('}'), std::format(R"(, "ports": {})", ports));
  return entry;
}

constexpr std::string_view kPowerPort = R"([{"network": "power", "role": "consumer", "priority": "doors",
                                            "values": [{"name": "demand", "value": 50}]}])";

TEST(PaletteTest, ParsesAPrimitivesPorts) {
  const auto palette = ParsePalette(Palette(WithPorts(1, "Lamp", kPowerPort)));
  ASSERT_TRUE(palette.has_value()) << palette.error();

  EXPECT_EQ(palette->primitives.front().ports,
            (std::vector<PortSpec> {{.network = "power",
                                .role = "consumer",
                                .priority = "doors",
                                .values = {{.name = "demand", .value = 50}}}}));
  EXPECT_EQ(palette->primitives.front().ports.front().Value("demand"), std::optional<int64_t> {50});
  EXPECT_EQ(palette->primitives.front().ports.front().Value("capacity"), std::nullopt);
  EXPECT_TRUE(ParsePalette(Palette(Entry(1, "Wall")))->primitives.front().ports.empty());
}

TEST(PaletteTest, RejectsAPortWithoutANetworkOrRoleAndTwoPortsOnOneNetwork) {
  EXPECT_FALSE(ParsePalette(Palette(WithPorts(1, "A", R"([{"role": "consumer"}])"))).has_value());
  EXPECT_FALSE(ParsePalette(Palette(WithPorts(1, "A", R"([{"network": "power"}])"))).has_value());
  EXPECT_FALSE(ParsePalette(Palette(WithPorts(1, "A", R"([{"network": "power", "role": "consumer"},
                                                         {"network": "power", "role": "producer"}])")))
                   .has_value());
}

TEST(PaletteTest, RejectsAPortValueWithoutANameOrRepeatedOrNegative) {
  const auto with = [](std::string_view values) {
    return ParsePalette(Palette(WithPorts(
        1, "A", std::format(R"([{{"network": "power", "role": "consumer", "values": {}}}])", values))));
  };

  EXPECT_TRUE(with(R"([{"name": "demand", "value": 5}])").has_value());
  EXPECT_FALSE(with(R"([{"value": 5}])").has_value());
  EXPECT_FALSE(with(R"([{"name": "demand", "value": 5}, {"name": "demand", "value": 6}])").has_value());
  EXPECT_FALSE(with(R"([{"name": "demand", "value": -1}])").has_value());
}

const std::string kBase = Palette(Entry(1, "Wall"));

PaletteFragment Fragment(std::string_view file, std::string_view entries) {
  return {.file = std::string(file), .json = Palette(entries)};
}

TEST(PaletteTest, FragmentsFollowTheBasesPrimitives) {
  const std::vector fragments = {Fragment("power.json", WithPorts(10, "Battery", kPowerPort)),
                                 Fragment("gas.json", Entry(20, "Vent"))};
  const auto palette = ParsePalette(kBase, fragments);
  ASSERT_TRUE(palette.has_value()) << palette.error();

  ASSERT_EQ(palette->primitives.size(), 3u);
  EXPECT_EQ(palette->primitives[0].name, "Wall");
  EXPECT_EQ(palette->primitives[1].name, "Battery");
  EXPECT_EQ(palette->primitives[2].name, "Vent");
  EXPECT_EQ(palette->Find(10)->get().ports.size(), 1u);
}

TEST(PaletteTest, TheHashCoversTheFragments) {
  const std::vector power = {Fragment("power.json", Entry(10, "Battery"))};
  const std::vector gas = {Fragment("gas.json", Entry(20, "Vent"))};
  const auto bare = ParsePalette(kBase);
  ASSERT_TRUE(bare.has_value()) << bare.error();

  EXPECT_NE(ParsePalette(kBase, power)->hash, bare->hash);
  EXPECT_NE(ParsePalette(kBase, power)->hash, ParsePalette(kBase, gas)->hash);
  EXPECT_EQ(ParsePalette(kBase, power)->hash, ParsePalette(kBase, power)->hash);
}

TEST(PaletteTest, AFragmentsDuplicateIdOrNameIsRefusedAndNamed) {
  const std::vector same_id = {Fragment("power.json", Entry(1, "Battery"))};
  const auto by_id = ParsePalette(kBase, same_id);
  ASSERT_FALSE(by_id.has_value());
  EXPECT_NE(by_id.error().find("power.json"), std::string::npos) << by_id.error();

  const std::vector same_name = {Fragment("gas.json", Entry(2, "Wall"))};
  EXPECT_FALSE(ParsePalette(kBase, same_name).has_value());

  const std::vector clash = {Fragment("a.json", Entry(10, "A")), Fragment("b.json", Entry(10, "B"))};
  const auto between = ParsePalette(kBase, clash);
  ASSERT_FALSE(between.has_value());
  EXPECT_NE(between.error().find("b.json"), std::string::npos) << between.error();
}

TEST(PaletteTest, AnUnparsableFragmentIsRefusedAndNamed) {
  const std::vector broken = {PaletteFragment {.file = "power.json", .json = "{"}};
  const auto palette = ParsePalette(kBase, broken);
  ASSERT_FALSE(palette.has_value());
  EXPECT_NE(palette.error().find("power.json"), std::string::npos) << palette.error();
}

}  // namespace
}  // namespace z13::building::primitives
