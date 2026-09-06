#pragma once

#include <flecs.h>

namespace z13::tests {

// Registers test components and fills the world with a deterministic set of
// named entities. Every created entity carries the TestEntity marker.
void PopulateFixtureWorld(flecs::world& world);

}  // namespace z13::tests
