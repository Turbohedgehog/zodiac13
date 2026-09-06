#include "world_fixture.h"

#include "fixture_names.h"
#include "test_components.h"

namespace z13::tests {

void PopulateFixtureWorld(flecs::world& world) {
  RegisterTestComponents(world);

  world.entity(kPlayer.data())
      .add<TestEntity>()
      .set<Position>({1.f, 2.f, 3.f})
      .set<Velocity>({0.f, 0.f, 0.f})
      .set<Health>({100, 100})
      .set<Label>({"hero"})
      .add<PlayerTag>();

  world.entity(kEnemy1.data())
      .add<TestEntity>()
      .set<Position>({10.f, 0.f, 5.f})
      .set<Health>({30, 50})
      .add<EnemyTag>();

  world.entity(kEnemy2.data())
      .add<TestEntity>()
      .set<Position>({-4.f, 0.f, 8.f})
      .set<Velocity>({1.f, 0.f, 0.f})
      .add<EnemyTag>();

  world.entity(kProp.data())
      .add<TestEntity>()
      .set<Position>({0.f, 0.f, 0.f})
      .set<Label>({"barrel"});

  world.entity(kSquad.data())
      .add<TestEntity>()
      .set<Position>({5.f, 0.f, 5.f});

  world.entity(kSquadLeader.data())
      .add<TestEntity>()
      .set<Position>({5.f, 0.f, 6.f})
      .add<PlayerTag>();

  // ChildOf comes from the "squad::leader" path above; the rest are plain
  // (relation, target) pairs between fixture entities.
  world.entity(kPlayer.data()).add<Likes>(world.entity(kSquad.data()));
  world.entity(kEnemy1.data()).add<Likes>(world.entity(kEnemy2.data()));
  world.entity(kSquadLeader.data()).add<Owns>(world.entity(kProp.data()));
}

}  // namespace z13::tests
