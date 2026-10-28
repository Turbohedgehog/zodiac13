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

#include <cstdint>
#include <memory>
#include <set>
#include <string>

#include <net_module/state_digest.h>
#include <z13/components/block.h>
#include <z13_module/gameplay/gameplay_entities.h>
#include <z13_settings/settings.h>

#include "../../../net_module/src/net_session.h"
#include "../support/building_test_helpers.h"
#include "../support/load_session.h"

// What a player a client doesn't simulate builds reaches the client as an edit: it ends up with the same blocks
// as the server, and no resync.
namespace z13::net {
namespace {

using Keycode = z13::fbs::input::Keycode;
using z13::testing::LoadSession;

constexpr uint64_t kConnectTicks = 600;
constexpr uint64_t kSettleTicks = 60;
// The clients stand 3 m apart in a row behind the host, ids 1 and 2. With the one nearest player simulated, client 1
// simulates the host (the lower id on a tie), so not client 2, which builds.
constexpr size_t kClients = 2;
constexpr size_t kObserver = 0;
constexpr size_t kBuilder = 1;

std::set<std::string> BlockNames(flecs::world world) {
  std::set<std::string> names;
  world.query_builder<const z13::station::Block>().build().each(
      [&](flecs::entity block, const z13::station::Block&) { names.insert(block.name().c_str()); });
  return names;
}

uint64_t EditPackets(LoadSession& session) {
  const SentTraffic& sent = session.Server().World().get<NetSession>().Sent();
  const auto found = sent.by_message.find(fbs::net::MessageBody::WorldEdit);
  return found == sent.by_message.end() ? 0 : found->second.packets;
}

class GhostEditsTest : public ::testing::Test {
 protected:
  void SetUp() override {
    session_ = std::make_unique<LoadSession>(kClients, z13::testing::Steering {.walk = false, .look = false},
                                             [](z13::Settings& settings) {
                                               settings.net->interest_events = true;
                                               settings.net->ghosts = true;
                                               settings.net->simulated_players_max = 1;
                                               settings.net->simulated_players_slack = 0;
                                               settings.net->ghost_radius_m = 5000.;
                                               settings.net->simulated_radius_m = 5000.;
                                             });
    ASSERT_TRUE(session_->Connect(kConnectTicks));
    Tick(kSettleTicks);
  }

  void Tick(uint64_t count) {
    for (uint64_t i = 0; i < count; ++i) {
      session_->Tick();
    }
  }

  z13::testing::Z13TestWorld& Client(size_t index) { return session_->Client(index); }

  std::unique_ptr<LoadSession> session_;
};

TEST_F(GhostEditsTest, ABlockBuiltAndDestroyedByAPlayerTheClientDoesNotSimulateReachesIt) {
  const auto step = [&] { session_->Tick(); };
  const uint32_t builder_id = *Client(kBuilder).World().get<z13::gameplay::LocalPlayer>().id;
  ASSERT_FALSE(Client(kObserver).World().lookup(z13::gameplay::PlayerEntityName(builder_id).c_str()))
      << "the observer simulates the builder";

  z13::testing::Tap(Client(kBuilder), Keycode::KEY_TAB, step);
  Tick(kSettleTicks);
  z13::testing::Click(Client(kBuilder), Keycode::MOUSE_BUTTON_LEFT, step);
  Tick(kSettleTicks);

  const std::set<std::string> built = BlockNames(session_->Server().World());
  ASSERT_EQ(built.size(), 1u);
  EXPECT_EQ(BlockNames(Client(kObserver).World()), built) << "the observer never got the block";
  EXPECT_EQ(BlockNames(Client(kBuilder).World()), built);
  EXPECT_EQ(EditPackets(*session_), 1u) << "only the observer lacks the builder";

  z13::testing::Click(Client(kBuilder), Keycode::MOUSE_BUTTON_RIGHT, step);
  Tick(kSettleTicks);

  EXPECT_TRUE(BlockNames(session_->Server().World()).empty());
  EXPECT_TRUE(BlockNames(Client(kObserver).World()).empty()) << "the observer kept a block that was destroyed";
  EXPECT_EQ(EditPackets(*session_), 2u);
  for (size_t i = 0; i < kClients; ++i) {
    EXPECT_EQ(Client(i).World().get<StateDigests>().resyncs, 0u) << "client " << i;
  }
}

}  // namespace
}  // namespace z13::net
