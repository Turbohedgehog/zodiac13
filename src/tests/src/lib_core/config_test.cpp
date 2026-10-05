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

#include <expected>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <lib_core/settings/config.h>

namespace z13 {
namespace {

constexpr std::string_view kServerArg = "--server";
constexpr std::string_view kConnectArg = "--connect";
constexpr std::string_view kStationArg = "--station";
constexpr std::string_view kHelpArg = "--help";
constexpr std::string_view kExampleHost = "example.com";
constexpr std::string_view kExampleEndpoint = "example.com:9999";
constexpr std::string_view kExampleEndpointPortZero = "example.com:0";
constexpr std::string_view kEmptyHostEndpoint = ":80";

struct ParsedConfig {
  Config config;
  std::expected<void, std::string> result;
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

// A client takes the mode from the server it joins.
TEST(ConfigTest, StationAndConnectTogetherIsRejected) {
  EXPECT_FALSE(ParseArgs({kStationArg, kConnectArg, kExampleHost}).result.has_value());
}

}  // namespace
}  // namespace z13
