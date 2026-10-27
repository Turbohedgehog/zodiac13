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
#include <functional>
#include <set>
#include <string>
#include <vector>

#include <net_module/state_digest.h>
#include <z13/components/block.h>
#include <z13_settings/settings.h>

#include "../support/building_test_helpers.h"
#include "../support/load_session.h"

namespace z13::net {
namespace {

using Keycode = z13::fbs::input::Keycode;
using z13::testing::LoadSession;

constexpr uint64_t kConnectTicks = 600;
constexpr uint64_t kSettleTicks = 60;
constexpr uint64_t kWalkAwayTicks = 1200;
constexpr int kFarBuilds = 3;

std::set<std::string> BlockNames(flecs::world world) {
  std::set<std::string> names;
  world.query_builder<const z13::station::Block>().build().each(
      [&](flecs::entity block, const z13::station::Block&) { names.insert(block.name().c_str()); });
  return names;
}

TEST(NamesTest, ABlockBuiltBeyondAClientsPresenceIsNamedAsTheServerNamesIt) {
  LoadSession session(2, {.walk = false, .look = false}, [](z13::Settings& settings) {
    settings.net->interest_events = true;
  });
  ASSERT_TRUE(session.Connect(kConnectTicks));
  const auto tick = [&](uint64_t count) {
    for (uint64_t i = 0; i < count; ++i) {
      session.Tick();
    }
  };
  const auto step = [&] { session.Tick(); };
  z13::testing::Z13TestWorld& far_builder = session.Client(0);
  z13::testing::Z13TestWorld& near_builder = session.Client(1);
  tick(kSettleTicks);

  far_builder.EmitInput(z13::testing::KeyDown(Keycode::KEY_W));
  tick(kWalkAwayTicks);
  far_builder.EmitInput(z13::testing::KeyUp(Keycode::KEY_W));
  tick(kSettleTicks);
  z13::testing::Tap(far_builder, Keycode::KEY_TAB, step);
  tick(kSettleTicks);
  for (int i = 0; i < kFarBuilds; ++i) {
    z13::testing::Click(far_builder, Keycode::MOUSE_BUTTON_LEFT, step);
    tick(kSettleTicks);
  }
  ASSERT_EQ(BlockNames(session.Server().World()).size(), static_cast<size_t>(kFarBuilds));
  ASSERT_TRUE(BlockNames(near_builder.World()).empty()) << "the near client saw the far builds";

  z13::testing::Tap(near_builder, Keycode::KEY_TAB, step);
  tick(kSettleTicks);
  z13::testing::Click(near_builder, Keycode::MOUSE_BUTTON_LEFT, step);
  tick(kSettleTicks);

  const std::set<std::string> server_names = BlockNames(session.Server().World());
  const std::set<std::string> near_names = BlockNames(near_builder.World());
  ASSERT_EQ(server_names.size(), static_cast<size_t>(kFarBuilds + 1));
  ASSERT_EQ(near_names.size(), 1u) << "the near client's world holds more than its own block";
  const flecs::entity mine = near_builder.World().lookup(near_names.begin()->c_str());
  const flecs::entity same_name = session.Server().World().lookup(near_names.begin()->c_str());
  ASSERT_TRUE(same_name) << "the server has no block named " << *near_names.begin();
  EXPECT_EQ(mine.get<z13::station::Block>(), same_name.get<z13::station::Block>())
      << "the near client's " << *near_names.begin() << " is another block on the server";
  EXPECT_EQ(near_builder.World().get<StateDigests>().resyncs, 0u);
}

}  // namespace
}  // namespace z13::net
