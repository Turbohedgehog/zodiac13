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

#include <string>
#include <vector>

#include <flecs.h>

#include <z13_tests/test_time.h>

#include <z13/components/input.h>

#include "../support/z13_test_world.h"

// Where a system sits relative to the input phases decides whether it sees an action
// value before or after it is shifted, so the networking stages lean on this order.
namespace z13::input {
namespace {

using z13::testing::Z13TestWorld;

TEST(PhaseOrderTest, InputPhasesRunInTheDocumentedOrder) {
  Z13TestWorld test_world;
  flecs::world& world = test_world.World();

  std::vector<std::string> order;
  const auto mark = [&order](const char* name) { order.emplace_back(name); };

  world.system("PhaseOrderTest::PreFrame").kind(flecs::PreFrame).run([&](flecs::iter&) { mark("PreFrame"); });
  world.system("PhaseOrderTest::ScheduledCommands")
      .kind<ScheduledCommandsPhase>()
      .run([&](flecs::iter&) { mark("ScheduledCommands"); });
  world.system("PhaseOrderTest::Clear")
      .kind<ClearActionFramePhase>()
      .run([&](flecs::iter&) { mark("Clear"); });
  world.system("PhaseOrderTest::OnUpdate").kind(flecs::OnUpdate).run([&](flecs::iter&) { mark("OnUpdate"); });
  world.system("PhaseOrderTest::Calculate")
      .kind<CalculateActionFramePhase>()
      .run([&](flecs::iter&) { mark("Calculate"); });
  world.system("PhaseOrderTest::Record")
      .kind<RecordActionFramePhase>()
      .run([&](flecs::iter&) { mark("Record"); });
  world.system("PhaseOrderTest::Remote")
      .kind<RemoteActionFramePhase>()
      .run([&](flecs::iter&) { mark("Remote"); });
  world.system("PhaseOrderTest::Apply")
      .kind<ApplyActionFramePhase>()
      .run([&](flecs::iter&) { mark("Apply"); });
  world.system("PhaseOrderTest::PostFrame").kind(flecs::PostFrame).run([&](flecs::iter&) { mark("PostFrame"); });

  world.progress(z13::testing::kTestDeltaTime);

  const std::vector<std::string> expected {
      "PreFrame", "ScheduledCommands", "Clear", "OnUpdate", "Calculate", "Record", "Remote", "Apply", "PostFrame"};
  EXPECT_EQ(order, expected);
}

}  // namespace
}  // namespace z13::input
