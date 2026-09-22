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
#include <vector>

#include <lib_core/config.h>

namespace z13 {
namespace {

struct ParsedConfig {
  Config config;
  std::expected<void, std::string> result;
};

// argv[0] is the (unused) program name boost::program_options expects.
ParsedConfig ParseArgs(std::vector<std::string> args) {
  std::vector<std::string> owned {"z13_test"};
  for (auto& arg : args) {
    owned.push_back(std::move(arg));
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
  const auto parsed = ParseArgs({"--server"});

  EXPECT_TRUE(parsed.config.IsServer());
  EXPECT_EQ(parsed.config.GetPort(), kDefaultServerPort);
  EXPECT_TRUE(parsed.result.has_value());
}

TEST(ConfigTest, ServerFlagWithPortOverridesDefault) {
  const auto parsed = ParseArgs({"--server", "30000"});

  EXPECT_TRUE(parsed.config.IsServer());
  EXPECT_EQ(parsed.config.GetPort(), 30000);
  EXPECT_TRUE(parsed.result.has_value());
}

TEST(ConfigTest, ServerPortZeroIsRejected) {
  EXPECT_FALSE(ParseArgs({"--server", "0"}).result.has_value());
}

TEST(ConfigTest, ServerPortAboveRangeIsRejected) {
  EXPECT_FALSE(ParseArgs({"--server", "70000"}).result.has_value());
}

TEST(ConfigTest, ServerPortNonNumericIsRejected) {
  EXPECT_FALSE(ParseArgs({"--server", "abc"}).result.has_value());
}

TEST(ConfigTest, HelpListsServerAndConnectOptions) {
  const auto parsed = ParseArgs({"--help"});
  std::ostringstream out;
  out << parsed.config;
  const std::string text = out.str();

  EXPECT_NE(text.find("--server"), std::string::npos);
  EXPECT_NE(text.find("--connect"), std::string::npos);
}

TEST(ConfigTest, ConnectWithoutPortUsesDefaultPort) {
  const auto endpoint = ParseArgs({"--connect", "example.com"}).config.GetConnectEndpoint();

  ASSERT_TRUE(endpoint.has_value());
  EXPECT_EQ(endpoint->host, "example.com");
  EXPECT_EQ(endpoint->port, kDefaultServerPort);
}

TEST(ConfigTest, ConnectWithPortIsParsed) {
  const auto endpoint = ParseArgs({"--connect", "example.com:9999"}).config.GetConnectEndpoint();

  ASSERT_TRUE(endpoint.has_value());
  EXPECT_EQ(endpoint->host, "example.com");
  EXPECT_EQ(endpoint->port, 9999);
}

TEST(ConfigTest, ConnectWithEmptyHostIsRejected) {
  const auto parsed = ParseArgs({"--connect", ":80"});

  EXPECT_FALSE(parsed.result.has_value());
  EXPECT_FALSE(parsed.config.GetConnectEndpoint().has_value());
}

TEST(ConfigTest, ConnectWithPortZeroIsRejected) {
  EXPECT_FALSE(ParseArgs({"--connect", "example.com:0"}).result.has_value());
}

TEST(ConfigTest, ServerAndConnectTogetherIsRejected) {
  EXPECT_FALSE(ParseArgs({"--server", "--connect", "example.com"}).result.has_value());
}

}  // namespace
}  // namespace z13
