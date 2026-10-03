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
#include <memory>
#include <string>
#include <unordered_set>
#include <string_view>
#include <tuple>
#include <utility>

#include <flatbuffers/flatbuffers.h>

#include <lib_core/component_codec.h>
#include <lib_core/world_state.h>

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

std::expected<void, std::string> CaptureComponentsAndTags(
    const flecs::world& world, flecs::entity e, const ComponentFilter& accept_component,
    EntitySnapshot& s) {
  std::expected<void, std::string> captured;
  e.each([&](flecs::id id) {
    if (!captured || !id.is_entity()) {
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

    if (!id.entity().has(ecs_id(EcsType))) {
      return;  // data component without meta: cannot round-trip
    }
    auto bytes = ComponentBytes(world, id.raw_id(), value).and_then([&](std::span<const std::byte> view) {
      return EncodeValue(world, id.raw_id(), view);
    });
    if (!bytes) {
      captured = std::unexpected(std::format("'{}' on '{}': {}", path, s.name, bytes.error()));
      return;
    }
    s.components.push_back({std::move(path), std::move(*bytes)});
  });

  std::sort(s.tags.begin(), s.tags.end());
  std::sort(s.components.begin(), s.components.end(),
            [](const ComponentValue& a, const ComponentValue& b) { return a.type < b.type; });
  return captured;
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

std::expected<WorldSnapshot, std::string> CaptureImpl(
    const flecs::world& world, const EntityFilter& accept, const ComponentFilter& accept_component,
    const EntityFilter& accept_target) {
  WorldSnapshot snapshot;
  std::expected<void, std::string> captured;

  flecs::world w = world;
  w.query_builder()
      .with<flecs::Identifier>(flecs::Name)  // every named entity
      .build()
      .each([&](flecs::entity e) {
        if (!captured || (accept && !accept(e))) {
          return;
        }

        EntitySnapshot s;
        s.name = PathOf(e);
        captured = CaptureComponentsAndTags(w, e, accept_component, s);
        CaptureRelationships(e, accept_target, s);
        snapshot.entities.push_back(std::move(s));
      });

  std::sort(snapshot.entities.begin(), snapshot.entities.end(),
            [](const EntitySnapshot& a, const EntitySnapshot& b) { return a.name < b.name; });

  return captured.transform([&snapshot] { return std::move(snapshot); });
}

}  // namespace

std::expected<WorldSnapshot, std::string> CaptureWorld(const flecs::world& world, const EntityFilter& accept) {
  return CaptureImpl(world, accept, {}, {});
}

std::expected<WorldSnapshot, std::string> CaptureWorld(const flecs::world& world) {
  return CaptureWorld(world, DefaultEntityFilter);
}

std::expected<void, std::string> ApplyWorld(flecs::world& world, const WorldSnapshot& snapshot) {
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
      const auto decoded = ComponentBytes(world, comp, e.ensure(comp)).and_then([&](std::span<std::byte> view) {
        return DecodeValue(world, comp, view, c.value);
      });
      e.modified(comp);
      if (!decoded) {
        return std::unexpected(std::format("'{}' on '{}': {}", c.type, s.name, decoded.error()));
      }
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
  return {};
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

std::expected<WorldSnapshot, std::string> CaptureState(const flecs::world& world) {
  return CaptureImpl(world, StateEntityFilter, StateComponentFilter, StateEntityFilter);
}

std::expected<WorldSnapshot, std::string> CaptureEntityState(const flecs::world& world, flecs::entity entity) {
  return CaptureImpl(
      world, [entity](flecs::entity e) { return e == entity; }, StateComponentFilter, StateEntityFilter);
}

namespace {

using Error = std::unexpected<std::string>;

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
      if (auto valid = ValidateValue(world, component, c.value); !valid) {
        return Error(std::format("invalid value for '{}' on '{}': {}", c.type, s.name, valid.error()));
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

  if (auto applied = ApplyWorld(world, snapshot); !applied) {
    return applied;
  }

  for (const auto& s : snapshot.entities) {
    const flecs::entity e = world.lookup(s.name.c_str());
    if (!e.has<flecs::Component>()) {
      e.add<StateEntity>();
    }
    PruneToSnapshot(e, s);
  }
  return {};
}

std::expected<void, std::string> ApplyWorldStateDelta(flecs::world& world, const WorldSnapshot& snapshot) {
  if (auto valid = ValidateSnapshot(world, snapshot); !valid) {
    return valid;
  }

  if (auto applied = ApplyWorld(world, snapshot); !applied) {
    return applied;
  }

  for (const auto& s : snapshot.entities) {
    const flecs::entity e = world.lookup(s.name.c_str());
    if (!e.has<flecs::Component>()) {
      e.add<StateEntity>();
    }
    PruneToSnapshot(e, s);
  }
  return {};
}

fbs::state::WorldSnapshotT ToFlatbuffer(const WorldSnapshot& snapshot) {
  fbs::state::WorldSnapshotT flat;
  for (const EntitySnapshot& entity : snapshot.entities) {
    auto& flat_entity = *flat.entities.emplace_back(std::make_unique<fbs::state::EntitySnapshotT>());
    flat_entity.name = entity.name;
    flat_entity.tags = entity.tags;
    for (const ComponentValue& component : entity.components) {
      auto& flat_component = *flat_entity.components.emplace_back(std::make_unique<fbs::state::ComponentValueT>());
      flat_component.type = component.type;
      flat_component.value = component.value;
    }
    for (const Relationship& relationship : entity.relationships) {
      auto& flat_relationship =
          *flat_entity.relationships.emplace_back(std::make_unique<fbs::state::RelationshipT>());
      flat_relationship.relation = relationship.relation;
      flat_relationship.target = relationship.target;
    }
  }
  return flat;
}

WorldSnapshot FromFlatbuffer(const fbs::state::WorldSnapshotT& flat) {
  WorldSnapshot snapshot;
  for (const auto& flat_entity : flat.entities) {
    EntitySnapshot& entity = snapshot.entities.emplace_back();
    entity.name = flat_entity->name;
    entity.tags = flat_entity->tags;
    for (const auto& flat_component : flat_entity->components) {
      entity.components.push_back({.type = flat_component->type, .value = flat_component->value});
    }
    for (const auto& flat_relationship : flat_entity->relationships) {
      entity.relationships.push_back({.relation = flat_relationship->relation, .target = flat_relationship->target});
    }
  }
  return snapshot;
}

std::expected<std::vector<uint8_t>, std::string> SaveWorldState(
    const flecs::world& world, const EntityFilter& accept) {
  return CaptureWorld(world, accept).transform([](const WorldSnapshot& snapshot) {
    const fbs::state::WorldSnapshotT flat = ToFlatbuffer(snapshot);
    flatbuffers::FlatBufferBuilder builder;
    builder.Finish(fbs::state::WorldSnapshot::Pack(builder, &flat));
    return std::vector<uint8_t>(builder.GetBufferPointer(), builder.GetBufferPointer() + builder.GetSize());
  });
}

std::expected<std::vector<uint8_t>, std::string> SaveWorldState(const flecs::world& world) {
  return SaveWorldState(world, DefaultEntityFilter);
}

std::expected<void, std::string> LoadWorldState(flecs::world& world, std::span<const uint8_t> bytes) {
  flatbuffers::Verifier verifier(bytes.data(), bytes.size());
  if (!fbs::state::VerifyWorldSnapshotBuffer(verifier)) {
    return std::unexpected("malformed WorldSnapshot buffer");
  }
  fbs::state::WorldSnapshotT flat;
  fbs::state::GetWorldSnapshot(bytes.data())->UnPackTo(&flat);
  return ApplyWorld(world, FromFlatbuffer(flat));
}

}  // namespace z13::flecs_tools
