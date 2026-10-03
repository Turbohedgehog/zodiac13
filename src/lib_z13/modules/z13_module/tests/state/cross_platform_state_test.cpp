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
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>

#include <lib_core/component_codec.h>
#include <lib_core/simulation_clock.h>
#include <lib_core/world_serializer.h>

#include "../support/building_test_helpers.h"
#include "../support/z13_test_world.h"

// Dumps a scripted session's state at checkpoints. CI compares the dumps of every
// platform (pr_tests.yml), as the tests here only ever see one platform.
namespace z13::state {
namespace {

namespace ft = z13::flecs_tools;
using z13::testing::Z13TestWorld;
using Keycode = z13::fbs::input::Keycode;

constexpr std::string_view kDumpPathVariable = "Z13_STATE_DUMP";
constexpr uint64_t kCheckpointTicks = 15;
constexpr int kWalkTicks = 45;
constexpr int kBlocks = 3;
constexpr int kTurnTicks = 10;
constexpr int kPushTicks = 40;

std::optional<std::filesystem::path> DumpPath() {
  const std::string variable(kDumpPathVariable);
  const char* path = std::getenv(variable.c_str());
  return path ? std::optional<std::filesystem::path>(path) : std::nullopt;
}

class Scenario {
 public:
  // One line per component, so a diff points at the first diverging value.
  std::string Run() {
    Step();
    WalkAndLook();
    BuildAndTurn();
    z13::testing::Click(world_, Keycode::MOUSE_BUTTON_RIGHT, Advance());
    PushIntoTheBlocks();
    Checkpoint(world_.World().get<ft::SimulationClock>().tick);
    return dump_;
  }

 private:
  void WalkAndLook() {
    world_.EmitInput(z13::testing::KeyDown(Keycode::KEY_W));
    for (int i = 0; i < kWalkTicks; ++i) {
      Look(i % 7 - 3, (i * 5) % 11 - 5);  // varying, fractional degrees
      Step();
    }
    world_.EmitInput(z13::testing::KeyUp(Keycode::KEY_W));
    Step();
  }

  void BuildAndTurn() {
    z13::testing::EnterBuildMode(world_, Advance());
    for (int block = 0; block < kBlocks; ++block) {
      z13::testing::Click(world_, Keycode::MOUSE_BUTTON_LEFT, Advance());
      for (int i = 0; i < kTurnTicks; ++i) {
        Look(3, 0);
        Step();
      }
    }
  }

  // Back towards the blocks, so the physics pushes the player out of them.
  void PushIntoTheBlocks() {
    world_.EmitInput(z13::testing::KeyDown(Keycode::KEY_W));
    for (int i = 0; i < kPushTicks; ++i) {
      Look(i < kPushTicks / 2 ? -4 : 0, 0);
      Step();
    }
    world_.EmitInput(z13::testing::KeyUp(Keycode::KEY_W));
    Step();
  }

  void Look(int dx, int dy) {
    z13::input::MouseMoveEvent move_event;
    move_event.delta = {.x = dx, .y = dy};
    world_.EmitInput(move_event);
  }

  void Step() {
    world_.Tick(1.f / static_cast<float>(world_.Config().GetFPS()));
    const uint64_t tick = world_.World().get<ft::SimulationClock>().tick;
    if (tick % kCheckpointTicks == 0) {
      Checkpoint(tick);
    }
  }

  z13::testing::AdvanceFrame Advance() {
    return [this] { Step(); };
  }

  void Checkpoint(uint64_t tick) {
    for (const ft::EntitySnapshot& entity : ft::CaptureState(world_.World()).entities) {
      for (const ft::ComponentValue& component : entity.components) {
        const flecs::entity type = world_.World().lookup(component.type.c_str());
        const std::string json = ft::ValueToJson(world_.World(), type, component.value).value();
        dump_ += std::format("{}\t{}\t{}\t{}\n", tick, entity.name, component.type, json);
      }
    }
  }

  Z13TestWorld world_;
  std::string dump_;
};

TEST(CrossPlatformStateTest, ScriptedSessionIsDeterministic) {
  const std::string dump = Scenario().Run();

  EXPECT_EQ(Scenario().Run(), dump);
  if (const auto path = DumpPath()) {
    std::ofstream(*path, std::ios::binary) << dump;  // binary: the same bytes on Windows
  }
}

}  // namespace
}  // namespace z13::state
