#include <vector>

#include <gtest/gtest.h>

#include <flecs.h>
#include <rfl/json.hpp>

#include <lib_core/world_serializer.h>

#include "fixture_names.h"
#include "json_canonical.h"
#include "test_components.h"
#include "world_fixture.h"

namespace z13::tests {
namespace {

namespace ft = z13::flecs_tools;

// Restrict serialization to the fixture entities.
const ft::EntityFilter kTestFilter = [](flecs::entity e) { return e.has<TestEntity>(); };

ft::WorldSnapshot Capture(const flecs::world& world) {
  return ft::CaptureWorld(world, kTestFilter);
}

class FlecsStateRoundTrip : public ::testing::Test {
 protected:
  flecs::world world_a_;  // filled from the fixture
  flecs::world world_b_;  // rebuilt from the binary state

  void SetUp() override {
    PopulateFixtureWorld(world_a_);
    RegisterTestComponents(world_b_);
  }

  // SaveWorldState(A) -> LoadWorldState(B).
  void RunPipeline() {
    const std::vector<char> binary = ft::SaveWorldState(world_a_, kTestFilter);
    ASSERT_FALSE(binary.empty());
    ASSERT_TRUE(ft::LoadWorldState(world_b_, binary));
  }
};

TEST_F(FlecsStateRoundTrip, PreservesWorldJson) {
  RunPipeline();
  EXPECT_EQ(CanonicalWorldJson(world_a_), CanonicalWorldJson(world_b_));
}

TEST_F(FlecsStateRoundTrip, PreservesSnapshot) {
  RunPipeline();
  EXPECT_EQ(rfl::json::write(Capture(world_a_)), rfl::json::write(Capture(world_b_)));
}

TEST_F(FlecsStateRoundTrip, MissingComponentsStayMissing) {
  RunPipeline();

  const auto prop = world_b_.lookup(kProp.data());
  ASSERT_TRUE(prop);
  EXPECT_FALSE(prop.has<Velocity>());
  EXPECT_FALSE(prop.has<Health>());
  EXPECT_FALSE(prop.has<PlayerTag>());
}

TEST_F(FlecsStateRoundTrip, HierarchicalNamesPreserved) {
  RunPipeline();

  const auto leader = world_b_.lookup(kSquadLeader.data());
  ASSERT_TRUE(leader);
  EXPECT_TRUE(leader.has<PlayerTag>());
  EXPECT_STREQ(leader.parent().name().c_str(), kSquad.data());
}

TEST_F(FlecsStateRoundTrip, PlainRelationshipsPreserved) {
  RunPipeline();

  const auto player = world_b_.lookup(kPlayer.data());
  const auto enemy_1 = world_b_.lookup(kEnemy1.data());
  const auto enemy_2 = world_b_.lookup(kEnemy2.data());
  const auto squad = world_b_.lookup(kSquad.data());
  ASSERT_TRUE(player && enemy_1 && enemy_2 && squad);

  EXPECT_TRUE(player.has<Likes>(squad));
  EXPECT_TRUE(enemy_1.has<Likes>(enemy_2));
  EXPECT_FALSE(enemy_2.has<Likes>(enemy_1));
}

TEST_F(FlecsStateRoundTrip, ParentAndOwnsRelationshipsPreserved) {
  RunPipeline();

  const auto leader = world_b_.lookup(kSquadLeader.data());
  const auto squad = world_b_.lookup(kSquad.data());
  const auto prop = world_b_.lookup(kProp.data());
  ASSERT_TRUE(leader && squad && prop);

  EXPECT_TRUE(leader.has(flecs::ChildOf, squad));
  EXPECT_TRUE(leader.has<Owns>(prop));
}

TEST(FlecsStateSnapshot, BinaryWriteIsDeterministic) {
  flecs::world world;
  PopulateFixtureWorld(world);

  const auto first = ft::SaveWorldState(world, kTestFilter);
  const auto second = ft::SaveWorldState(world, kTestFilter);
  EXPECT_FALSE(first.empty());
  EXPECT_EQ(first, second);
}

TEST(FlecsStateSnapshot, EmptyWorldRoundTrips) {
  flecs::world world_a;
  flecs::world world_b;
  RegisterTestComponents(world_a);
  RegisterTestComponents(world_b);

  const auto binary = ft::SaveWorldState(world_a, kTestFilter);
  ASSERT_TRUE(ft::LoadWorldState(world_b, binary));

  EXPECT_TRUE(ft::CaptureWorld(world_a, kTestFilter).entities.empty());
  EXPECT_EQ(CanonicalWorldJson(world_a), CanonicalWorldJson(world_b));
}

}  // namespace
}  // namespace z13::tests
