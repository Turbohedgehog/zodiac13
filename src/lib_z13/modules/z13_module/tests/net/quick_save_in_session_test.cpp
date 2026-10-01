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
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <string>

#include <lib_core/simulation_clock.h>
#include <lib_core/world_json_store.h>

#include <net_module/in_memory_transport.h>

#include <z13/components/gameplay.h>
#include <z13/components/net.h>

#include "../support/building_test_helpers.h"
#include "../support/test_network.h"
#include "../support/z13_test_world.h"

// A quick load in a session would swap one peer's world for a local file.
namespace z13::net {
namespace {

namespace ft = z13::flecs_tools;
using z13::testing::KeyDown;
using z13::testing::KeyUp;
using z13::testing::kConnectArg;
using z13::testing::kMaxNetTestTicks;
using z13::testing::kNetTestDeltaTime;
using z13::testing::kServerArg;
using z13::testing::kTestServerEndpoint;
using z13::testing::RunNetworkUntil;
using z13::testing::Z13TestWorld;
using Keycode = z13::fbs::input::Keycode;

constexpr uint64_t kSettleTicks = 30;

class QuickSaveInSessionTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(Run(kMaxNetTestTicks, [&] {
      return client_.World().has<z13::gameplay::Gameplay>() &&
          client_.World().get<ConnectionStatus>().state == ConnectionState::kConnected;
    }));
  }

  bool Run(uint64_t max_ticks, const std::function<bool()>& condition) {
    return RunNetworkUntil(*network_, {server_, client_}, kNetTestDeltaTime, max_ticks, condition);
  }

  void RunTicks(uint64_t ticks) {
    Run(ticks, [] { return false; });
  }

  void Tap(Z13TestWorld& world, Keycode key) {
    world.EmitInput(KeyDown(key));
    RunTicks(kSettleTicks);
    world.EmitInput(KeyUp(key));
    RunTicks(kSettleTicks);
  }

  static void WriteQuickSave(Z13TestWorld& world) {
    std::filesystem::create_directories(world.QuickSavePath().parent_path());
    std::ofstream(world.QuickSavePath(), std::ios::binary) << ft::WorldJsonStore::Save(world.World()).value();
  }

  static uint64_t Tick(Z13TestWorld& world) {
    return world.World().get<ft::SimulationClock>().tick;
  }

  std::shared_ptr<InMemoryNetwork> network_ = std::make_shared<InMemoryNetwork>();
  Z13TestWorld server_ {/*skip_main_menu=*/false, {std::string(kServerArg)}, network_};
  Z13TestWorld client_ {
      /*skip_main_menu=*/false, {std::string(kConnectArg), std::string(kTestServerEndpoint)}, network_};
};

TEST_F(QuickSaveInSessionTest, ClientQuickLoadDoesNotRewindTheServer) {
  WriteQuickSave(server_);
  WriteQuickSave(client_);
  const uint64_t saved_tick = Tick(server_);
  RunTicks(kSettleTicks);

  Tap(client_, Keycode::KEY_F9);

  EXPECT_GT(Tick(server_), saved_tick + kSettleTicks) << "the server loaded its quick save";
  EXPECT_GT(Tick(client_), saved_tick + kSettleTicks) << "the client loaded its quick save";
  EXPECT_EQ(client_.World().get<ConnectionStatus>().state, ConnectionState::kConnected);
}

TEST_F(QuickSaveInSessionTest, HostQuickLoadDoesNotRewindTheSession) {
  WriteQuickSave(server_);
  const uint64_t saved_tick = Tick(server_);
  RunTicks(kSettleTicks);

  Tap(server_, Keycode::KEY_F9);

  EXPECT_GT(Tick(server_), saved_tick + kSettleTicks);
}

TEST_F(QuickSaveInSessionTest, QuickSaveWritesNothingInASession) {
  Tap(client_, Keycode::KEY_F5);
  Tap(server_, Keycode::KEY_F5);

  EXPECT_FALSE(std::filesystem::exists(client_.QuickSavePath()));
  EXPECT_FALSE(std::filesystem::exists(server_.QuickSavePath()));
}

}  // namespace
}  // namespace z13::net
