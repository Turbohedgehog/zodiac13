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
#include <map>
#include <string>
#include <vector>

#include <Eigen/Dense>

#include <lib_core/state/world_serializer.h>
#include <z13/components/state_changes.h>
#include <z13/components/wall.h>

#include "../../z13_module/tests/support/z13_test_world.h"
#include "../src/wall_entities.h"
#include "../src/wall_merge.h"
#include "support/station_builders.h"
#include "support/test_namer.h"

// What a player's command changes is recorded as StateChange, so that the server can tell the clients that don't
// simulate the player (docs/network-scaling-plan.md, stage 5b).
namespace z13::building {
namespace {

using z13::gameplay::ActorScope;
using z13::gameplay::StateChange;
using z13::gameplay::StateChangeKind;
using z13::station::WallAxis;
using z13::station::WallId;
using z13::station::WallNode;
using z13::testing::StationWorld;
using z13::testing::Z13TestWorld;
namespace ft = z13::flecs_tools;

constexpr uint32_t kActor = 5;
constexpr int kThickness = 2;
constexpr int kStickLength = 8;
constexpr int kJointReach = 16;

z13::building::primitives::WallGeometry Wall(const Eigen::Vector3i& from, const Eigen::Vector3i& to) {
  return {.node_a = from, .node_b = to, .stick_a = {WallAxis::kZ, kStickLength},
          .stick_b = {WallAxis::kZ, kStickLength}, .thickness = kThickness};
}

std::vector<StateChange> TakeChanges(flecs::world world) {
  std::vector<StateChange> changes;
  std::vector<flecs::entity> events;
  world.query<const StateChange>().each([&](flecs::entity event, const StateChange& change) {
    changes.push_back(change);
    events.push_back(event);
  });
  for (const flecs::entity event : events) {
    event.destruct();
  }
  return changes;
}

// The wall entities of a world by name, each with its state as text.
std::map<std::string, std::string> WallState(flecs::world world) {
  std::map<std::string, std::string> state;
  const auto add = [&](flecs::entity entity) {
    const auto captured = ft::CaptureEntityState(world, entity);
    ASSERT_TRUE(captured.has_value());
    for (const ft::EntitySnapshot& snapshot : captured->entities) {
      std::string text;
      for (const ft::ComponentValue& component : snapshot.components) {
        text += component.type + "=" + std::string(component.value.begin(), component.value.end()) + ";";
      }
      state[snapshot.name] = text;
    }
  };
  world.query<const WallNode>().each([&](flecs::entity e, const WallNode&) { add(e); });
  world.query<const z13::station::WallSegment>().each(
      [&](flecs::entity e, const z13::station::WallSegment&) { add(e); });
  world.query<const z13::station::WallOpening>().each(
      [&](flecs::entity e, const z13::station::WallOpening&) { add(e); });
  return state;
}

// What a client that doesn't simulate the actor does with its changes: sets what was set, removes what was removed.
void Replay(flecs::world from, flecs::world to, const std::vector<StateChange>& changes) {
  ft::WorldSnapshot snapshot;
  std::vector<std::string> removed;
  for (const StateChange& change : changes) {
    if (change.kind == StateChangeKind::kRemoved) {
      removed.push_back(change.name);
      continue;
    }
    const flecs::entity entity = from.entity(change.entity);
    if (!entity.is_alive()) {
      continue;
    }
    const auto captured = ft::CaptureEntityState(from, entity);
    ASSERT_TRUE(captured.has_value());
    for (const ft::EntitySnapshot& snapshot_of : ft::AllEntities(*captured)) {
      snapshot.entities.push_back(snapshot_of);
    }
  }
  ASSERT_TRUE(ft::ApplyWorldStateDelta(to, snapshot).has_value());
  for (const std::string& name : removed) {
    if (const flecs::entity entity = to.lookup(name.c_str())) {
      entity.destruct();
    }
  }
}

TEST(StateChangeTest, AWallLaidByAPlayerIsRecordedWithItsActor) {
  Z13TestWorld test_world = StationWorld();
  flecs::world world = test_world.World();
  TakeChanges(world);

  {
    const ActorScope scope(world.get_mut<z13::gameplay::EditActor>(), kActor);
    SpawnWall(world, z13::testing::TestNamer(world), Wall({0, 0, 0}, {10, 0, 0}));
  }
  const std::vector<StateChange> changes = TakeChanges(world);

  EXPECT_EQ(changes.size(), 3u);  // two nodes and the segment
  for (const StateChange& change : changes) {
    EXPECT_EQ(change.kind, StateChangeKind::kSet);
    EXPECT_EQ(change.actor, kActor);
  }
}

TEST(StateChangeTest, WithoutAnActorNothingIsRecorded) {
  Z13TestWorld test_world = StationWorld();
  flecs::world world = test_world.World();
  TakeChanges(world);

  SpawnWall(world, z13::testing::TestNamer(world), Wall({0, 0, 0}, {10, 0, 0}));

  EXPECT_TRUE(TakeChanges(world).empty());
}

TEST(StateChangeTest, ARemovedWallKeepsTheNamesAndCellsOfWhatWentWithIt) {
  Z13TestWorld test_world = StationWorld();
  flecs::world world = test_world.World();
  const WallId segment = SpawnWall(world, z13::testing::TestNamer(world), Wall({0, 0, 0}, {10, 0, 0}))
                             .get<z13::station::WallSegment>().id;
  TakeChanges(world);

  {
    const ActorScope scope(world.get_mut<z13::gameplay::EditActor>(), kActor);
    DestroyWallSegment(world, segment);
  }
  const std::vector<StateChange> changes = TakeChanges(world);

  ASSERT_EQ(changes.size(), 3u);
  for (const StateChange& change : changes) {
    EXPECT_EQ(change.kind, StateChangeKind::kRemoved);
    EXPECT_FALSE(change.name.empty());
    EXPECT_TRUE(change.cell.has_value());
  }
}

// An edit made in place and not recorded would leave another world short of it.
TEST(StateChangeTest, TheChangesOfAPlayersWallsRebuildThemInAnotherWorld) {
  Z13TestWorld edited = StationWorld();
  Z13TestWorld mirror = StationWorld();
  flecs::world world = edited.World();
  const auto step = [&](const auto& edit) {
    {
      const ActorScope scope(world.get_mut<z13::gameplay::EditActor>(), kActor);
      edit();
    }
    Replay(world, mirror.World(), TakeChanges(world));
    edited.Tick();
  };
  auto& namer = z13::testing::TestNamer(world);
  WallId first_segment {};
  WallId second_node {};
  WallId third_node {};
  step([&] {
    const auto first = SpawnWall(world, namer, Wall({0, 0, 0}, {8, 0, 0}));
    first_segment = first.get<z13::station::WallSegment>().id;
    second_node = first.get<z13::station::WallSegment>().node_b;
  });
  step([&] {
    const auto second = SpawnWall(world, namer, Wall({13, 0, 0}, {20, 0, 0}));
    third_node = second.get<z13::station::WallSegment>().node_a;
  });
  step([&] {
    const auto merge = PlanNodeMerge(
        world, second_node, third_node, MergeAt::kCenter,
        world.get<z13::building::primitives::BlockPalette>().palette, kJointReach);
    ASSERT_TRUE(merge.has_value()) << merge.error();
    ApplyNodeMerge(world, namer, *merge);
  });
  step([&] {
    SpawnWallOpening(world, namer,
                     {.segment_id = first_segment, .kind = z13::station::OpeningKind::kDoor, .offset = 2, .width = 2,
                      .height = 4});
  });
  step([&] { DestroyWallSegment(world, first_segment); });

  EXPECT_EQ(WallState(world), WallState(mirror.World()));
  EXPECT_FALSE(WallState(world).empty());
}

}  // namespace
}  // namespace z13::building
