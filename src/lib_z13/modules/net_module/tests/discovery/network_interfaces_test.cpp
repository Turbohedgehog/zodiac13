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

#include <array>
#include <string>
#include <vector>

#include <net_module/discovery_socket.h>
#include <net_module/network_interfaces.h>

namespace z13::net {
namespace {

constexpr uint32_t kHome = 0xC0A8010Au;     // 192.168.1.10
constexpr uint32_t kOffice = 0x0A000005u;   // 10.0.0.5
constexpr uint32_t kTunnel = 0xAC100001u;   // 172.16.0.1
constexpr uint32_t kLoopbackNet = 0x7F000000u;
constexpr uint32_t kFirstOctetMask = 0xFF000000u;

TEST(NetworkInterfacesTest, EachSubnetIsAskedNextToTheLimitedBroadcast) {
  const std::array interfaces {
      InterfaceAddress {.address = kHome, .prefix_length = 24},
      InterfaceAddress {.address = kOffice, .prefix_length = 8},
      InterfaceAddress {.address = kHome + 1, .prefix_length = 24},
  };

  EXPECT_EQ(BroadcastHosts(interfaces),
            (std::vector<std::string> {"10.255.255.255", "192.168.1.255", std::string(kBroadcastHost)}));
}

TEST(NetworkInterfacesTest, APointToPointLinkHasNoBroadcast) {
  const std::array interfaces {
      InterfaceAddress {.address = kTunnel, .prefix_length = 31},
      InterfaceAddress {.address = kTunnel, .prefix_length = 32},
  };

  EXPECT_EQ(BroadcastHosts(interfaces), std::vector<std::string> {std::string(kBroadcastHost)});
}

TEST(NetworkInterfacesTest, TheLoopbackIsntListed) {
  const auto interfaces = ListInterfaceAddresses();
  ASSERT_TRUE(interfaces.has_value()) << interfaces.error();
  for (const InterfaceAddress& entry : *interfaces) {
    EXPECT_NE(entry.address & kFirstOctetMask, kLoopbackNet);
  }
}

}  // namespace
}  // namespace z13::net
