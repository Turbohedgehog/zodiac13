#pragma once

#include <string>

#include <flecs.h>

namespace z13::tests {

// Serializes every TestEntity-tagged entity with flecs `entity.to_json()` and
// returns a canonical form: object keys sorted, array elements sorted by value.
// Two worlds holding the same state produce byte-identical output.
std::string CanonicalWorldJson(const flecs::world& world);

}  // namespace z13::tests
