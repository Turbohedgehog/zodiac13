#include <gtest/gtest.h>

#include <bit>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <flecs.h>

#include <lib_core/state/snapshot_grouping.h>
#include <lib_core/state/world_serializer.h>
#include <lib_core/state/world_state.h>

#include "test_components.h"

namespace {

namespace ft = z13::flecs_tools;
using z13::tests::Health;
using z13::tests::Position;

constexpr float kGroupWidth = 10.f;
constexpr float kInFirstGroup = 1.f;
constexpr float kAlsoInFirstGroup = 2.f;
constexpr float kInSecondGroup = 11.f;
constexpr float kMovedInSecondGroup = 12.f;
constexpr float kInThirdGroup = 25.f;

void InstallGrouping(flecs::world& world) {
  world.set(ft::SnapshotGrouping {
      .member = world.component<Position>().id(),
      .make_group_of =
          [] {
            return ft::GroupOf([](flecs::entity e) {
              return static_cast<ft::SnapshotGroupId>(e.get<Position>().x / kGroupWidth);
            });
          },
      .hash_of =
          [](flecs::entity e) {
            const Position& position = e.get<Position>();
            uint64_t hash = ft::HashName(std::string_view(e.name().c_str(), e.name().length()));
            for (const float value : {position.x, position.y, position.z}) {
              hash = ft::MixHash(hash ^ std::bit_cast<uint32_t>(value));
            }
            return hash;
          },
  });
}

using ComponentBytes = std::vector<std::pair<std::string, std::vector<uint8_t>>>;
using StateByName = std::map<std::string, ComponentBytes>;

StateByName Flatten(const ft::WorldSnapshot& snapshot) {
  StateByName state;
  for (const ft::EntitySnapshot& entity : ft::AllEntities(snapshot)) {
    ComponentBytes& components = state[entity.name];
    for (const ft::ComponentValue& component : entity.components) {
      components.emplace_back(component.type, component.value);
    }
  }
  return state;
}

class SnapshotGroupingTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ft::RegisterStateMeta(world_);
    ft::RegisterComponents<Position, Health>(world_);
    InstallGrouping(world_);
    world_.observer<const Position>().event(flecs::OnSet).each([this](flecs::entity e, const Position&) {
      ++sets_[e.name().c_str()];
    });
  }

  void Spawn(std::string_view name, float x) {
    world_.entity(std::string(name).c_str()).add<ft::StateEntity>().set(Position {x, 0.f, 0.f});
  }

  float XOf(std::string_view name) { return world_.lookup(std::string(name).c_str()).get<Position>().x; }

  ft::WorldSnapshot Capture() { return ft::CaptureState(world_).value(); }

  flecs::world world_;
  std::map<std::string, int> sets_;
};

TEST_F(SnapshotGroupingTest, GroupedEntitiesAreFiledByGroupNotListedFlat) {
  Spawn("a", kInFirstGroup);
  Spawn("b", kAlsoInFirstGroup);
  Spawn("c", kInSecondGroup);
  world_.entity("hero").add<ft::StateEntity>().set(Health {5, 5});

  const ft::WorldSnapshot snapshot = Capture();

  ASSERT_EQ(snapshot.groups.size(), 2u);
  EXPECT_EQ(snapshot.groups[0]->id, 0u);
  EXPECT_EQ(snapshot.groups[0]->entities.size(), 2u);
  EXPECT_EQ(snapshot.groups[1]->id, 1u);
  EXPECT_EQ(snapshot.groups[1]->entities.size(), 1u);
  for (const ft::EntitySnapshot& entity : snapshot.entities) {
    EXPECT_TRUE(entity.name != "a" && entity.name != "b" && entity.name != "c") << entity.name;
  }
}

TEST_F(SnapshotGroupingTest, HoldsTheSameStateAsAnUngroupedCapture) {
  Spawn("a", kInFirstGroup);
  Spawn("c", kInSecondGroup);
  world_.entity("hero").add<ft::StateEntity>().set(Health {5, 5});

  const StateByName grouped = Flatten(Capture());
  const auto make_group_of = std::exchange(world_.get_mut<ft::SnapshotGrouping>().make_group_of, nullptr);
  const ft::WorldSnapshot ungrouped = Capture();
  world_.get_mut<ft::SnapshotGrouping>().make_group_of = make_group_of;

  EXPECT_TRUE(ungrouped.groups.empty());
  EXPECT_EQ(grouped, Flatten(ungrouped));
}

TEST_F(SnapshotGroupingTest, AnUnnamedMemberIsLeftOutLikeAnyUnnamedEntity) {
  world_.entity().add<ft::StateEntity>().set(Position {kInFirstGroup, 0.f, 0.f});

  EXPECT_TRUE(Capture().groups.empty());
}

TEST_F(SnapshotGroupingTest, AnUnchangedGroupIsSharedWithThePreviousCapture) {
  Spawn("a", kInFirstGroup);
  Spawn("c", kInSecondGroup);
  const ft::WorldSnapshot first = Capture();

  world_.lookup("c").set(Position {kMovedInSecondGroup, 0.f, 0.f});
  const ft::WorldSnapshot second = Capture();

  ASSERT_EQ(second.groups.size(), 2u);
  EXPECT_EQ(second.groups[0], first.groups[0]);
  EXPECT_NE(second.groups[1], first.groups[1]);
}

TEST_F(SnapshotGroupingTest, AnUnchangedWorldIsNotFingerprintedAgain) {
  int hashed = 0;
  bool unchanged = false;
  auto& grouping = world_.get_mut<ft::SnapshotGrouping>();
  const auto hash_of = grouping.hash_of;
  grouping.hash_of = [&hashed, hash_of](flecs::entity e) {
    ++hashed;
    return hash_of(e);
  };
  grouping.unchanged = [&unchanged] { return unchanged; };
  Spawn("a", kInFirstGroup);
  const ft::WorldSnapshot first = Capture();

  hashed = 0;
  unchanged = true;
  const ft::WorldSnapshot second = Capture();
  EXPECT_EQ(hashed, 0);
  EXPECT_EQ(second.groups, first.groups);

  unchanged = false;
  Capture();
  EXPECT_EQ(hashed, 1);
  world_.get_mut<ft::SnapshotGrouping>().unchanged = nullptr;
}

TEST_F(SnapshotGroupingTest, RestoreLeavesAnUnchangedGroupAlone) {
  Spawn("a", kInFirstGroup);
  Spawn("b", kAlsoInFirstGroup);
  Spawn("c", kInSecondGroup);
  const ft::WorldSnapshot snapshot = Capture();
  world_.lookup("c").set(Position {kMovedInSecondGroup, 0.f, 0.f});
  sets_.clear();

  ASSERT_TRUE(ft::RestoreWorld(world_, snapshot).has_value());

  EXPECT_EQ(sets_["a"], 0);
  EXPECT_EQ(sets_["b"], 0);
  EXPECT_EQ(sets_["c"], 1);
  EXPECT_EQ(XOf("c"), kInSecondGroup);
}

TEST_F(SnapshotGroupingTest, RestoreRemovesAnEntityAddedToAGroup) {
  Spawn("a", kInFirstGroup);
  const ft::WorldSnapshot snapshot = Capture();
  Spawn("b", kAlsoInFirstGroup);

  ASSERT_TRUE(ft::RestoreWorld(world_, snapshot).has_value());

  EXPECT_FALSE(world_.lookup("b"));
  EXPECT_EQ(XOf("a"), kInFirstGroup);
}

TEST_F(SnapshotGroupingTest, RestoreRemovesAGroupTheSnapshotLacks) {
  Spawn("a", kInFirstGroup);
  const ft::WorldSnapshot snapshot = Capture();
  Spawn("far", kInThirdGroup);

  ASSERT_TRUE(ft::RestoreWorld(world_, snapshot).has_value());

  EXPECT_FALSE(world_.lookup("far"));
}

TEST_F(SnapshotGroupingTest, RestoreBringsBackWhatWasRemovedFromAGroup) {
  Spawn("a", kInFirstGroup);
  Spawn("b", kAlsoInFirstGroup);
  const ft::WorldSnapshot snapshot = Capture();
  world_.lookup("b").destruct();

  ASSERT_TRUE(ft::RestoreWorld(world_, snapshot).has_value());

  ASSERT_TRUE(world_.lookup("b"));
  EXPECT_EQ(XOf("b"), kAlsoInFirstGroup);
}

}  // namespace
