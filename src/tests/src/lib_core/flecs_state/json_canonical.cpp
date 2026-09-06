#include "json_canonical.h"

#include <algorithm>
#include <utility>
#include <variant>

#include <rfl/Generic.hpp>
#include <rfl/json.hpp>

#include "test_components.h"

namespace z13::tests {

namespace {

void Canonicalize(rfl::Generic& node) {
  auto& value = node.get();

  if (auto* object = std::get_if<rfl::Generic::Object>(&value)) {
    for (auto& [key, child] : *object) {
      Canonicalize(child);
    }
    std::sort(object->begin(), object->end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    return;
  }

  if (auto* array = std::get_if<rfl::Generic::Array>(&value)) {
    for (auto& element : *array) {
      Canonicalize(element);
    }
    std::sort(array->begin(), array->end(),
              [](const rfl::Generic& a, const rfl::Generic& b) {
                return rfl::json::write(a) < rfl::json::write(b);
              });
  }
}

}  // namespace

std::string CanonicalWorldJson(const flecs::world& world) {
  rfl::Generic::Array entities;

  world.query_builder().with<TestEntity>().build().each([&](flecs::entity e) {
    std::string entity_json = e.to_json().c_str();
    entities.push_back(rfl::json::read<rfl::Generic>(entity_json).value());
  });

  rfl::Generic root{std::move(entities)};
  Canonicalize(root);
  return rfl::json::write(root);
}

}  // namespace z13::tests
