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

#include <string>
#include <vector>

#include <settings_generated.h>

#include <station_graph/property_schema.h>
#include <z13_tests/shipped_station.h>

namespace z13::editor::graph {
namespace {

constexpr std::string_view kHeight = "height";
constexpr std::string_view kKind = "kind";
constexpr int kDefaultHeight = 14;
constexpr int kAboveMaxHeight = 41;
constexpr int kBelowMinHeight = 11;
constexpr int kMinHeight = 12;
constexpr int kMaxHeight = 40;

PropertySchema Shipped() {
  const auto schema = z13::testing::ShippedPropertySchema();
  EXPECT_TRUE(schema) << schema.error();
  return schema.value_or(PropertySchema {});
}

TEST(PropertySchemaTest, TheShippedSchemaDescribesNodesAndEdges) {
  const PropertySchema schema = Shipped();

  ASSERT_TRUE(schema.node.contains(std::string(kHeight)));
  const PropertySpec& height = schema.node.at(std::string(kHeight));
  EXPECT_EQ(height.kind, PropertyKind::kInt);
  EXPECT_EQ(std::get<int>(height.default_value), kDefaultHeight);
  ASSERT_TRUE(schema.edge.contains(std::string(kKind)));
  EXPECT_EQ(schema.edge.at(std::string(kKind)).kind, PropertyKind::kEnum);
}

TEST(PropertySchemaTest, ChecksTheKindTheRangeAndTheValues) {
  const PropertySchema schema = Shipped();
  const PropertySpec& height = schema.node.at(std::string(kHeight));
  const PropertySpec& kind = schema.edge.at(std::string(kKind));

  EXPECT_TRUE(CheckProperty(height, kDefaultHeight));
  EXPECT_FALSE(CheckProperty(height, kAboveMaxHeight));
  EXPECT_FALSE(CheckProperty(height, kBelowMinHeight));
  EXPECT_FALSE(CheckProperty(height, true));
  EXPECT_TRUE(CheckProperty(kind, std::string("Passage")));
  EXPECT_FALSE(CheckProperty(kind, std::string("Hatch")));
}

TEST(PropertySchemaTest, ReadsAValueFromTextByTheKindOfTheSpec) {
  const PropertySchema schema = Shipped();
  const PropertySpec& height = schema.node.at(std::string(kHeight));
  const PropertySpec& kind = schema.edge.at(std::string(kKind));
  const PropertySpec& lamp = schema.node.at("lamp");

  EXPECT_EQ(std::get<int>(ParsePropertyValue(height, "20").value()), 20);
  EXPECT_FALSE(ParsePropertyValue(height, "20m"));
  EXPECT_FALSE(ParsePropertyValue(height, std::to_string(kAboveMaxHeight)));
  EXPECT_FALSE(ParsePropertyValue(lamp, "yes"));
  EXPECT_FALSE(std::get<bool>(ParsePropertyValue(lamp, "false").value()));
  EXPECT_EQ(std::get<std::string>(ParsePropertyValue(kind, "Window").value()), "Window");
  EXPECT_FALSE(ParsePropertyValue(kind, "Hatch"));
}

TEST(PropertySchemaTest, UnknownNamesAreErrorsAndDefaultsFillTheRest) {
  const PropertySchema schema = Shipped();

  const auto unknown = CheckProperties(schema.node, {{"colour", std::string("red")}});
  ASSERT_FALSE(unknown);
  EXPECT_NE(unknown.error().find("colour"), std::string::npos);

  const PropertyMap values = WithDefaults(schema.node, {{std::string(kHeight), 20}});
  EXPECT_EQ(values.size(), schema.node.size());
  EXPECT_EQ(std::get<int>(values.at(std::string(kHeight))), 20);
}

TEST(PropertySchemaTest, TheCaptionsUnitsAndEnumNamesComeFromTheSchema) {
  const PropertySchema schema = Shipped();

  const PropertySpec& height = schema.node.at(std::string(kHeight));
  EXPECT_EQ(height.title, "Room height");
  EXPECT_EQ(height.unit, "cells");
  EXPECT_EQ(height.min, kMinHeight);
  EXPECT_EQ(height.max, kMaxHeight);
  EXPECT_EQ(schema.edge.at(std::string(kKind)).values, (std::vector<std::string> {"Door", "Passage", "Window"}));
  EXPECT_EQ(std::get<std::string>(schema.edge.at(std::string(kKind)).default_value), "Door");
  EXPECT_FALSE(std::get<bool>(schema.node.at("spawn").default_value));
}

TEST(PropertySchemaTest, ASchemaWithoutTheTablesIsRefused) {
  EXPECT_FALSE(PropertySchemaOf(*reflection::GetSchema(z13::fbs::settings::SettingsBinarySchema::data())));
}

}  // namespace
}  // namespace z13::editor::graph
