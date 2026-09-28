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

#include <algorithm>
#include <cstdint>
#include <format>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <flecs.h>

#include <z13_tests/test_time.h>

#include <z13/components/gameplay.h>
#include <z13/components/input.h>

#include "../support/z13_test_world.h"

namespace z13::input {
namespace {

using z13::testing::kServerArg;
using z13::testing::Z13TestWorld;

TEST(PhaseOrderTest, PhasesRunInTheDocumentedOrder) {
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
  world.system("PhaseOrderTest::GameplayPreUpdate")
      .kind<z13::gameplay::PreUpdatePhase>()
      .run([&](flecs::iter&) { mark("GameplayPreUpdate"); });
  world.system("PhaseOrderTest::GameplayUpdate")
      .kind<z13::gameplay::UpdatePhase>()
      .run([&](flecs::iter&) { mark("GameplayUpdate"); });
  world.system("PhaseOrderTest::GameplayPostUpdate")
      .kind<z13::gameplay::PostUpdatePhase>()
      .run([&](flecs::iter&) { mark("GameplayPostUpdate"); });
  world.system("PhaseOrderTest::OnValidate").kind(flecs::OnValidate).run([&](flecs::iter&) { mark("OnValidate"); });
  world.system("PhaseOrderTest::PostUpdate").kind(flecs::PostUpdate).run([&](flecs::iter&) { mark("PostUpdate"); });
  world.system("PhaseOrderTest::OnStore").kind(flecs::OnStore).run([&](flecs::iter&) { mark("OnStore"); });
  world.system("PhaseOrderTest::PostFrame").kind(flecs::PostFrame).run([&](flecs::iter&) { mark("PostFrame"); });

  world.progress(z13::testing::kTestDeltaTime);

  // Gameplay's Pre/Update phases and the input chain both only follow OnUpdate; the
  // tie goes to the smaller phase path (see LinearizePhases).
  const std::vector<std::string> expected {"PreFrame", "ScheduledCommands", "Clear", "OnUpdate",
      "GameplayPreUpdate", "GameplayUpdate", "Calculate", "Record", "Remote", "Apply",
      "GameplayPostUpdate", "OnValidate", "PostUpdate", "OnStore", "PostFrame"};
  EXPECT_EQ(order, expected);
}

// A second DependsOn (flecs' default OnUpdate left behind by a deferred .kind()) makes the
// pipeline schedule the system by the deeper phase, not the declared one.
std::vector<std::string> SystemsWithSeveralPhases(flecs::world& world) {
  std::vector<std::string> names;
  world.query_builder().with(flecs::System).build().each([&names](flecs::entity system) {
    int32_t phase_count {};
    system.each(flecs::DependsOn, [&phase_count](flecs::entity) { ++phase_count; });
    if (phase_count > 1) {
      names.emplace_back(system.path().c_str());
    }
  });
  return names;
}

// Mirrors how the pipeline ranks phases: one past the deepest DependsOn target.
int32_t PhaseDepth(flecs::entity phase) {
  int32_t depth {};
  phase.each(flecs::DependsOn, [&depth](flecs::entity target) { depth = std::max(depth, PhaseDepth(target) + 1); });
  return depth;
}

// Systems of two phases at the same depth run in entity id order, which differs between
// Debug and Release, so every phase that has systems needs a depth of its own.
std::set<std::string> PhasesSharingDepth(flecs::world& world) {
  std::map<int32_t, std::string> phase_by_depth;
  std::set<std::string> clashes;
  world.query_builder().with(flecs::System).build().each([&](flecs::entity system) {
    system.each(flecs::DependsOn, [&](flecs::entity phase) {
      const auto [it, inserted] = phase_by_depth.try_emplace(PhaseDepth(phase), phase.name().c_str());
      if (!inserted && it->second != phase.name().c_str()) {
        clashes.insert(std::format("{} / {}", it->second, phase.name().c_str()));
      }
    });
  });
  return clashes;
}

TEST(PhaseOrderTest, EverySystemHasAtMostOnePhase) {
  Z13TestWorld local;
  EXPECT_EQ(SystemsWithSeveralPhases(local.World()), std::vector<std::string> {});

  Z13TestWorld server(true, {std::string(kServerArg)});
  EXPECT_EQ(SystemsWithSeveralPhases(server.World()), std::vector<std::string> {});
}

TEST(PhaseOrderTest, NoPhaseLivesInAnAnonymousNamespace) {
  Z13TestWorld server(true, {std::string(kServerArg)});
  std::vector<std::string> anonymous;
  server.World().query_builder().with(flecs::Phase).build().each([&anonymous](flecs::entity phase) {
    const std::string path {phase.path().c_str()};
    if (path.find("anonymous") != std::string::npos) {
      anonymous.push_back(path);
    }
  });
  // The path is the phase-order tie-break, and compilers spell anonymous namespaces differently.
  EXPECT_EQ(anonymous, std::vector<std::string> {});
}

TEST(PhaseOrderTest, EveryPhaseDependsOnlyOnItsAnchor) {
  Z13TestWorld server(true, {std::string(kServerArg)});
  std::vector<std::string> offenders;
  server.World().query_builder().with(flecs::Phase).build().each([&offenders](flecs::entity phase) {
    int32_t anchor_count {};
    int32_t phase_count {};
    phase.each(flecs::DependsOn, [&](flecs::entity target) {
      ++(target.has(flecs::Phase) ? phase_count : anchor_count);
    });
    if (phase != flecs::OnStart && (anchor_count != 1 || phase_count != 0)) {
      offenders.emplace_back(phase.path().c_str());
    }
  });
  EXPECT_EQ(offenders, std::vector<std::string> {});
}

TEST(PhaseOrderTest, DisablingAPhaseKeepsTheLaterOnesRunning) {
  Z13TestWorld test_world;
  flecs::world& world = test_world.World();

  std::vector<std::string> order;
  world.system("PhaseOrderTest::Calculate")
      .kind<CalculateActionFramePhase>()
      .run([&order](flecs::iter&) { order.emplace_back("Calculate"); });
  world.system("PhaseOrderTest::Record")
      .kind<RecordActionFramePhase>()
      .run([&order](flecs::iter&) { order.emplace_back("Record"); });
  world.system("PhaseOrderTest::PostFrame")
      .kind(flecs::PostFrame)
      .run([&order](flecs::iter&) { order.emplace_back("PostFrame"); });

  world.component<CalculateActionFramePhase>().disable();
  world.progress(z13::testing::kTestDeltaTime);

  const std::vector<std::string> expected {"Record", "PostFrame"};
  EXPECT_EQ(order, expected);
}

TEST(PhaseOrderTest, NoTwoPhasesRunAtTheSameDepth) {
  Z13TestWorld server(true, {std::string(kServerArg)});
  EXPECT_EQ(PhasesSharingDepth(server.World()), std::set<std::string> {});
}

}  // namespace
}  // namespace z13::input
