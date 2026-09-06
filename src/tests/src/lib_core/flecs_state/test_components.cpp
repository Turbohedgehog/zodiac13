#include "test_components.h"

#include <lib_core/component_meta.h>

namespace z13::tests {

void RegisterTestComponents(flecs::world& world) {
  z13::flecs_tools::RegisterStdStringMeta(world);
  z13::flecs_tools::RegisterComponentsMeta<Position, Velocity, Health, Label,
                                           PlayerTag, EnemyTag, Likes, Owns, TestEntity>(world);
}

}  // namespace z13::tests
