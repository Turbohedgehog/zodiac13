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

#include <set>
#include <string>

#include <primitives/decor.h>
#include <primitives/station_assets.h>
#include <z13_tests/shipped_station.h>

namespace z13::building::primitives {
namespace {

TEST(DecorTest, ParsesPropsWithTheirPose) {
  const auto props = ParseDecor(R"({"props": [{"model": "chair", "position": {"x": 1, "y": 2, "z": 3}, "yaw": 1.5}]})");
  ASSERT_TRUE(props.has_value()) << props.error();
  ASSERT_EQ(props->size(), 1U);
  EXPECT_EQ((*props)[0].model, "chair");
  EXPECT_EQ((*props)[0].position, Eigen::Vector3f(1.F, 2.F, 3.F));
  EXPECT_FLOAT_EQ((*props)[0].yaw, 1.5F);
}

TEST(DecorTest, APropWithoutAModelIsRejected) {
  EXPECT_FALSE(ParseDecor(R"({"props": [{"position": {"x": 0, "y": 0, "z": 0}}]})").has_value());
}

TEST(DecorTest, EveryShippedStationsPropsHaveAModelFile) {
  const z13::AssetSource assets = z13::testing::SourceAssets();
  for (const std::string& scene : z13::testing::ShippedScenes()) {
    const auto json = assets.ReadAsset(DecorFile(scene));
    if (!json) {
      continue;
    }
    const auto props = ParseDecor(*json);
    ASSERT_TRUE(props.has_value()) << scene << ": " << props.error();
    std::set<std::string> models;
    for (const Prop& prop : *props) {
      models.insert(prop.model);
    }
    for (const std::string& model : models) {
      EXPECT_TRUE(assets.ReadAsset(PropModelFile(model)).has_value()) << scene << ": no model " << model;
    }
  }
}

}  // namespace
}  // namespace z13::building::primitives
