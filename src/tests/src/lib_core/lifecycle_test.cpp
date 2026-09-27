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

#include <lib_core/lifecycle.h>

namespace z13 {
namespace {

TEST(LifecycleTest, RunsStagesInOrderAndLateCallbacksImmediately) {
  flecs::world world;
  InitLifecycle(world);

  std::vector<std::string> calls;
  const auto record = [&calls](std::string name) {
    return [&calls, name](flecs::world&) { calls.push_back(name); };
  };
  OnInitWorldData(world, record("WorldData"));
  OnInitSystems(world, record("Systems"));
  OnInitPhases(world, record("Phases"));
  OnRegisterComponents(world, [&calls, record](flecs::world& w) {
    calls.emplace_back("Components");
    OnRegisterComponents(w, record("ComponentsQueuedDuringStage"));
  });

  RunLifecycle(world);
  const std::vector<std::string> expected {
      "Components", "ComponentsQueuedDuringStage", "Phases", "Systems", "WorldData"};
  EXPECT_EQ(calls, expected);

  calls.clear();
  OnInitSystems(world, record("LateSystems"));
  EXPECT_EQ(calls, std::vector<std::string> {"LateSystems"});
}

}  // namespace
}  // namespace z13
