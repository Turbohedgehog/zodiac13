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

#include <lib_core/endpoint.h>

namespace z13 {
namespace {

constexpr uint16_t kTestDefaultPort = 26213;

TEST(ParseEndpointTest, HostOnlyUsesDefaultPort) {
  const auto endpoint = ParseEndpoint("example.com", kTestDefaultPort);

  ASSERT_TRUE(endpoint.has_value());
  EXPECT_EQ(endpoint->host, "example.com");
  EXPECT_EQ(endpoint->port, kTestDefaultPort);
}

TEST(ParseEndpointTest, IpAndPortAreParsed) {
  const auto endpoint = ParseEndpoint("192.168.0.1:9999", kTestDefaultPort);

  ASSERT_TRUE(endpoint.has_value());
  EXPECT_EQ(endpoint->host, "192.168.0.1");
  EXPECT_EQ(endpoint->port, 9999);
}

TEST(ParseEndpointTest, EmptyTextIsRejected) {
  EXPECT_FALSE(ParseEndpoint("", kTestDefaultPort).has_value());
}

TEST(ParseEndpointTest, EmptyHostIsRejected) {
  EXPECT_FALSE(ParseEndpoint(":80", kTestDefaultPort).has_value());
}

TEST(ParseEndpointTest, EmptyPortAfterColonIsRejected) {
  EXPECT_FALSE(ParseEndpoint("example.com:", kTestDefaultPort).has_value());
}

TEST(ParseEndpointTest, PortZeroIsRejected) {
  EXPECT_FALSE(ParseEndpoint("example.com:0", kTestDefaultPort).has_value());
}

TEST(ParseEndpointTest, PortAboveRangeIsRejected) {
  EXPECT_FALSE(ParseEndpoint("example.com:70000", kTestDefaultPort).has_value());
}

TEST(ParseEndpointTest, NonNumericPortIsRejected) {
  EXPECT_FALSE(ParseEndpoint("example.com:abc", kTestDefaultPort).has_value());
}

}  // namespace
}  // namespace z13
