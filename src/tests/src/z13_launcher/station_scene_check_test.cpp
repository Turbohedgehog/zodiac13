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
#include <string>
#include <string_view>
#include <vector>

#include <lib_core/settings/config.h>
#include <z13_launcher/station_scene_check.h>
#include <z13_tests/shipped_station.h>

#include "../../../lib_z13/modules/z13_module/tests/support/z13_test_world.h"

namespace z13 {
namespace {

using z13::testing::kSiteScene;
using z13::testing::kStationArg;
using z13::testing::kStationSceneArg;
using z13::testing::kTestScene;

const std::filesystem::path kSourceAssets = z13::testing::SourceAsset({});


// argv[0] is the (unused) program name boost::program_options expects.
Config Parsed(const std::vector<std::string_view>& args) {
  std::vector<std::string> owned {"z13_test"};
  owned.insert(owned.end(), args.begin(), args.end());
  std::vector<char*> argv;
  for (auto& arg : owned) {
    argv.push_back(arg.data());
  }
  Config config;
  EXPECT_TRUE(config.ParseCommandLineArguments(static_cast<int>(argv.size()), argv.data()).has_value());
  return config;
}

TEST(StationSceneCheckTest, AShippedBlueprintPasses) {
  EXPECT_TRUE(CheckStationScene(Parsed({kStationSceneArg, kTestScene}), kSourceAssets).has_value());
  EXPECT_TRUE(CheckStationScene(Parsed({kStationSceneArg, kSiteScene}), kSourceAssets).has_value());
}

TEST(StationSceneCheckTest, NoSceneMeansAnEmptyStation) {
  EXPECT_TRUE(CheckStationScene(Parsed({kStationArg}), kSourceAssets).has_value());
}

TEST(StationSceneCheckTest, AMissingBlueprintStopsTheLaunch) {
  const Status checked = CheckStationScene(Parsed({kStationSceneArg, "moon_base"}), kSourceAssets);

  ASSERT_FALSE(checked.has_value());
  EXPECT_NE(checked.error().find("moon_base"), std::string::npos);
}

}  // namespace
}  // namespace z13
