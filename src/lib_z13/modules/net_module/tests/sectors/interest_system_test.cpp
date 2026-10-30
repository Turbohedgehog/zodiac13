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

#include <memory>
#include <string>

#include <net_module/in_memory_transport.h>
#include <sectors/interest_map.h>
#include <sectors/sector_index.h>
#include <z13/components/gameplay.h>
#include <z13/components/net.h>
#include <z13/components/player.h>

#include "../../../z13_module/tests/support/test_network.h"
#include "../../../z13_module/tests/support/z13_test_world.h"
#include "../../src/interest_system.h"

namespace z13::net {
namespace {

using sectors::InterestMap;
using sectors::MemberKind;
using z13::gameplay::Gameplay;
using z13::testing::kConnectArg;
using z13::testing::kMaxNetTestTicks;
using z13::testing::kNetTestDeltaTime;
using z13::testing::kServerArg;
using z13::testing::kTestServerEndpoint;
using z13::testing::RunNetworkUntil;
using z13::testing::Z13TestWorld;

bool IsConnected(Z13TestWorld& world) {
  return world.World().has<Gameplay>() &&
         world.World().get<ConnectionStatus>().state == ConnectionState::kConnected;
}

// A server and two connected clients.
class InterestSystemTest : public ::testing::Test {
 protected:
  void SetUp() override {
    network_ = std::make_shared<InMemoryNetwork>();
    server_ = std::make_unique<Z13TestWorld>(std::vector<std::string> {std::string(kServerArg)}, network_);
    first_ = std::make_unique<Z13TestWorld>(
        std::vector<std::string> {std::string(kConnectArg), std::string(kTestServerEndpoint)}, network_);
    second_ = std::make_unique<Z13TestWorld>(
        std::vector<std::string> {std::string(kConnectArg), std::string(kTestServerEndpoint)}, network_);
    ASSERT_TRUE(RunNetworkUntil(*network_, {*server_, *first_, *second_}, kNetTestDeltaTime, kMaxNetTestTicks, [&] {
      return IsConnected(*first_) && IsConnected(*second_);
    }));
    Run(kSettleTicks);
  }

  void Run(uint64_t ticks) {
    RunNetworkUntil(*network_, {*server_, *first_, *second_}, kNetTestDeltaTime, ticks, [] { return false; });
  }

  static uint32_t PlayerOf(Z13TestWorld& client) { return client.World().get<z13::gameplay::LocalPlayer>().id.value(); }

  static constexpr uint64_t kSettleTicks = 30;
  std::shared_ptr<InMemoryNetwork> network_;
  std::unique_ptr<Z13TestWorld> server_;
  std::unique_ptr<Z13TestWorld> first_;
  std::unique_ptr<Z13TestWorld> second_;
};

TEST_F(InterestSystemTest, EveryConnectedPlayerIsSubscribedAroundItsSector) {
  const auto& map = server_->World().get<InterestMap>();
  const auto& index = server_->World().get<sectors::SectorIndex>();

  for (Z13TestWorld* client : {first_.get(), second_.get()}) {
    const uint32_t player = PlayerOf(*client);
    const auto sector = index.Locate({MemberKind::kPlayer, player});
    ASSERT_TRUE(sector.has_value());
    EXPECT_TRUE(map.Covers(player, *sector));
    EXPECT_TRUE(map.IsInner(player, *sector));
  }
}

}  // namespace
}  // namespace z13::net
