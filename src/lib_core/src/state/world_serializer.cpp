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
#include <lib_profile/profile.h>

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

// What a capture learned about a component type; building a path takes a string, so it is built once per capture.
struct ComponentInfo {
  std::string path;
  bool accepted {};
  bool has_meta {};
};

struct ComponentScope {
  ComponentFilter accept;
  std::unordered_map<flecs::entity_t, ComponentInfo> infos;
};

const ComponentInfo& InfoOf(ComponentScope& components, flecs::entity component) {
  if (const auto known = components.infos.find(component.id()); known != components.infos.end()) {
    return known->second;
  }
  std::string path = PathOf(component);
  const bool accepted = !IsUnderFlecs(path) && (!components.accept || components.accept(component));
  const bool has_meta = component.has(ecs_id(EcsType));
  return components.infos.emplace(component.id(), ComponentInfo {std::move(path), accepted, has_meta}).first->second;
}

Status CaptureComponentsAndTags(
    const flecs::world& world, flecs::entity e, ComponentScope& components, EntitySnapshot& s) {
  Status captured;
  e.each([&](flecs::id id) {
    if (!captured || !id.is_entity()) {
      return;  // pairs handled separately
    }

    const ComponentInfo& info = InfoOf(components, id.entity());
    if (!info.accepted) {
      return;
    }

    // Not try_get: flecs aborts on a tag whose id is past its low component ids.
    const ecs_type_info_t* type_info = ecs_get_type_info(world, id.raw_id());
    if (type_info == nullptr || type_info->size == 0) {
      s.tags.push_back(info.path);
      return;
    }
    const void* value = e.try_get(id.raw_id());
    if (value == nullptr) {
      return;
    }

    if (!info.has_meta) {
      return;  // data component without meta: cannot round-trip
    }
    auto bytes = ComponentBytes(world, id.raw_id(), value).and_then([&](std::span<const std::byte> view) {
      return EncodeValue(world, id.raw_id(), view);
    });
    if (!bytes) {
      captured = std::unexpected(std::format("'{}' on '{}': {}", info.path, s.name, bytes.error()));
      return;
    }
    s.components.push_back({info.path, std::move(*bytes)});
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

// Bare test worlds have no WorldRestores.
void CountRestore(flecs::world& world) {
  if (auto* restores = world.try_get_mut<WorldRestores>()) {
    ++restores->count;
  }
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
    const flecs::world& world, flecs::entity e, ComponentScope& components,
    const EntityFilter& accept_target) {
  EntitySnapshot s;
  s.name = PathOf(e);
  if (auto captured = CaptureComponentsAndTags(world, e, components, s); !captured) {
    return std::unexpected(std::move(captured.error()));
  }
  CaptureRelationships(e, accept_target, s);
  return s;
}

void SortByName(std::vector<EntitySnapshot>& entities) {
  std::sort(entities.begin(), entities.end(),
            [](const EntitySnapshot& a, const EntitySnapshot& b) { return a.name < b.name; });
}

bool ByName(const SharedEntity& a, const SharedEntity& b) {
  return a->name < b->name;
}

void SortByName(SharedEntities& entities) {
  std::sort(entities.begin(), entities.end(), ByName);
}

EntityRefs RefsOf(const SharedEntities& entities) {
  EntityRefs refs;
  std::ranges::transform(entities, std::back_inserter(refs), [](const SharedEntity& e) { return std::cref(*e); });
  return refs;
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
  const GroupOf group_of = grouping.make_group_of(world);
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
    const flecs::world& world, const SnapshotGrouping& grouping, ComponentScope& components,
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
      auto entity = CaptureEntity(world, member, components, accept_target);
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
    const flecs::world& world, const SnapshotGrouping& grouping, ComponentScope& components,
    const EntityFilter& accept_target, WorldSnapshot& snapshot) {
  auto& captured = grouping.state_->captured;
  const bool unchanged = grouping.unchanged && grouping.unchanged(world);
  if (!unchanged || captured.empty()) {
    if (auto encoded = EncodeGroups(world, grouping, components, accept_target); !encoded) {
      captured.clear();  // `unchanged` already moved on
      return encoded;
    }
  }
  for (const auto& [id, group] : captured) {
    snapshot.groups.push_back(group);
  }
  return {};
}

bool HasHashedMember(flecs::entity e, const SnapshotGrouping& grouping) {
  return std::ranges::any_of(grouping.hashed_members, [&e](flecs::entity_t member) { return e.has(member); });
}

// The named entities outside the groups. The fingerprinted ones are encoded again only when their fingerprint
// differs from the last capture's, and when no hashed member changed they are not even visited.
Status CaptureUngrouped(
    flecs::world world, const EntityFilter& accept, ComponentScope& components,
    const EntityFilter& accept_target, OptionalGrouping grouping, bool state_only, WorldSnapshot& snapshot) {
  auto ungrouped = world.query_builder();
  ungrouped.with<flecs::Identifier>(flecs::Name);  // every named entity
  if (state_only) {
    // What StateEntityFilter can accept; the other named entities (systems, queries) are never visited.
    ungrouped.with<StateEntity>().or_().with<flecs::Component>();
  }
  if (grouping) {
    ungrouped.without(*grouping->get().member);
  }
  const auto hash_ungrouped = grouping ? grouping->get().hash_ungrouped : nullptr;
  SnapshotGroupingState* const cache = hash_ungrouped ? grouping->get().state_.get() : nullptr;
  // The watch is asked on every capture: asking consumes the change.
  const bool members_unchanged = cache && grouping->get().hashed_unchanged && grouping->get().hashed_unchanged(world);
  const bool reuse_members = members_unchanged && cache->members_cached;

  std::unordered_map<flecs::entity_t, CachedEntity> kept;
  SharedEntities visited;  // what the scan found; the members come ready in the cache when they are reused
  SharedEntities members;  // the fingerprinted members among them, to be kept sorted
  if (reuse_members) {
    for (const flecs::entity_t member : grouping->get().hashed_members) {
      ungrouped.without(member);
    }
  }

  Status captured;
  bool members_complete = true;
  ungrouped.build().each([&](flecs::entity e) {
    if (!captured || (accept && !accept(e))) {
      return;
    }

    const std::optional<uint64_t> hash = hash_ungrouped ? hash_ungrouped(e) : std::nullopt;
    const bool member = cache && HasHashedMember(e, *grouping);
    members_complete = members_complete && (hash || !member);
    if (hash) {
      const auto same = cache->ungrouped.find(e.id());
      if (same != cache->ungrouped.end() && same->second.hash == *hash) {
        visited.push_back(same->second.snapshot);
        if (member) {
          members.push_back(same->second.snapshot);
        }
        kept.emplace(e.id(), std::move(same->second));
        return;
      }
    }
    auto entity = CaptureEntity(world, e, components, accept_target);
    if (!entity) {
      captured = std::unexpected(std::move(entity.error()));
      return;
    }
    SharedEntity shared = ShareEntity(std::move(*entity));
    if (hash) {
      kept.emplace(e.id(), CachedEntity {.hash = *hash, .snapshot = shared, .member = member});
      if (member) {
        members.push_back(shared);
      }
    }
    visited.push_back(std::move(shared));
  });
  SortByName(visited);
  if (reuse_members) {
    std::ranges::merge(cache->sorted_members, visited, std::back_inserter(snapshot.entities), ByName);
  } else {
    snapshot.entities = std::move(visited);
  }
  if (cache) {
    // The entries taken from the cache were moved out of it; after a failure it starts over.
    if (reuse_members && captured) {
      std::erase_if(cache->ungrouped, [](const auto& entry) { return !entry.second.member; });
      cache->ungrouped.merge(kept);
    } else {
      cache->ungrouped = captured ? std::move(kept) : decltype(kept) {};
    }
    cache->members_cached = captured && (reuse_members || members_complete);
    if (!reuse_members) {
      SortByName(members);
      cache->sorted_members = captured && members_complete ? std::move(members) : SharedEntities {};
    }
  }
  return captured;
}

std::expected<WorldSnapshot, std::string> CaptureImpl(
    const flecs::world& world, const EntityFilter& accept, const ComponentFilter& accept_component,
    const EntityFilter& accept_target, OptionalGrouping grouping = std::nullopt, bool state_only = false) {
  WorldSnapshot snapshot;
  flecs::world w = world;
  ComponentScope components {.accept = accept_component};
  Status captured = CaptureUngrouped(w, accept, components, accept_target, grouping, state_only, snapshot);
  if (captured && grouping) {
    captured = CaptureGroups(w, *grouping, components, accept_target, snapshot);
  }

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

bool SameEntities(const SharedEntities& a, const SharedEntities& b) {
  return std::ranges::equal(a, b, [](const SharedEntity& x, const SharedEntity& y) { return x == y || *x == *y; });
}

EntityRefs AllEntities(const WorldSnapshot& snapshot) {
  EntityRefs all = RefsOf(snapshot.entities);
  for (const auto& group : snapshot.groups) {
    all.insert(all.end(), group->entities.begin(), group->entities.end());
  }
  return all;
}

Status ApplyWorld(flecs::world& world, const WorldSnapshot& snapshot) {
  auto applied = ApplyEntities(world, AllEntities(snapshot));
  if (applied) {
    CountRestore(world);
  }
  return applied;
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
  Z13_PROFILE_ZONE("CaptureState");
  return CaptureImpl(
      world, StateEntityFilter, StateComponentFilter, StateEntityFilter,
      ActiveGrouping(world.try_get<SnapshotGrouping>()), true);
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

// Whether the groups of the last capture are the world's groups now. The check consumes the
// change `unchanged` reports, so a world it finds changed drops them: the next capture re-encodes.
bool CapturedIsCurrent(flecs::world& world, const SnapshotGrouping& grouping) {
  auto& captured = grouping.state_->captured;
  const bool current = grouping.unchanged && grouping.unchanged(world) && !captured.empty();
  if (!current) {
    captured.clear();
  }
  return current;
}

GroupIds MatchingGroups(
    flecs::world& world, const WorldSnapshot& snapshot, OptionalGrouping grouping, bool captured_is_current) {
  GroupIds matching;
  if (snapshot.groups.empty() || !grouping) {
    return matching;
  }
  if (captured_is_current) {
    const auto& captured = grouping->get().state_->captured;
    for (const auto& group : snapshot.groups) {
      if (const auto same = captured.find(group->id);
          same != captured.end() && same->second->fingerprint == group->fingerprint) {
        matching.insert(group->id);
      }
    }
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

// The state entities the snapshot lacks. With the captured groups current, the entities of the
// matching groups are skipped without being visited and the others are listed from the capture.
std::vector<flecs::entity> StaleEntities(flecs::world& world, OptionalGrouping grouping, const GroupIds& matching,
                                         const std::unordered_set<std::string>& names, bool captured_is_current) {
  std::vector<flecs::entity> stale;
  const auto is_stale = [&names](flecs::entity e) {
    return StateEntityFilter(e) && e.name().length() > 0 && !names.contains(PathOf(e));
  };
  if (captured_is_current && grouping) {
    world.query_builder().with<StateEntity>().without(*grouping->get().member).build().each([&](flecs::entity e) {
      if (is_stale(e)) {
        stale.push_back(e);
      }
    });
    for (const auto& [id, group] : grouping->get().state_->captured) {
      if (matching.contains(id)) {
        continue;
      }
      for (const EntitySnapshot& member : group->entities) {
        if (const flecs::entity e = world.lookup(member.name.c_str()); e && is_stale(e)) {
          stale.push_back(e);
        }
      }
    }
    return stale;
  }

  const GroupOf group_of = matching.empty() ? GroupOf {} : grouping->get().make_group_of(world);
  const auto in_matching_group = [&](flecs::entity e) {
    return group_of && e.has(*grouping->get().member) && matching.contains(group_of(e));
  };
  world.query_builder().with<StateEntity>().build().each([&](flecs::entity e) {
    if (!in_matching_group(e) && is_stale(e)) {
      stale.push_back(e);
    }
  });
  return stale;
}

EntityRefs EntitiesToApply(const WorldSnapshot& snapshot, const GroupIds& matching) {
  EntityRefs entities = RefsOf(snapshot.entities);
  for (const auto& group : snapshot.groups) {
    if (!matching.contains(group->id)) {
      entities.insert(entities.end(), group->entities.begin(), group->entities.end());
    }
  }
  return entities;
}

}  // namespace

Status RestoreWorld(flecs::world& world, const WorldSnapshot& snapshot) {
  Z13_PROFILE_ZONE("RestoreWorld");
  const OptionalGrouping grouping = ActiveGrouping(world.try_get<SnapshotGrouping>());
  const bool captured_is_current = grouping && CapturedIsCurrent(world, *grouping);
  const GroupIds matching = MatchingGroups(world, snapshot, grouping, captured_is_current);
  const EntityRefs entities = EntitiesToApply(snapshot, matching);
  if (auto valid = ValidateEntities(world, entities); !valid) {
    return valid;
  }

  std::unordered_set<std::string> names;
  for (const EntitySnapshot& s : entities) {
    names.insert(s.name);
  }

  const std::vector<flecs::entity> stale_entities = StaleEntities(world, grouping, matching, names, captured_is_current);
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
  CountRestore(world);
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
  CountRestore(world);
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
    EntitySnapshot entity;
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
    snapshot.entities.push_back(ShareEntity(std::move(entity)));
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
