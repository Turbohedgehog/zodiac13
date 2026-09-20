#include <gtest/gtest.h>

#include <optional>
#include <string>

#include <flecs.h>

#include <lib_core/component_meta.h>
#include <lib_core/world_json_store.h>
#include <lib_core/world_state.h>
#include <lib_core/world_state_requests.h>
#include <z13_tests/test_time.h>

#include "test_components.h"

namespace {

namespace ft = z13::flecs_tools;
using z13::tests::Position;
using z13::testing::kTestDeltaTime;

class WorldStateRequestsTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ft::RegisterStateMeta(world_);
    ft::RegisterComponent<Position>(world_);
    hero_ = world_.entity("hero").add<ft::StateEntity>().set(Position{1.f, 0.f, 0.f});
  }

  std::string JsonWithHeroAt(float x) {
    hero_.set(Position{x, 0.f, 0.f});
    return ft::WorldJsonStore::Save(world_).value();
  }

  float HeroX() const { return hero_.get<Position>().x; }

  flecs::world world_;
  flecs::entity hero_;
};

TEST_F(WorldStateRequestsTest, LoadIsAppliedAtTheStartOfTheNextFrame) {
  const std::string json = JsonWithHeroAt(5.f);
  hero_.set(Position{9.f, 0.f, 0.f});
  std::optional<std::expected<void, std::string>> outcome;

  ft::RequestLoadWorldState(world_, json, [&outcome](const auto& result) { outcome = result; });
  EXPECT_EQ(HeroX(), 9.f);
  EXPECT_FALSE(outcome.has_value());

  world_.progress(kTestDeltaTime);

  EXPECT_EQ(HeroX(), 5.f);
  ASSERT_TRUE(outcome.has_value());
  EXPECT_TRUE(outcome->has_value());
}

TEST_F(WorldStateRequestsTest, SystemsOfTheSameFrameSeeTheRestoredState) {
  const std::string json = JsonWithHeroAt(5.f);
  hero_.set(Position{9.f, 0.f, 0.f});
  float seen_by_system = 0.f;
  world_.system<const Position>("Test::ReadPosition").each([&seen_by_system](const Position& p) {
    seen_by_system = p.x;
  });

  ft::RequestLoadWorldState(world_, json);
  world_.progress(kTestDeltaTime);

  EXPECT_EQ(seen_by_system, 5.f);
}

TEST_F(WorldStateRequestsTest, LoadRequestedFromInsideASystemIsApplied) {
  const std::string json = JsonWithHeroAt(5.f);
  hero_.set(Position{9.f, 0.f, 0.f});
  bool requested = false;
  world_.system("Test::RequestLoad").run([&](flecs::iter& it) {
    while (it.next()) {
      if (!requested) {
        requested = true;
        flecs::world world = it.world();
        ft::RequestLoadWorldState(world, json);
      }
    }
  });

  world_.progress(kTestDeltaTime);
  EXPECT_TRUE(requested);
  EXPECT_EQ(HeroX(), 9.f);

  world_.progress(kTestDeltaTime);
  EXPECT_EQ(HeroX(), 5.f);
}

TEST_F(WorldStateRequestsTest, RequestsRunInOrder) {
  std::string saved_before_load;
  const std::string json = JsonWithHeroAt(5.f);
  hero_.set(Position{9.f, 0.f, 0.f});

  ft::RequestSaveWorldState(world_, [&saved_before_load](std::expected<std::string, std::string> saved) {
    saved_before_load = saved.value();
  });
  ft::RequestLoadWorldState(world_, json);
  world_.progress(kTestDeltaTime);

  EXPECT_NE(saved_before_load.find("9"), std::string::npos);
  EXPECT_EQ(HeroX(), 5.f);
}

TEST_F(WorldStateRequestsTest, FailedLoadIsReportedAndLeavesTheWorldUntouched) {
  std::optional<std::expected<void, std::string>> outcome;

  ft::RequestLoadWorldState(world_, "not json", [&outcome](const auto& result) { outcome = result; });
  world_.progress(kTestDeltaTime);

  ASSERT_TRUE(outcome.has_value());
  EXPECT_FALSE(outcome->has_value());
  EXPECT_EQ(HeroX(), 1.f);
}

}  // namespace
