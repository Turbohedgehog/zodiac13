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

#include <lib_core/state/world_serializer.h>

#include <algorithm>
#include <format>
#include <map>
#include <memory>
#include <optional>
#include <unordered_map>
#include <string>
#include <unordered_set>
#include <string_view>
#include <tuple>
#include <utility>

#include <flatbuffers/flatbuffers.h>

#include <lib_core/state/component_codec.h>
#include <lib_core/state/snapshot_grouping.h>
#include <lib_core/state/world_state.h>
#include <lib_core/utils/status.h>

#include "group_bucket.h"

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

Status CaptureComponentsAndTags(
    const flecs::world& world, flecs::entity e, const ComponentFilter& accept_component,
    EntitySnapshot& s) {
  Status captured;
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

std::expected<EntitySnapshot, std::string> CaptureEntity(
    const flecs::world& world, flecs::entity e, const ComponentFilter& accept_component,
    const EntityFilter& accept_target) {
  EntitySnapshot s;
  s.name = PathOf(e);
  if (auto captured = CaptureComponentsAndTags(world, e, accept_component, s); !captured) {
    return std::unexpected(std::move(captured.error()));
  }
  CaptureRelationships(e, accept_target, s);
  return s;
}

void SortByName(std::vector<EntitySnapshot>& entities) {
  std::sort(entities.begin(), entities.end(),
            [](const EntitySnapshot& a, const EntitySnapshot& b) { return a.name < b.name; });
}

using GroupBuckets = std::map<SnapshotGroupId, GroupBucket>;

OptionalGrouping ActiveGrouping(const SnapshotGrouping* grouping) {
  if (grouping == nullptr || !grouping->member || !grouping->make_group_of || !grouping->hash_of) {
    return std::nullopt;
  }
  return std::cref(*grouping);
}

// The member term is read only: an inout one would mark the component changed on every scan.
GroupBuckets ScanGroups(flecs::world world, const SnapshotGrouping& grouping) {
  GroupBuckets buckets;
  const GroupOf group_of = grouping.make_group_of();
  world.query_builder()
      .with<StateEntity>()
      .with<flecs::Identifier>(flecs::Name)
      .with(*grouping.member).in()
      .build()
      .each([&](flecs::entity e) {
        if (StateEntityFilter(e)) {
          buckets[group_of(e)].Add(e, grouping.hash_of(e));
        }
      });
  return buckets;
}

Status EncodeGroups(
    const flecs::world& world, const SnapshotGrouping& grouping, const ComponentFilter& accept_component,
    const EntityFilter& accept_target) {
  auto& previous = grouping.state_->captured;
  std::map<SnapshotGroupId, std::shared_ptr<const GroupSnapshot>> captured;
  for (const auto& [id, bucket] : ScanGroups(world, grouping)) {
    const uint64_t fingerprint = bucket.Fingerprint();
    if (const auto same = previous.find(id); same != previous.end() && same->second->fingerprint == fingerprint) {
      captured.emplace(id, same->second);
      continue;
    }

    GroupSnapshot group {.id = id, .fingerprint = fingerprint};
    for (const flecs::entity member : bucket.Members()) {
      auto entity = CaptureEntity(world, member, accept_component, accept_target);
      if (!entity) {
        return std::unexpected(std::move(entity.error()));
      }
      group.entities.push_back(std::move(*entity));
    }
    SortByName(group.entities);
    captured.emplace(id, std::make_shared<const GroupSnapshot>(std::move(group)));
  }
  previous = std::move(captured);
  return {};
}

Status CaptureGroups(
    const flecs::world& world, const SnapshotGrouping& grouping, const ComponentFilter& accept_component,
    const EntityFilter& accept_target, WorldSnapshot& snapshot) {
  auto& captured = grouping.state_->captured;
  const bool unchanged = grouping.unchanged && grouping.unchanged();
  if (!unchanged || captured.empty()) {
    if (auto encoded = EncodeGroups(world, grouping, accept_component, accept_target); !encoded) {
      captured.clear();  // `unchanged` already moved on
      return encoded;
    }
  }
  for (const auto& [id, group] : captured) {
    snapshot.groups.push_back(group);
  }
  return {};
}

std::expected<WorldSnapshot, std::string> CaptureImpl(
    const flecs::world& world, const EntityFilter& accept, const ComponentFilter& accept_component,
    const EntityFilter& accept_target, OptionalGrouping grouping = std::nullopt) {
  WorldSnapshot snapshot;
  Status captured;

  flecs::world w = world;
  auto ungrouped = w.query_builder();
  ungrouped.with<flecs::Identifier>(flecs::Name);  // every named entity
  if (grouping) {
    ungrouped.without(*grouping->get().member);
  }
  ungrouped.build().each([&](flecs::entity e) {
    if (!captured || (accept && !accept(e))) {
      return;
    }

    auto entity = CaptureEntity(w, e, accept_component, accept_target);
    if (!entity) {
      captured = std::unexpected(std::move(entity.error()));
      return;
    }
    snapshot.entities.push_back(std::move(*entity));
  });
  if (captured && grouping) {
    captured = CaptureGroups(w, *grouping, accept_component, accept_target, snapshot);
  }

  SortByName(snapshot.entities);

  return captured.transform([&snapshot] { return std::move(snapshot); });
}

}  // namespace

std::expected<WorldSnapshot, std::string> CaptureWorld(const flecs::world& world, const EntityFilter& accept) {
  return CaptureImpl(world, accept, {}, {});
}

std::expected<WorldSnapshot, std::string> CaptureWorld(const flecs::world& world) {
  return CaptureWorld(world, DefaultEntityFilter);
}

namespace {

Status ApplyEntities(flecs::world& world, const EntityRefs& entities) {
  // Entities first, so values can reference any of them by path.
  for (const EntitySnapshot& s : entities) {
    world.entity(s.name.c_str());
  }

  for (const EntitySnapshot& s : entities) {
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
  for (const EntitySnapshot& s : entities) {
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

}  // namespace

EntityRefs AllEntities(const WorldSnapshot& snapshot) {
  EntityRefs all(snapshot.entities.begin(), snapshot.entities.end());
  for (const auto& group : snapshot.groups) {
    all.insert(all.end(), group->entities.begin(), group->entities.end());
  }
  return all;
}

Status ApplyWorld(flecs::world& world, const WorldSnapshot& snapshot) {
  return ApplyEntities(world, AllEntities(snapshot));
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

std::expected<WorldSnapshot, std::string> CaptureState(flecs::world world) {
  return CaptureImpl(
      world, StateEntityFilter, StateComponentFilter, StateEntityFilter,
      ActiveGrouping(world.try_get<SnapshotGrouping>()));
}

std::expected<WorldSnapshot, std::string> CaptureEntityState(const flecs::world& world, flecs::entity entity) {
  return CaptureImpl(
      world, [entity](flecs::entity e) { return e == entity; }, StateComponentFilter, StateEntityFilter);
}

namespace {

using Error = std::unexpected<std::string>;

Status ValidateEntities(flecs::world& world, const EntityRefs& entities) {
  std::unordered_set<std::string> names;
  for (const EntitySnapshot& s : entities) {
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

  for (const EntitySnapshot& s : entities) {
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

using GroupIds = std::unordered_set<SnapshotGroupId>;

GroupIds MatchingGroups(flecs::world& world, const WorldSnapshot& snapshot, OptionalGrouping grouping) {
  GroupIds matching;
  if (snapshot.groups.empty() || !grouping) {
    return matching;
  }
  const GroupBuckets buckets = ScanGroups(world, *grouping);
  for (const auto& group : snapshot.groups) {
    if (const auto bucket = buckets.find(group->id);
        bucket != buckets.end() && bucket->second.Fingerprint() == group->fingerprint) {
      matching.insert(group->id);
    }
  }
  return matching;
}

EntityRefs EntitiesToApply(const WorldSnapshot& snapshot, const GroupIds& matching) {
  EntityRefs entities(snapshot.entities.begin(), snapshot.entities.end());
  for (const auto& group : snapshot.groups) {
    if (!matching.contains(group->id)) {
      entities.insert(entities.end(), group->entities.begin(), group->entities.end());
    }
  }
  return entities;
}

}  // namespace

Status RestoreWorld(flecs::world& world, const WorldSnapshot& snapshot) {
  const OptionalGrouping grouping = ActiveGrouping(world.try_get<SnapshotGrouping>());
  const GroupIds matching = MatchingGroups(world, snapshot, grouping);
  const EntityRefs entities = EntitiesToApply(snapshot, matching);
  if (auto valid = ValidateEntities(world, entities); !valid) {
    return valid;
  }

  std::unordered_set<std::string> names;
  for (const EntitySnapshot& s : entities) {
    names.insert(s.name);
  }

  const GroupOf group_of = matching.empty() ? GroupOf {} : grouping->get().make_group_of();
  const auto in_matching_group = [&](flecs::entity e) {
    return group_of && e.has(*grouping->get().member) && matching.contains(group_of(e));
  };
  std::vector<flecs::entity> stale_entities;
  world.query_builder().with<StateEntity>().build().each([&](flecs::entity e) {
    if (StateEntityFilter(e) && e.name().length() > 0 && !in_matching_group(e) && !names.contains(PathOf(e))) {
      stale_entities.push_back(e);
    }
  });
  for (const flecs::entity e : stale_entities) {
    e.destruct();
  }

  // Value singletons the snapshot lacks are kept, but a tag's presence is its value: one
  // the snapshot lacks was unset there (e.g. StationMode).
  std::vector<flecs::entity> stale_singleton_tags;
  world.query_builder().with<StateComponent>().with(flecs::Singleton).build().each([&](flecs::entity component) {
    const auto* info = component.try_get<flecs::Component>();
    if (info != nullptr && info->size == 0 && StateEntityFilter(component) && !names.contains(PathOf(component))) {
      stale_singleton_tags.push_back(component);
    }
  });
  for (const flecs::entity component : stale_singleton_tags) {
    component.remove(component);
  }

  if (auto applied = ApplyEntities(world, entities); !applied) {
    return applied;
  }

  for (const EntitySnapshot& s : entities) {
    const flecs::entity e = world.lookup(s.name.c_str());
    if (!e.has<flecs::Component>()) {
      e.add<StateEntity>();
    }
    PruneToSnapshot(e, s);
  }
  return {};
}

Status ApplyWorldStateDelta(flecs::world& world, const WorldSnapshot& snapshot) {
  const EntityRefs entities = AllEntities(snapshot);
  if (auto valid = ValidateEntities(world, entities); !valid) {
    return valid;
  }

  if (auto applied = ApplyEntities(world, entities); !applied) {
    return applied;
  }

  for (const EntitySnapshot& s : entities) {
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
  for (const EntitySnapshot& entity : AllEntities(snapshot)) {
    auto& flat_entity = *flat.entities.emplace_back(std::make_unique<fbs::state::EntitySnapshotT>());
    flat_entity.name = entity.name;
    for (const std::string& tag : entity.tags) {
      flat_entity.tags.emplace_back(std::make_unique<fbs::state::TagT>())->name = tag;
    }
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
    for (const auto& flat_tag : flat_entity->tags) {
      entity.tags.push_back(flat_tag->name);
    }
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

Status LoadWorldState(flecs::world& world, std::span<const uint8_t> bytes) {
  flatbuffers::Verifier verifier(bytes.data(), bytes.size());
  if (!fbs::state::VerifyWorldSnapshotBuffer(verifier)) {
    return std::unexpected("malformed WorldSnapshot buffer");
  }
  fbs::state::WorldSnapshotT flat;
  fbs::state::GetWorldSnapshot(bytes.data())->UnPackTo(&flat);
  return ApplyWorld(world, FromFlatbuffer(flat));
}

}  // namespace z13::flecs_tools
