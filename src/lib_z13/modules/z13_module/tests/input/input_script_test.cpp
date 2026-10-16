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

#include <Eigen/Core>

#include <z13/components/player_parts.h>

#include "../../src/input/input_script.h"
#include "../../src/input/scripted_input_system.h"
#include "../support/z13_test_world.h"

// --input-script: its text, and its steps played into a world's input.
namespace z13::gameplay::input {
namespace {

using Keycode = z13::fbs::input::Keycode;

TEST(InputScriptTest, ParsesKeysTurnsAndQuitSkippingComments) {
  const auto steps = ParseInputScript(
      "# walk, then look around\n"
      "0 KEY_W down\n"
      "\n"
      "60   KEY_W up   # a second at 60 Hz\n"
      "60 turn 90 -10\n"
      "120 quit\n");
  ASSERT_TRUE(steps.has_value()) << steps.error();
  ASSERT_EQ(steps->size(), 4u);
  EXPECT_EQ((*steps)[0].action, ScriptAction::kKeyDown);
  EXPECT_EQ((*steps)[0].key, Keycode::KEY_W);
  EXPECT_EQ((*steps)[1].tick, 60u);
  EXPECT_EQ((*steps)[1].action, ScriptAction::kKeyUp);
  EXPECT_EQ((*steps)[2].action, ScriptAction::kTurn);
  EXPECT_TRUE((*steps)[2].turn_degrees.isApprox(Eigen::Vector2f(90.f, -10.f)));
  EXPECT_EQ((*steps)[3].action, ScriptAction::kQuit);
}

TEST(InputScriptTest, RejectsWhatItCannotPlay) {
  for (const char* text : {"0 KEY_NOPE down", "x KEY_W down", "0 KEY_W sideways", "0 turn 90", "10 quit\n5 quit"}) {
    EXPECT_FALSE(ParseInputScript(text).has_value()) << text;
  }
}

TEST(InputScriptTest, TheScriptWalksAndTurnsThePlayerOnceInTheGame) {
  z13::testing::Z13TestWorld test_world;
  // LookAngles comes with the first frame.
  test_world.Tick();
  const flecs::entity player = test_world.Player();
  const Eigen::Vector3f start = player.get<Eigen::Matrix4f>().col(3).head<3>();
  const float start_yaw = player.get<LookAngles>().yaw_deg;
  test_world.World().set(ScriptedInput {.steps = *ParseInputScript("0 KEY_W down\n30 KEY_W up\n31 turn 45 0\n")});

  for (int tick = 0; tick < 40; ++tick) {
    test_world.Tick();
  }

  EXPECT_GT((player.get<Eigen::Matrix4f>().col(3).head<3>() - start).norm(), 0.f) << "W never moved the player";
  EXPECT_NE(player.get<LookAngles>().yaw_deg, start_yaw) << "the turn never reached the look";
  EXPECT_EQ(test_world.World().get<ScriptedInput>().next, 3u);
}

}  // namespace
}  // namespace z13::gameplay::input
