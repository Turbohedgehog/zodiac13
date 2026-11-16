#include <gtest/gtest.h>

#include <algorithm>
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
constexpr float kInFourthGroup = 35.f;

void InstallGrouping(flecs::world& world) {
  world.set(ft::SnapshotGrouping {
      .member = world.component<Position>().id(),
      .make_layout =
          [](flecs::world) {
            return ft::GroupLayout {.group_of = [](flecs::entity e) {
              return static_cast<ft::SnapshotGroupId>(e.get<Position>().x / kGroupWidth);
            }};
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

ft::SharedEntity
SharedEntityOf(const ft::WorldSnapshot& snapshot, std::string_view name) {
  const auto found = std::ranges::find_if(snapshot.entities, [&](const ft::SharedEntity& e) { return e->name == name; });
  return found == snapshot.entities.end() ? nullptr : *found;
}

class SnapshotGroupingTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ft::RegisterStateMeta(world_);
    ft::RegisterComponents<Position, Health, z13::tests::Label>(world_);
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

  uint64_t GroupScans() { return world_.get<ft::SnapshotGrouping>().state_->fingerprints_made; }

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
  for (const ft::SharedEntity& entity : snapshot.entities) {
    EXPECT_TRUE(entity->name != "a" && entity->name != "b" && entity->name != "c") << entity->name;
  }
}

TEST_F(SnapshotGroupingTest, HoldsTheSameStateAsAnUngroupedCapture) {
  Spawn("a", kInFirstGroup);
  Spawn("c", kInSecondGroup);
  world_.entity("hero").add<ft::StateEntity>().set(Health {5, 5});

  const StateByName grouped = Flatten(Capture());
  const auto make_layout = std::exchange(world_.get_mut<ft::SnapshotGrouping>().make_layout, nullptr);
  const ft::WorldSnapshot ungrouped = Capture();
  world_.get_mut<ft::SnapshotGrouping>().make_layout = make_layout;

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

TEST_F(SnapshotGroupingTest, AnUnchangedWorldIsNotScannedAgain) {
  Spawn("a", kInFirstGroup);
  const ft::WorldSnapshot first = Capture();
  const uint64_t scans = GroupScans();

  const ft::WorldSnapshot second = Capture();
  EXPECT_EQ(GroupScans(), scans);
  EXPECT_EQ(second.groups, first.groups);

  world_.lookup("a").set(Position {kAlsoInFirstGroup, 0.f, 0.f});
  Capture();
  EXPECT_EQ(GroupScans(), scans + 1);
}

// Restored from an older snapshot while nothing was written since the last capture: the groups of that capture
// stand in for a scan of the world.
using CurrentCaptureTest = SnapshotGroupingTest;

TEST_F(CurrentCaptureTest, RestoringAQuietWorldDoesNotScanIt) {
  Spawn("a", kInFirstGroup);
  Spawn("c", kInSecondGroup);
  const ft::WorldSnapshot snapshot = Capture();
  const uint64_t scans = GroupScans();
  sets_.clear();

  ASSERT_TRUE(ft::RestoreWorld(world_, snapshot).has_value());

  EXPECT_EQ(GroupScans(), scans);
  EXPECT_EQ(sets_["a"], 0);
  EXPECT_EQ(sets_["c"], 0);
}

TEST_F(CurrentCaptureTest, RestoringAQuietWorldStillFixesTheGroupsThatDiffer) {
  Spawn("a", kInFirstGroup);
  Spawn("c", kInSecondGroup);
  Spawn("d", kInFourthGroup);
  const ft::WorldSnapshot snapshot = Capture();
  Spawn("b", kAlsoInFirstGroup);
  world_.lookup("c").set(Position {kMovedInSecondGroup, 0.f, 0.f});
  Spawn("far", kInThirdGroup);
  Capture();
  const uint64_t scans = GroupScans();
  sets_.clear();

  ASSERT_TRUE(ft::RestoreWorld(world_, snapshot).has_value());

  EXPECT_EQ(GroupScans(), scans);
  EXPECT_EQ(sets_["d"], 0);
  EXPECT_FALSE(world_.lookup("b"));
  EXPECT_FALSE(world_.lookup("far"));
  EXPECT_EQ(XOf("c"), kInSecondGroup);
}

TEST_F(CurrentCaptureTest, RestoringAChangedWorldScansItAndTheNextCaptureEncodesAgain) {
  Spawn("a", kInFirstGroup);
  Spawn("c", kInSecondGroup);
  const ft::WorldSnapshot snapshot = Capture();
  world_.lookup("c").set(Position {kMovedInSecondGroup, 0.f, 0.f});
  const uint64_t scans = GroupScans();

  ASSERT_TRUE(ft::RestoreWorld(world_, snapshot).has_value());
  EXPECT_GT(GroupScans(), scans);
  EXPECT_EQ(XOf("c"), kInSecondGroup);

  EXPECT_EQ(Flatten(Capture()), Flatten(snapshot));
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

TEST_F(SnapshotGroupingTest, AnUnchangedFlatUngroupedEntityIsSharedWithThePreviousCapture) {
  world_.entity("hero").add<ft::StateEntity>().set(Health {5, 5});
  world_.entity("other").add<ft::StateEntity>().set(Health {7, 7});
  const ft::WorldSnapshot first = Capture();

  world_.lookup("other").set(Health {6, 7});
  const ft::WorldSnapshot second = Capture();

  EXPECT_EQ(SharedEntityOf(second, "hero"), SharedEntityOf(first, "hero"));
  EXPECT_NE(SharedEntityOf(second, "other"), SharedEntityOf(first, "other"));
  EXPECT_EQ(Flatten(second), Flatten(ft::CaptureState(world_).value()));
}

TEST_F(SnapshotGroupingTest, AChangedValueOfAnUngroupedEntityIsCaptured) {
  world_.entity("hero").add<ft::StateEntity>().set(Health {5, 5});
  Capture();

  world_.lookup("hero").set(Health {4, 5});
  const ft::WorldSnapshot second = Capture();

  const auto cached = Flatten(second);
  const auto make_layout = std::exchange(world_.get_mut<ft::SnapshotGrouping>().make_layout, nullptr);
  const auto fresh = Flatten(Capture());
  world_.get_mut<ft::SnapshotGrouping>().make_layout = make_layout;
  EXPECT_EQ(cached, fresh);
  EXPECT_EQ(world_.lookup("hero").get<Health>().hp, 4);
}

TEST_F(SnapshotGroupingTest, AnUngroupedEntityWithAStringStaysCurrent) {
  world_.entity("sign").add<ft::StateEntity>().set(z13::tests::Label {"one"});
  const ft::WorldSnapshot first = Capture();

  EXPECT_EQ(SharedEntityOf(Capture(), "sign"), SharedEntityOf(first, "sign"));

  world_.lookup("sign").set(z13::tests::Label {"two"});
  const ft::SharedEntity changed = SharedEntityOf(Capture(), "sign");
  ASSERT_TRUE(changed);
  EXPECT_NE(changed, SharedEntityOf(first, "sign"));
  EXPECT_NE(changed->components, SharedEntityOf(first, "sign")->components);
}

TEST_F(SnapshotGroupingTest, AnEntityGainingAComponentIsCaptured) {
  world_.entity("hero").add<ft::StateEntity>().set(Health {5, 5});
  Capture();

  world_.lookup("hero").set(Position {kInFirstGroup, 0.f, 0.f});
  const ft::WorldSnapshot second = Capture();

  EXPECT_EQ(SharedEntityOf(second, "hero"), nullptr);
  ASSERT_EQ(second.groups.size(), 1u);
}

}  // namespace
