#pragma once

#include <string_view>

namespace z13::tests {

// Fixture entity paths, shared between world_fixture and the tests.
// All are string-literal backed, so `.data()` is safe to pass to the flecs API.
inline constexpr std::string_view kPlayer = "player";
inline constexpr std::string_view kEnemy1 = "enemy_1";
inline constexpr std::string_view kEnemy2 = "enemy_2";
inline constexpr std::string_view kProp = "prop";
inline constexpr std::string_view kSquad = "squad";
inline constexpr std::string_view kSquadLeader = "squad::leader";

}  // namespace z13::tests
