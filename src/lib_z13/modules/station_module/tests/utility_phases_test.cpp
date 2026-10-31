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

#include <z13/components/block_changes.h>
#include <z13/components/rooms.h>
#include <z13/components/utilities.h>

#include "../../z13_module/tests/support/z13_test_world.h"

namespace z13::utility {
namespace {

using z13::testing::Z13TestWorld;

template <class Phase>
void Record(Z13TestWorld& world, std::vector<std::string>& order, const char* name) {
  world.World().system(name).kind<Phase>().run([&order, name](flecs::iter&) { order.emplace_back(name); });
}

TEST(UtilityPhasesTest, TheyRunAfterTheRoomsAndBeforeTheBlockChangesAreCleared) {
  Z13TestWorld world;
  std::vector<std::string> order;
  Record<z13::station::StationTopologyPhase>(world, order, "topology");
  Record<UtilitySimulationPhase>(world, order, "simulation");
  Record<UtilityConsumersPhase>(world, order, "consumers");
  Record<z13::station::ClearBlockChangesPhase>(world, order, "cleared");

  world.Tick();

  ASSERT_GE(order.size(), 4u);
  const std::vector<std::string> wanted = {"topology", "simulation", "consumers", "cleared"};
  EXPECT_EQ(std::vector(order.end() - 4, order.end()), wanted);
}

TEST(UtilityPhasesTest, ANetworkModuleIsAsSuppliedAsItSays) {
  Z13TestWorld world;
  const flecs::entity unserved = world.World().entity();
  const flecs::entity served = world.World().entity().set(UtilitySupply {.satisfied = true});
  const flecs::entity starved = world.World().entity().set(UtilitySupply {.satisfied = false});

  EXPECT_TRUE(IsSupplied(unserved));
  EXPECT_TRUE(IsSupplied(served));
  EXPECT_FALSE(IsSupplied(starved));
}

}  // namespace
}  // namespace z13::utility
