/*
 * Copyright 2026 Ivan Kulenko / Zodiac13
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://apache.org
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <lib_core/world_serializer.h>

#include <algorithm>
#include <format>
#include <string>
#include <unordered_set>
#include <string_view>
#include <tuple>
#include <utility>

#include <lib_core/log.h>
#include <lib_core/world_state.h>

// reflect-cpp's headers trigger warnings under /W4 that this project treats as
// errors; suppress them for code this project doesn't own.
#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#include <rfl/msgpack.hpp>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

namespace z13::flecs_tools {

namespace {

// Relation name used for the parent (ChildOf) pair in a snapshot.
constexpr std::string_view kChildOf = "ChildOf";
// Leading separator flecs puts on rooted paths ("::a::b").
constexpr std::string_view kRootScope = "::";
// Built-in flecs namespace; entities under it are never serialized.
constexpr std::string_view kFlecsName = "flecs";
constexpr std::string_view kFlecsScope = "flecs::";

bool IsUnderFlecs(std::string_view path) {
  if (path.starts_with(kRootScope)) {
    path.remove_prefix(kRootScope.size());
  }
  return path == kFlecsName || path.starts_with(kFlecsScope);
}

// flecs paths come back rooted; return them without the leading root scope.
std::string PathOf(flecs::entity e) {
  std::string path = e.path().c_str();
  if (std::string_view{path}.starts_with(kRootScope)) {
    path.erase(0, kRootScope.size());
  }
  return path;
}

void CaptureComponentsAndTags(
    const flecs::world& world, flecs::entity e, const ComponentFilter& accept_component,
    EntitySnapshot& s) {
  e.each([&](flecs::id id) {
    if (!id.is_entity()) {
      return;  // pairs handled separately
    }

    std::string path = PathOf(id.entity());
    if (IsUnderFlecs(path) || (accept_component && !accept_component(id.entity()))) {
      return;
    }

    const void* value = e.try_get(id.raw_id());
    if (value == nullptr) {
      s.tags.push_back(std::move(path));  // zero-size component -> tag
      return;
    }

    if (const flecs::string json = world.to_json(id.raw_id(), value); json.size() > 0) {
      s.components.push_back({std::move(path), json.c_str()});
    }
    // data component without meta: cannot round-trip, skipped.
  });

  std::sort(s.tags.begin(), s.tags.end());
  std::sort(s.components.begin(), s.components.end(),
            [](const ComponentValue& a, const ComponentValue& b) { return a.type < b.type; });
}

void CaptureRelationships(flecs::entity e, const EntityFilter& accept_target, EntitySnapshot& s) {
  e.each([&](flecs::id id) {
    if (!id.is_pair()) {
      return;
    }

    const flecs::entity relation = id.first();
    const flecs::entity target = id.second();
    if (!target || !target.is_alive() || (accept_target && !accept_target(target))) {
      return;
    }

    std::string relation_name;
    if (relation.raw_id() == flecs::ChildOf) {
      relation_name = kChildOf;
    } else {
      relation_name = PathOf(relation);
      if (IsUnderFlecs(relation_name)) {
        return;
      }
    }

    s.relationships.push_back({std::move(relation_name), PathOf(target)});
  });

  std::sort(s.relationships.begin(), s.relationships.end(),
            [](const Relationship& a, const Relationship& b) {
              return std::tie(a.relation, a.target) < std::tie(b.relation, b.target);
            });
}

}  // namespace

bool DefaultEntityFilter(flecs::entity e) {
  if (!e.is_alive()) {
    return false;
  }
  const std::string path = PathOf(e);
  if (path.empty() || IsUnderFlecs(path)) {
    return false;
  }
  return !e.has<flecs::Component>() && !e.has(flecs::Module);
}

namespace {

WorldSnapshot CaptureImpl(
    const flecs::world& world, const EntityFilter& accept, const ComponentFilter& accept_component,
    const EntityFilter& accept_target) {
  WorldSnapshot snapshot;

  flecs::world w = world;
  w.query_builder()
      .with<flecs::Identifier>(flecs::Name)  // every named entity
      .build()
      .each([&](flecs::entity e) {
        if (accept && !accept(e)) {
          return;
        }

        EntitySnapshot s;
        s.name = PathOf(e);
        CaptureComponentsAndTags(w, e, accept_component, s);
        CaptureRelationships(e, accept_target, s);
        snapshot.entities.push_back(std::move(s));
      });

  std::sort(snapshot.entities.begin(), snapshot.entities.end(),
            [](const EntitySnapshot& a, const EntitySnapshot& b) { return a.name < b.name; });

  return snapshot;
}

}  // namespace

WorldSnapshot CaptureWorld(const flecs::world& world, const EntityFilter& accept) {
  return CaptureImpl(world, accept, {}, {});
}

WorldSnapshot CaptureWorld(const flecs::world& world) {
  return CaptureWorld(world, DefaultEntityFilter);
}

void ApplyWorld(flecs::world& world, const WorldSnapshot& snapshot) {
  // Pass 1: entities, tags, component values.
  for (const auto& s : snapshot.entities) {
    flecs::entity e = world.entity(s.name.c_str());

    for (const auto& tag : s.tags) {
      if (const flecs::entity t = world.lookup(tag.c_str())) {
        e.add(t);
      }
    }

    for (const auto& c : s.components) {
      const flecs::entity comp = world.lookup(c.type.c_str());
      if (!comp) {
        continue;
      }
      void* value = e.ensure(comp);
      world.from_json(comp, value, c.value.c_str());
      e.modified(comp);
    }
  }

  // Pass 2: relationships, once every target entity exists.
  for (const auto& s : snapshot.entities) {
    flecs::entity e = world.entity(s.name.c_str());
    for (const auto& r : s.relationships) {
      const flecs::entity target = world.lookup(r.target.c_str());
      if (!target) {
        continue;
      }

      if (r.relation == kChildOf) {
        e.add(flecs::ChildOf, target);
      } else if (const flecs::entity relation = world.lookup(r.relation.c_str())) {
        e.add(relation, target);
      }
    }
  }
}

// A singleton's value lives on its component entity, so those are state only for
// state singletons that have a value; every other entity needs the StateEntity tag.
bool StateEntityFilter(flecs::entity e) {
  if (!e.is_alive()) {
    return false;
  }
  return e.has<flecs::Component>() ? IsStateSingleton(e) && e.has(e) : e.has<StateEntity>();
}

bool StateComponentFilter(flecs::entity component) {
  return component.has<StateComponent>();
}

WorldSnapshot CaptureState(const flecs::world& world) {
  return CaptureImpl(world, StateEntityFilter, StateComponentFilter, StateEntityFilter);
}

std::vector<char> SaveEntityState(const flecs::world& world, flecs::entity entity) {
  const WorldSnapshot snapshot = CaptureImpl(
      world, [entity](flecs::entity e) { return e == entity; }, StateComponentFilter, StateEntityFilter);
  return rfl::msgpack::write(snapshot);
}

namespace {

using Error = std::unexpected<std::string>;

// A value is valid if flecs can parse it into a fresh instance of its component.
bool ParsesAs(flecs::world& world, flecs::entity component, const std::string& json) {
  void* scratch = ecs_value_new(world.c_ptr(), component.id());
  if (scratch == nullptr) {
    return false;
  }
  const bool parsed = world.from_json(component, scratch, json.c_str()) != nullptr;
  ecs_value_free(world.c_ptr(), component.id(), scratch);
  return parsed;
}

std::expected<void, std::string> ValidateSnapshot(flecs::world& world, const WorldSnapshot& snapshot) {
  std::unordered_set<std::string> names;
  for (const auto& s : snapshot.entities) {
    if (s.name.empty() || !names.insert(s.name).second) {
      return Error(std::format("invalid or duplicate entity name '{}'", s.name));
    }
    if (const flecs::entity existing = world.lookup(s.name.c_str());
        existing && existing.has<flecs::Component>() && !IsStateSingleton(existing)) {
      return Error(std::format("'{}' is a component, not a state entity", s.name));
    }
  }

  const auto state_component = [&world](const std::string& type) {
    const flecs::entity component = world.lookup(type.c_str());
    return component && StateComponentFilter(component) ? component : flecs::entity{};
  };

  for (const auto& s : snapshot.entities) {
    for (const auto& tag : s.tags) {
      if (!state_component(tag)) {
        return Error(std::format("unknown state tag '{}' on '{}'", tag, s.name));
      }
    }

    for (const auto& c : s.components) {
      const flecs::entity component = state_component(c.type);
      if (!component) {
        return Error(std::format("unknown state component '{}' on '{}'", c.type, s.name));
      }
      if (!ParsesAs(world, component, c.value)) {
        return Error(std::format("invalid value for '{}' on '{}'", c.type, s.name));
      }
    }

    for (const auto& r : s.relationships) {
      const bool known_relation =
          r.relation == kChildOf || static_cast<bool>(world.lookup(r.relation.c_str()));
      const bool known_target = names.contains(r.target) || static_cast<bool>(world.lookup(r.target.c_str()));
      if (!known_relation || !known_target) {
        return Error(std::format("unresolved relationship '{}' -> '{}' on '{}'", r.relation, r.target, s.name));
      }
    }
  }
  return {};
}

// Removes state components and relationships of `e` that the snapshot doesn't list.
void PruneToSnapshot(flecs::entity e, const EntitySnapshot& s) {
  std::unordered_set<std::string> kept_types(s.tags.begin(), s.tags.end());
  for (const auto& c : s.components) {
    kept_types.insert(c.type);
  }

  std::vector<flecs::id> stale_ids;
  e.each([&](flecs::id id) {
    if (id.is_entity() && StateComponentFilter(id.entity()) && !kept_types.contains(PathOf(id.entity()))) {
      stale_ids.push_back(id);
    }
  });
  for (const flecs::id id : stale_ids) {
    e.remove(id);
  }

  EntitySnapshot current;
  CaptureRelationships(e, StateEntityFilter, current);
  for (const auto& r : current.relationships) {
    const bool kept = std::ranges::any_of(s.relationships, [&r](const Relationship& kept_r) {
      return kept_r.relation == r.relation && kept_r.target == r.target;
    });
    if (kept) {
      continue;
    }

    const flecs::world world = e.world();
    const flecs::entity target = world.lookup(r.target.c_str());
    if (r.relation == kChildOf) {
      e.remove(flecs::ChildOf, target);
    } else {
      e.remove(world.lookup(r.relation.c_str()), target);
    }
  }
}

}  // namespace

std::expected<void, std::string> RestoreWorld(flecs::world& world, const WorldSnapshot& snapshot) {
  if (auto valid = ValidateSnapshot(world, snapshot); !valid) {
    return valid;
  }

  std::unordered_set<std::string> names;
  for (const auto& s : snapshot.entities) {
    names.insert(s.name);
  }

  std::vector<flecs::entity> stale_entities;
  world.query_builder().with<StateEntity>().build().each([&](flecs::entity e) {
    if (StateEntityFilter(e) && e.name().length() > 0 && !names.contains(PathOf(e))) {
      stale_entities.push_back(e);
    }
  });
  for (const flecs::entity e : stale_entities) {
    e.destruct();
  }

  ApplyWorld(world, snapshot);

  for (const auto& s : snapshot.entities) {
    const flecs::entity e = world.lookup(s.name.c_str());
    if (!e.has<flecs::Component>()) {
      e.add<StateEntity>();
    }
    PruneToSnapshot(e, s);
  }
  return {};
}

std::expected<void, std::string> ApplyWorldStateDelta(flecs::world& world, const std::vector<char>& bytes) {
  auto result = rfl::msgpack::read<WorldSnapshot>(bytes);
  if (!result) {
    return Error(result.error().what());
  }

  const WorldSnapshot& snapshot = result.value();
  if (auto valid = ValidateSnapshot(world, snapshot); !valid) {
    return valid;
  }

  ApplyWorld(world, snapshot);

  for (const auto& s : snapshot.entities) {
    const flecs::entity e = world.lookup(s.name.c_str());
    if (!e.has<flecs::Component>()) {
      e.add<StateEntity>();
    }
    PruneToSnapshot(e, s);
  }
  return {};
}

std::vector<char> SaveState(const flecs::world& world) {
  return rfl::msgpack::write(CaptureState(world));
}

std::vector<char> SaveState(const WorldSnapshot& snapshot) {
  return rfl::msgpack::write(snapshot);
}

std::expected<void, std::string> LoadState(flecs::world& world, const std::vector<char>& bytes) {
  auto result = rfl::msgpack::read<WorldSnapshot>(bytes);
  if (!result) {
    return Error(result.error().what());
  }
  return RestoreWorld(world, result.value());
}

std::vector<char> SaveWorldState(const flecs::world& world, const EntityFilter& accept) {
  return rfl::msgpack::write(CaptureWorld(world, accept));
}

std::vector<char> SaveWorldState(const flecs::world& world) {
  return SaveWorldState(world, DefaultEntityFilter);
}

bool LoadWorldState(flecs::world& world, const std::vector<char>& bytes) {
  auto result = rfl::msgpack::read<WorldSnapshot>(bytes);
  if (!result) {
    log_error("z13::LoadWorldState: {}", result.error().what());
    return false;
  }

  ApplyWorld(world, result.value());
  return true;
}

}  // namespace z13::flecs_tools
