#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <optional>

#include <Eigen/Dense>
#include <flecs.h>

#include <lib_core/component_meta.h>
#include <lib_core/world_json_store.h>
#include <lib_core/world_serializer.h>
#include <lib_core/world_state.h>

#include "test_components.h"

namespace {

namespace ft = z13::flecs_tools;
using z13::tests::Health;
using z13::tests::Label;
using z13::tests::Likes;
using z13::tests::PlayerTag;
using z13::tests::Position;
using z13::tests::Velocity;

std::string SaveJson(const flecs::world& world) {
  const auto json = ft::WorldJsonStore::Save(world);
  EXPECT_TRUE(json.has_value()) << (json ? "" : json.error());
  return json.value_or("");
}

// A string member that follows a scalar: its offset must account for padding.
struct Named {
  using State = void;
  float fov = 90.f;
  std::string name;
};

// A singleton that is world state, and one that is not.
struct Settings {
  using State = void;
  using Singleton = void;
  float volume = 1.f;
};

// Not reflectable (flecs has no meta for std::optional): fine as long as it isn't State.
struct RuntimeOnly {
  using Singleton = void;
  std::optional<int> maybe;
};

struct Scratch {
  using Singleton = void;
  float value = 0.f;
};

// Position, Health, Label and PlayerTag are world state; Velocity and EnemyTag are
// runtime-only, and Likes is a plain relation.
void RegisterStateTestComponents(flecs::world& world) {
  ft::RegisterStateMeta(world);
  ft::RegisterComponents<Position, Health, Label, PlayerTag, Named, Velocity, z13::tests::EnemyTag, Likes,
                         Settings, Scratch, RuntimeOnly>(world);
}

flecs::entity SpawnState(flecs::world& world, const char* name) {
  return world.entity(name).add<ft::StateEntity>();
}

class WorldStateTest : public ::testing::Test {
 protected:
  void SetUp() override {
    RegisterStateTestComponents(source_);
    RegisterStateTestComponents(target_);
  }

  flecs::world source_;
  flecs::world target_;
};

TEST_F(WorldStateTest, CaptureKeepsOnlyStateEntitiesAndComponents) {
  SpawnState(source_, "hero").set(Position{1.f, 2.f, 3.f}).set(Velocity{9.f, 9.f, 9.f})
      .add<z13::tests::EnemyTag>();
  source_.entity("bystander").set(Position{5.f, 5.f, 5.f});

  const ft::WorldSnapshot snapshot = ft::CaptureState(source_);

  ASSERT_EQ(snapshot.entities.size(), 1u);
  EXPECT_EQ(snapshot.entities[0].name, "hero");
  ASSERT_EQ(snapshot.entities[0].components.size(), 1u);
  EXPECT_EQ(snapshot.entities[0].components[0].type, "z13::tests::Position");
  EXPECT_TRUE(snapshot.entities[0].tags.empty());
}

TEST_F(WorldStateTest, CaptureDropsRelationshipsToNonStateEntities) {
  const flecs::entity a = SpawnState(source_, "a");
  const flecs::entity b = SpawnState(source_, "b");
  const flecs::entity outsider = source_.entity("outsider");
  a.add<Likes>(b);
  a.add<Likes>(outsider);

  const ft::WorldSnapshot snapshot = ft::CaptureState(source_);

  ASSERT_EQ(snapshot.entities.size(), 2u);
  ASSERT_EQ(snapshot.entities[0].relationships.size(), 1u);
  EXPECT_EQ(snapshot.entities[0].relationships[0].target, "b");
}

TEST_F(WorldStateTest, RestoreReplacesStateAndLeavesRuntimeDataAlone) {
  SpawnState(source_, "hero").set(Position{1.f, 2.f, 3.f}).add<PlayerTag>();
  const ft::WorldSnapshot snapshot = ft::CaptureState(source_);

  SpawnState(target_, "hero").set(Position{7.f, 7.f, 7.f}).set(Health{5, 10}).set(Velocity{4.f, 4.f, 4.f});
  SpawnState(target_, "extra").set(Position{});
  target_.entity("bystander").set(Position{});

  ASSERT_TRUE(ft::RestoreWorld(target_, snapshot).has_value());

  const flecs::entity hero = target_.lookup("hero");
  ASSERT_TRUE(hero);
  EXPECT_EQ(hero.get<Position>().x, 1.f);
  EXPECT_TRUE(hero.has<PlayerTag>());
  EXPECT_FALSE(hero.has<Health>());
  EXPECT_TRUE(hero.has<Velocity>());
  EXPECT_FALSE(target_.lookup("extra"));
  EXPECT_TRUE(target_.lookup("bystander"));
}

TEST_F(WorldStateTest, RestoreCreatesMissingEntitiesAsStateEntities) {
  SpawnState(source_, "hero").set(Label{"Ada"});
  const ft::WorldSnapshot snapshot = ft::CaptureState(source_);

  ASSERT_TRUE(ft::RestoreWorld(target_, snapshot).has_value());

  const flecs::entity hero = target_.lookup("hero");
  ASSERT_TRUE(hero);
  EXPECT_TRUE(hero.has<ft::StateEntity>());
  EXPECT_EQ(hero.get<Label>().text, "Ada");
}

TEST_F(WorldStateTest, RestoreUpdatesRelationshipsInPlace) {
  const flecs::entity a = SpawnState(source_, "a");
  const flecs::entity b = SpawnState(source_, "b");
  SpawnState(source_, "c");
  a.add<Likes>(b);
  const ft::WorldSnapshot snapshot = ft::CaptureState(source_);

  const flecs::entity target_a = SpawnState(target_, "a");
  SpawnState(target_, "b");
  const flecs::entity target_c = SpawnState(target_, "c");
  target_a.add<Likes>(target_c);

  ASSERT_TRUE(ft::RestoreWorld(target_, snapshot).has_value());

  EXPECT_TRUE(target_a.has<Likes>(target_.lookup("b")));
  EXPECT_FALSE(target_a.has<Likes>(target_.lookup("c")));
}

TEST_F(WorldStateTest, RestoreRejectsInvalidSnapshotsWithoutTouchingTheWorld) {
  SpawnState(target_, "hero").set(Position{1.f, 1.f, 1.f});

  ft::WorldSnapshot unknown_component;
  unknown_component.entities.push_back({.name = "hero", .components = {{"z13::tests::Nope", "{}"}}});
  ft::WorldSnapshot non_state_component;
  non_state_component.entities.push_back({.name = "hero", .components = {{"z13::tests::Velocity", "{}"}}});
  ft::WorldSnapshot bad_value;
  bad_value.entities.push_back({.name = "hero", .components = {{"z13::tests::Position", "{\"x\": \"oops\"}"}}});
  ft::WorldSnapshot duplicate_names;
  duplicate_names.entities.push_back({.name = "hero"});
  duplicate_names.entities.push_back({.name = "hero"});
  ft::WorldSnapshot dangling_relation;
  dangling_relation.entities.push_back({.name = "hero", .relationships = {{"z13::tests::Likes", "ghost"}}});

  for (const auto* snapshot :
       {&unknown_component, &non_state_component, &bad_value, &duplicate_names, &dangling_relation}) {
    EXPECT_FALSE(ft::RestoreWorld(target_, *snapshot).has_value());
  }

  const flecs::entity hero = target_.lookup("hero");
  ASSERT_TRUE(hero);
  EXPECT_EQ(hero.get<Position>().x, 1.f);
}

TEST_F(WorldStateTest, JsonRoundTripsBetweenWorlds) {
  SpawnState(source_, "hero").set(Position{1.f / 3.f, -2.5f, 12345.678f}).set(Health{7, 10}).add<PlayerTag>()
      .set(Label{"Ada \"the\" Great"});
  Eigen::Matrix4f transform = Eigen::Matrix4f::Identity();
  transform(0, 3) = 1.f / 3.f;
  transform(2, 1) = 0.1f;
  SpawnState(source_, "camera").set(transform);

  const std::string json = SaveJson(source_);
  ASSERT_TRUE(ft::WorldJsonStore::Load(target_, json).has_value());

  EXPECT_EQ(SaveJson(target_), json);
  const flecs::entity hero = target_.lookup("hero");
  EXPECT_EQ(hero.get<Position>().x, 1.f / 3.f);
  EXPECT_EQ(hero.get<Label>().text, "Ada \"the\" Great");
  EXPECT_EQ(target_.lookup("camera").get<Eigen::Matrix4f>(), transform);
}

TEST_F(WorldStateTest, StringAfterScalarRoundTripsThroughJson) {
  SpawnState(source_, "camera").set(Named{60.f, "TestActorCamera"});

  const std::string json = SaveJson(source_);
  ASSERT_TRUE(ft::WorldJsonStore::Load(target_, json).has_value());

  const Named& restored = target_.lookup("camera").get<Named>();
  EXPECT_EQ(restored.fov, 60.f);
  EXPECT_EQ(restored.name, "TestActorCamera");
}

struct RegistrationTrigger {};

// Module registration runs inside observers, where flecs defers operations.
TEST_F(WorldStateTest, RegistrationInsideAnObserverKeepsAllMembers) {
  flecs::world world;
  ft::RegisterStateMeta(world);
  world.observer<RegistrationTrigger>()
      .event(flecs::OnAdd)
      .each([&world](const RegistrationTrigger&) { ft::RegisterComponent<Named>(world); });
  world.add<RegistrationTrigger>();
  world.entity("camera").add<ft::StateEntity>().set(Named{60.f, "TestActorCamera"});

  const std::string json = SaveJson(world);

  EXPECT_NE(json.find("\"fov\""), std::string::npos) << json;
  EXPECT_NE(json.find("TestActorCamera"), std::string::npos) << json;
}

// State and Singleton are independent properties.
static_assert(ft::StateComponentType<Position> && !ft::SingletonComponentType<Position>);
static_assert(ft::StateComponentType<Settings> && ft::SingletonComponentType<Settings>);
static_assert(!ft::StateComponentType<Scratch> && ft::SingletonComponentType<Scratch>);
static_assert(!ft::StateComponentType<Velocity> && !ft::SingletonComponentType<Velocity>);

TEST_F(WorldStateTest, MetaIsBuiltOnlyForStateComponents) {
  EXPECT_TRUE(source_.component<Position>().has<flecs::Struct>());
  EXPECT_TRUE(source_.component<Settings>().has<flecs::Struct>());
  EXPECT_FALSE(source_.component<Velocity>().has<flecs::Struct>());
  EXPECT_FALSE(source_.component<Scratch>().has<flecs::Struct>());
}

TEST_F(WorldStateTest, ANonReflectableComponentCanStillBeASingleton) {
  const flecs::entity component = source_.component<RuntimeOnly>();
  EXPECT_TRUE(component.has(flecs::Singleton));

  source_.set<RuntimeOnly>({42});

  EXPECT_EQ(source_.get<RuntimeOnly>().maybe, 42);
  EXPECT_EQ(SaveJson(source_).find("RuntimeOnly"), std::string::npos);
}

TEST_F(WorldStateTest, PropertiesAreAppliedIndependently) {
  const auto has_state = [this](flecs::entity component) { return component.has<ft::StateComponent>(); };
  const auto is_singleton = [](flecs::entity component) { return component.has(flecs::Singleton); };

  EXPECT_TRUE(has_state(source_.component<Position>()));
  EXPECT_FALSE(is_singleton(source_.component<Position>()));
  EXPECT_TRUE(has_state(source_.component<Settings>()) && is_singleton(source_.component<Settings>()));
  EXPECT_FALSE(has_state(source_.component<Scratch>()));
  EXPECT_TRUE(is_singleton(source_.component<Scratch>()));
  EXPECT_FALSE(has_state(source_.component<Velocity>()) || is_singleton(source_.component<Velocity>()));
  EXPECT_TRUE(ft::IsStateSingleton(source_.component<Settings>()));
  EXPECT_FALSE(ft::IsStateSingleton(source_.component<Scratch>()));
  EXPECT_FALSE(ft::IsStateSingleton(source_.component<Position>()));
}

void SetUpSingletons(flecs::world& world) {
  world.set<Settings>({0.25f});
  world.set<Scratch>({7.f});
}

TEST_F(WorldStateTest, StateSingletonRoundTripsBetweenWorlds) {
  SetUpSingletons(source_);
  const std::string json = SaveJson(source_);

  ASSERT_TRUE(ft::WorldJsonStore::Load(target_, json).has_value());

  EXPECT_NE(json.find("Settings"), std::string::npos);
  EXPECT_EQ(target_.get<Settings>().volume, 0.25f);
  EXPECT_EQ(SaveJson(target_), json);
}

TEST_F(WorldStateTest, SingletonsWithoutTheStatePropertyAreNeverState) {
  SetUpSingletons(source_);

  const std::string json = SaveJson(source_);
  target_.set<Scratch>({1.f});
  ASSERT_TRUE(ft::WorldJsonStore::Load(target_, json).has_value());

  EXPECT_EQ(json.find("Scratch"), std::string::npos);
  EXPECT_EQ(target_.get<Scratch>().value, 1.f);
}

TEST_F(WorldStateTest, RestoreUpdatesAStateSingletonInPlaceAndKeepsItWhenAbsent) {
  SetUpSingletons(source_);
  const ft::WorldSnapshot snapshot = ft::CaptureState(source_);
  target_.set<Settings>({0.9f});

  ASSERT_TRUE(ft::RestoreWorld(target_, snapshot).has_value());
  EXPECT_EQ(target_.get<Settings>().volume, 0.25f);

  target_.set<Settings>({0.6f});
  ASSERT_TRUE(ft::RestoreWorld(target_, ft::WorldSnapshot{}).has_value());
  EXPECT_TRUE(target_.has<Settings>());
  EXPECT_EQ(target_.get<Settings>().volume, 0.6f);
}

TEST_F(WorldStateTest, PlainComponentEntitiesCannotBeRestoreTargets) {
  SetUpSingletons(target_);
  ft::WorldSnapshot names_a_component;
  names_a_component.entities.push_back(
      {.name = std::string(target_.component<Scratch>().path().c_str() + 2)});

  EXPECT_FALSE(ft::RestoreWorld(target_, names_a_component).has_value());
  EXPECT_EQ(target_.get<Scratch>().value, 7.f);
}

TEST_F(WorldStateTest, NanAndInfinityRoundTrip) {
  constexpr float kNan = std::numeric_limits<float>::quiet_NaN();
  constexpr float kInf = std::numeric_limits<float>::infinity();
  SpawnState(source_, "hero").set(Position{kNan, kInf, 1.f});

  ASSERT_TRUE(ft::WorldJsonStore::Load(target_, SaveJson(source_)).has_value());

  const Position& restored = target_.lookup("hero").get<Position>();
  EXPECT_TRUE(std::isnan(restored.x));
  EXPECT_EQ(restored.y, kInf);
}

TEST_F(WorldStateTest, ToJsonRejectsAValueThatIsNotJson) {
  ft::WorldSnapshot snapshot;
  snapshot.entities.push_back({.name = "hero", .components = {{"z13::tests::Position", "{oops"}}});

  const auto json = ft::WorldJsonStore::ToJson(snapshot);

  ASSERT_FALSE(json.has_value());
  EXPECT_NE(json.error().find("z13::tests::Position"), std::string::npos) << json.error();
}

TEST_F(WorldStateTest, JsonEmbedsValuesAsObjectsAndIsDeterministic) {
  SpawnState(source_, "hero").set(Position{1.f, 2.f, 3.f});

  const std::string json = SaveJson(source_);

  EXPECT_NE(json.find("\"version\": 1"), std::string::npos);
  EXPECT_NE(json.find("\"value\": {"), std::string::npos);
  EXPECT_NE(json.find('\n'), std::string::npos) << "expected pretty-printed output";
  EXPECT_EQ(json, SaveJson(source_));
}

TEST_F(WorldStateTest, JsonLoadRejectsBadInputWithoutTouchingTheWorld) {
  SpawnState(target_, "hero").set(Position{1.f, 1.f, 1.f});

  EXPECT_FALSE(ft::WorldJsonStore::Load(target_, "not json").has_value());
  EXPECT_FALSE(ft::WorldJsonStore::Load(target_, R"({"version":99,"entities":[]})").has_value());
  EXPECT_FALSE(ft::WorldJsonStore::Load(
                   target_, R"({"version":1,"entities":[{"name":"hero","tags":[],"components":[)"
                            R"({"type":"z13::tests::Nope","value":{}}],"relationships":[]}]})")
                   .has_value());

  EXPECT_EQ(target_.lookup("hero").get<Position>().x, 1.f);
}

}  // namespace
