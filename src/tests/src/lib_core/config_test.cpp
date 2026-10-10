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
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <lib_core/settings/config.h>
#include <lib_core/utils/status.h>

namespace z13 {
namespace {

constexpr std::string_view kServerArg = "--server";
constexpr std::string_view kConnectArg = "--connect";
constexpr std::string_view kStationArg = "--station";
constexpr std::string_view kStationSceneArg = "--station-scene";
constexpr std::string_view kTestScene = "test";
constexpr std::string_view kRenderTourArg = "--render-tour";
constexpr std::string_view kTourFile = "tour.csv";
constexpr std::string_view kHelpArg = "--help";
constexpr std::string_view kExampleHost = "example.com";
constexpr std::string_view kExampleEndpoint = "example.com:9999";
constexpr std::string_view kExampleEndpointPortZero = "example.com:0";
constexpr std::string_view kEmptyHostEndpoint = ":80";

struct ParsedConfig {
  Config config;
  Status result;
};

// argv[0] is the (unused) program name boost::program_options expects.
ParsedConfig ParseArgs(const std::vector<std::string_view>& args) {
  std::vector<std::string> owned {"z13_test"};
  for (const std::string_view arg : args) {
    owned.emplace_back(arg);
  }
  std::vector<char*> argv;
  for (auto& arg : owned) {
    argv.push_back(arg.data());
  }

  Config config;
  auto result = config.ParseCommandLineArguments(static_cast<int>(argv.size()), argv.data());
  return {std::move(config), std::move(result)};
}

TEST(ConfigTest, DefaultsHaveNoServerNoConnectAndDefaultPort) {
  const auto parsed = ParseArgs({});

  EXPECT_FALSE(parsed.config.IsServer());
  EXPECT_EQ(parsed.config.GetPort(), kDefaultServerPort);
  EXPECT_FALSE(parsed.config.GetConnectEndpoint().has_value());
  EXPECT_TRUE(parsed.result.has_value());
}

TEST(ConfigTest, ServerFlagIsRecognizedWithDefaultPort) {
  const auto parsed = ParseArgs({kServerArg});

  EXPECT_TRUE(parsed.config.IsServer());
  EXPECT_EQ(parsed.config.GetPort(), kDefaultServerPort);
  EXPECT_TRUE(parsed.result.has_value());
}

TEST(ConfigTest, ServerFlagWithPortOverridesDefault) {
  const auto parsed = ParseArgs({kServerArg, "30000"});

  EXPECT_TRUE(parsed.config.IsServer());
  EXPECT_EQ(parsed.config.GetPort(), 30000);
  EXPECT_TRUE(parsed.result.has_value());
}

TEST(ConfigTest, ServerPortZeroIsRejected) {
  EXPECT_FALSE(ParseArgs({kServerArg, "0"}).result.has_value());
}

TEST(ConfigTest, ServerPortAboveRangeIsRejected) {
  EXPECT_FALSE(ParseArgs({kServerArg, "70000"}).result.has_value());
}

TEST(ConfigTest, ServerPortNonNumericIsRejected) {
  EXPECT_FALSE(ParseArgs({kServerArg, "abc"}).result.has_value());
}

TEST(ConfigTest, HelpListsServerAndConnectOptions) {
  const auto parsed = ParseArgs({kHelpArg});
  std::ostringstream out;
  out << parsed.config;
  const std::string text = out.str();

  EXPECT_NE(text.find(kServerArg), std::string::npos);
  EXPECT_NE(text.find(kConnectArg), std::string::npos);
}

TEST(ConfigTest, ConnectWithoutPortUsesDefaultPort) {
  const auto endpoint = ParseArgs({kConnectArg, kExampleHost}).config.GetConnectEndpoint();

  ASSERT_TRUE(endpoint.has_value());
  EXPECT_EQ(endpoint->host, kExampleHost);
  EXPECT_EQ(endpoint->port, kDefaultServerPort);
}

TEST(ConfigTest, ConnectWithPortIsParsed) {
  const auto endpoint = ParseArgs({kConnectArg, kExampleEndpoint}).config.GetConnectEndpoint();

  ASSERT_TRUE(endpoint.has_value());
  EXPECT_EQ(endpoint->host, kExampleHost);
  EXPECT_EQ(endpoint->port, 9999);
}

TEST(ConfigTest, ConnectWithEmptyHostIsRejected) {
  const auto parsed = ParseArgs({kConnectArg, kEmptyHostEndpoint});

  EXPECT_FALSE(parsed.result.has_value());
  EXPECT_FALSE(parsed.config.GetConnectEndpoint().has_value());
}

TEST(ConfigTest, ConnectWithPortZeroIsRejected) {
  EXPECT_FALSE(ParseArgs({kConnectArg, kExampleEndpointPortZero}).result.has_value());
}

TEST(ConfigTest, ServerAndConnectTogetherIsRejected) {
  EXPECT_FALSE(ParseArgs({kServerArg, kConnectArg, kExampleHost}).result.has_value());
}

TEST(ConfigTest, StationFlagIsRecognizedAlone) {
  const auto parsed = ParseArgs({kStationArg});

  EXPECT_TRUE(parsed.result.has_value());
  EXPECT_TRUE(parsed.config.IsStation());
  EXPECT_FALSE(ParseArgs({}).config.IsStation());
}

TEST(ConfigTest, StationWithServerIsAccepted) {
  const auto parsed = ParseArgs({kServerArg, kStationArg});

  EXPECT_TRUE(parsed.result.has_value());
  EXPECT_TRUE(parsed.config.IsServer());
  EXPECT_TRUE(parsed.config.IsStation());
}

TEST(ConfigTest, StationSceneImpliesStation) {
  const auto parsed = ParseArgs({kStationSceneArg, kTestScene});

  EXPECT_TRUE(parsed.result.has_value());
  EXPECT_TRUE(parsed.config.IsStation());
  EXPECT_EQ(parsed.config.GetStationScene(), std::string(kTestScene));
  EXPECT_FALSE(ParseArgs({kStationArg}).config.GetStationScene().has_value());
}

TEST(ConfigTest, RenderTourTakesItsFileAndNeedsAStationScene) {
  const auto parsed = ParseArgs({kStationSceneArg, kTestScene, kRenderTourArg, kTourFile});

  EXPECT_TRUE(parsed.result.has_value());
  EXPECT_EQ(parsed.config.GetRenderTourPath(), std::filesystem::path(kTourFile));
  EXPECT_FALSE(ParseArgs({kStationSceneArg, kTestScene}).config.GetRenderTourPath().has_value());
  EXPECT_FALSE(ParseArgs({kRenderTourArg, kTourFile}).result.has_value());
}

constexpr std::string_view kRenderTourViewArg = "--render-tour-view";
constexpr std::string_view kDockView = "11.64,48.4,13.75,-0.5,12.8";
constexpr std::string_view kOriginView = "0,0,0,90,0";

TEST(ConfigTest, RenderTourViewsAreRepeatableAndNeedATour) {
  const auto parsed = ParseArgs(
      {kStationSceneArg, kTestScene, kRenderTourArg, kTourFile, kRenderTourViewArg, kDockView, kRenderTourViewArg,
       kOriginView});

  ASSERT_TRUE(parsed.result.has_value()) << parsed.result.error();
  const std::vector<CameraPose>& views = parsed.config.GetRenderTourViews();
  ASSERT_EQ(views.size(), 2U);
  EXPECT_TRUE(views[0].eye.isApprox(Eigen::Vector3f(11.64f, 48.4f, 13.75f)));
  EXPECT_FLOAT_EQ(views[0].yaw_deg, -0.5f);
  EXPECT_FLOAT_EQ(views[0].pitch_deg, 12.8f);
  EXPECT_FLOAT_EQ(views[1].yaw_deg, 90.f);
  EXPECT_FALSE(ParseArgs({kStationSceneArg, kTestScene, kRenderTourViewArg, kDockView}).result.has_value());
}

TEST(ConfigTest, ARenderTourViewNeedsFiveNumbers) {
  for (const std::string_view view : {"1,2,3,4", "1,2,3,4,5,6", "1,2,x,4,5", ""}) {
    EXPECT_FALSE(ParseArgs({kStationSceneArg, kTestScene, kRenderTourArg, kTourFile, kRenderTourViewArg, view})
                     .result.has_value())
        << view;
  }
}

// A client takes the mode from the server it joins.
TEST(ConfigTest, StationAndConnectTogetherIsRejected) {
  EXPECT_FALSE(ParseArgs({kStationArg, kConnectArg, kExampleHost}).result.has_value());
  EXPECT_FALSE(ParseArgs({kStationSceneArg, kTestScene, kConnectArg, kExampleHost}).result.has_value());
}

}  // namespace
}  // namespace z13
