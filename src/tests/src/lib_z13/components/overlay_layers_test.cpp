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
#include <string>

#include <z13/components/overlay_layers.h>

namespace z13::station {
namespace {

OverlayLayers LayersNamed(std::initializer_list<const char*> names) {
  OverlayLayers layers;
  for (const char* name : names) {
    layers.layers.push_back({.name = name});
  }
  return layers;
}

TEST(OverlayLayersTest, StepGoesOffThenThroughEachLayerThenOff) {
  OverlayLayers layers = LayersNamed({"power", "gas"});

  layers.Step();
  EXPECT_EQ(layers.shown, std::optional<std::string> {"power"});
  layers.Step();
  EXPECT_EQ(layers.shown, std::optional<std::string> {"gas"});
  layers.Step();
  EXPECT_EQ(layers.shown, std::nullopt);
}

TEST(OverlayLayersTest, StepWithoutLayersStaysOff) {
  OverlayLayers layers;

  layers.Step();

  EXPECT_EQ(layers.shown, std::nullopt);
}

TEST(OverlayLayersTest, AShownLayerThatWentAwayStepsToOff) {
  OverlayLayers layers = LayersNamed({"power"});
  layers.shown = "gone";

  layers.Step();

  EXPECT_EQ(layers.shown, std::nullopt);
}

}  // namespace
}  // namespace z13::station
