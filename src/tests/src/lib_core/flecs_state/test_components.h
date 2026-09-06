#pragma once

#include <cstdint>
#include <string>

#include <flecs.h>

namespace z13::tests {

struct Position { float x{}, y{}, z{}; };
struct Velocity { float x{}, y{}, z{}; };
struct Health   { std::int32_t hp{}; std::int32_t max_hp{}; };
struct Label    { std::string text; };

struct PlayerTag {};
struct EnemyTag {};

// Relationship tags (used as the relation of a flecs pair).
struct Likes {};
struct Owns {};

// Marker on every fixture entity; capture iterates over it.
struct TestEntity {};

// Registers the components above with flecs meta so world/entity to_json works.
void RegisterTestComponents(flecs::world& world);

}  // namespace z13::tests
