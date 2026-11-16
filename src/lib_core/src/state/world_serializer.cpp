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
#include <numeric>
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
#include <lib_core/state/snapshot_hash.h>
#include <lib_core/state/state_watch.h>
#include <lib_core/state/world_state.h>
#include <lib_core/utils/fnv_hash.h>
#include <lib_core/utils/status.h>
#include <lib_profile/profile.h>


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
  bool flat {};  // IsFlatValue: the raw bytes of a value stand for its encoding
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
  const bool flat = accepted && has_meta && IsFlatValue(component.world(), component.id());
  return components.infos.emplace(component.id(), ComponentInfo {std::move(path), accepted, has_meta, flat})
      .first->second;
}

// A cheap stand-in for encoding an entity: the raw bytes of its components, so equal fingerprints mean equal
// encodings. None when a component isn't flat. A pair's target is taken by id, so renaming a target is not noticed.
std::optional<uint64_t> FingerprintOf(const flecs::world& world, flecs::entity e, ComponentScope& components) {
  uint64_t hash = HashName(e.parent() ? PathOf(e) : std::string_view {e.name().c_str()});
  bool flat = true;
  e.each([&](flecs::id id) {
    if (!flat) {
      return;
    }
    if (id.is_pair()) {
      hash = MixHash(hash ^ id.raw_id());
      return;
    }
    const ComponentInfo& info = InfoOf(components, id.entity());
    if (!info.accepted) {
      return;
    }
    hash = MixHash(hash ^ id.raw_id());
    const ecs_type_info_t* type_info = ecs_get_type_info(world, id.raw_id());
    if (type_info == nullptr || type_info->size == 0 || !info.has_meta) {
      return;
    }
    const void* value = e.try_get(id.raw_id());
    if (value == nullptr) {
      return;
    }
    flat = info.flat;
    hash = FnvHash(std::span(static_cast<const std::byte*>(value), static_cast<size_t>(type_info->size)), hash);
  });
  return flat ? std::optional {hash} : std::nullopt;
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

OptionalGrouping ActiveGrouping(const SnapshotGrouping* grouping) {
  if (grouping == nullptr || !grouping->member || !grouping->make_layout) {
    return std::nullopt;
  }
  return std::cref(*grouping);
}

// The fingerprint of an entity FingerprintOf can't cover is the hash of its encoding.
uint64_t GroupFingerprintOf(const flecs::world& world, flecs::entity e, ComponentScope& components) {
  if (const std::optional<uint64_t> fingerprint = FingerprintOf(world, e, components)) {
    return *fingerprint;
  }
  const auto encoded = CaptureEntity(world, e, components, {});
  return encoded ? HashEntitySnapshot(*encoded) : 0;
}

// What a group publishes: from the encoded values, as raw bytes of two worlds' equal components may differ (padding).
uint64_t EncodedFingerprintOf(const std::vector<EntitySnapshot>& entities) {
  const uint64_t sum = std::accumulate(entities.begin(), entities.end(), uint64_t {}, [](uint64_t total, const auto& e) {
    return total + MixHash(HashEntitySnapshot(e));
  });
  return MixHash(sum ^ MixHash(entities.size()));
}

// What the group's members add up to in the index, to tell whether it changed since it was encoded.
uint64_t FingerprintOfGroup(const GroupIndexEntry& group) {
  return MixHash(group.sum ^ MixHash(group.members.size()));
}

void AddToIndex(SnapshotGroupingState& state, flecs::entity_t entity, const GroupedEntity& grouped) {
  GroupIndexEntry& group = state.index[grouped.group];
  group.sum += MixHash(grouped.fingerprint);
  group.members.insert(entity);
}

void RemoveFromIndex(SnapshotGroupingState& state, flecs::entity_t entity, const GroupedEntity& grouped) {
  const auto found = state.index.find(grouped.group);
  GroupIndexEntry& group = found->second;
  group.sum -= MixHash(grouped.fingerprint);
  group.members.erase(entity);
  if (group.members.empty()) {
    state.index.erase(found);
  }
}

// Only an entity whose table was written (or a new one) is fingerprinted again, a missing one is dropped. The member
// term is read only: an inout one would mark the component changed on every scan.
void UpdateGroupIndex(flecs::world world, const SnapshotGrouping& grouping, ComponentScope& components) {
  SnapshotGroupingState& state = *grouping.state_;
  const GroupLayout layout = grouping.make_layout(world);
  if (layout.key != state.layout_key) {
    state.grouped.clear();
    state.index.clear();
    state.layout_key = layout.key;
  }
  ++state.update;
  TableKey last_table {};
  bool last_changed {};
  world.query_builder()
      .with<StateEntity>()
      .with<flecs::Identifier>(flecs::Name)
      .with(*grouping.member).in()
      .build()
      .each([&](flecs::entity e) {
        if (!StateEntityFilter(e)) {
          return;
        }
        const TableKey table = TableKeyOf(e);
        if (table != last_table) {
          last_table = table;
          last_changed = state.watch.TableChanged(table);
        }
        const auto known = state.grouped.find(e.id());
        if (known != state.grouped.end() && known->second.table == table && !last_changed) {
          known->second.seen = state.update;
          return;
        }
        ++state.fingerprints_made;
        const GroupedEntity now {
            .group = layout.group_of(e),
            .fingerprint = GroupFingerprintOf(world, e, components),
            .table = table,
            .seen = state.update,
        };
        if (known != state.grouped.end()) {
          RemoveFromIndex(state, e.id(), known->second);
        }
        AddToIndex(state, e.id(), now);
        state.grouped.insert_or_assign(e.id(), now);
      });
  std::erase_if(state.grouped, [&state](const auto& entry) {
    if (entry.second.seen == state.update) {
      return false;
    }
    RemoveFromIndex(state, entry.first, entry.second);
    return true;
  });
}

// The groups of the index, each encoded again only when its fingerprint differs from the last capture's.
Status EncodeGroups(
    const flecs::world& world, const SnapshotGrouping& grouping, ComponentScope& components,
    const EntityFilter& accept_target) {
  SnapshotGroupingState& state = *grouping.state_;
  flecs::world mutable_world = world;
  std::map<SnapshotGroupId, std::shared_ptr<const GroupSnapshot>> captured;
  std::map<SnapshotGroupId, uint64_t> keys;
  for (const auto& [id, entry] : state.index) {
    const uint64_t key = FingerprintOfGroup(entry);
    keys.emplace(id, key);
    if (const auto same = state.captured.find(id);
        same != state.captured.end() && state.captured_keys.contains(id) && state.captured_keys.at(id) == key) {
      captured.emplace(id, same->second);
      continue;
    }

    GroupSnapshot group {.id = id};
    for (const flecs::entity_t member : entry.members) {
      auto entity = CaptureEntity(world, mutable_world.entity(member), components, accept_target);
      if (!entity) {
        return std::unexpected(std::move(entity.error()));
      }
      group.entities.push_back(std::move(*entity));
    }
    SortByName(group.entities);
    group.fingerprint = EncodedFingerprintOf(group.entities);
    captured.emplace(id, std::make_shared<const GroupSnapshot>(std::move(group)));
  }
  state.captured = std::move(captured);
  state.captured_keys = std::move(keys);
  return {};
}

Status CaptureGroups(
    flecs::world world, const SnapshotGrouping& grouping, ComponentScope& components,
    const EntityFilter& accept_target, WorldSnapshot& snapshot) {
  UpdateGroupIndex(world, grouping, components);
  if (auto encoded = EncodeGroups(world, grouping, components, accept_target); !encoded) {
    grouping.state_->captured.clear();
    return encoded;
  }
  for (const auto& [id, group] : grouping.state_->captured) {
    snapshot.groups.push_back(group);
  }
  return {};
}

// The named entities outside the groups. One whose table was not written since the last capture is taken from it
// as it was; the others are fingerprinted, and encoded again only when the fingerprint differs.
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
  SnapshotGroupingState* const cache = grouping ? grouping->get().state_.get() : nullptr;

  const uint64_t update = cache ? ++cache->ungrouped_update : 0;
  TableKey last_table {};  // entities come table by table, so the watch is asked once per table
  bool last_changed {};
  Status captured;
  ungrouped.build().each([&](flecs::entity e) {
    if (!captured || (accept && !accept(e))) {
      return;
    }

    const TableKey table = cache ? TableKeyOf(e) : TableKey {};
    std::optional<uint64_t> hash;
    if (cache) {
      if (table != last_table) {
        last_table = table;
        last_changed = cache->watch.TableChanged(table);
      }
      const auto known = cache->ungrouped.find(e.id());
      const bool have = known != cache->ungrouped.end();
      if (have && known->second.table == table && !last_changed) {
        known->second.seen = update;
        snapshot.entities.push_back(known->second.snapshot);
        return;
      }
      hash = FingerprintOf(world, e, components);
      if (have && hash && known->second.hash == hash) {
        known->second.table = table;
        known->second.seen = update;
        snapshot.entities.push_back(known->second.snapshot);
        return;
      }
    }
    auto entity = CaptureEntity(world, e, components, accept_target);
    if (!entity) {
      captured = std::unexpected(std::move(entity.error()));
      return;
    }
    SharedEntity shared = ShareEntity(std::move(*entity));
    if (cache) {
      cache->ungrouped.insert_or_assign(
          e.id(), CachedEntity {.hash = hash, .table = table, .snapshot = shared, .seen = update});
    }
    snapshot.entities.push_back(std::move(shared));
  });
  SortByName(snapshot.entities);
  if (cache) {
    if (captured) {
      std::erase_if(cache->ungrouped, [update](const auto& entry) { return entry.second.seen != update; });
    } else {
      cache->ungrouped.clear();
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
  if (grouping) {
    grouping->get().state_->watch.Refresh(w);
  }
  Status captured = CaptureUngrouped(w, accept, components, accept_target, grouping, state_only, snapshot);
  if (captured && grouping) {
    captured = CaptureGroups(w, *grouping, components, accept_target, snapshot);
  }
  if (captured && grouping) {
    grouping->get().state_->watch.MarkCaptured();
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

GroupIds MatchingGroups(const WorldSnapshot& snapshot, OptionalGrouping grouping) {
  GroupIds matching;
  if (!grouping) {
    return matching;
  }
  const SnapshotGroupingState& state = *grouping->get().state_;
  for (const auto& group : snapshot.groups) {
    const auto found = state.index.find(group->id);
    const auto mine = state.captured.find(group->id);
    const auto key = state.captured_keys.find(group->id);
    if (found != state.index.end() && mine != state.captured.end() && key != state.captured_keys.end() &&
        key->second == FingerprintOfGroup(found->second) && mine->second->fingerprint == group->fingerprint) {
      matching.insert(group->id);
    }
  }
  return matching;
}

// The state entities the snapshot lacks: the ungrouped ones are visited, the grouped ones come from the index, and
// the matching groups are skipped.
std::vector<flecs::entity> StaleEntities(flecs::world& world, OptionalGrouping grouping, const GroupIds& matching,
                                         const std::unordered_set<std::string>& names) {
  std::vector<flecs::entity> stale;
  const auto is_stale = [&names](flecs::entity e) {
    return StateEntityFilter(e) && e.name().length() > 0 && !names.contains(PathOf(e));
  };
  auto visited = world.query_builder();
  visited.with<StateEntity>();
  if (grouping) {
    visited.without(*grouping->get().member);
  }
  visited.build().each([&](flecs::entity e) {
    if (is_stale(e)) {
      stale.push_back(e);
    }
  });
  if (grouping) {
    for (const auto& [id, group] : grouping->get().state_->index) {
      if (matching.contains(id)) {
        continue;
      }
      for (const flecs::entity_t member : group.members) {
        if (const flecs::entity e = world.entity(member); e.is_alive() && is_stale(e)) {
          stale.push_back(e);
        }
      }
    }
  }
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
  if (grouping) {
    ComponentScope components {.accept = StateComponentFilter};
    grouping->get().state_->watch.Refresh(world);
    UpdateGroupIndex(world, *grouping, components);
  }
  const GroupIds matching = MatchingGroups(snapshot, grouping);
  const EntityRefs entities = EntitiesToApply(snapshot, matching);
  if (auto valid = ValidateEntities(world, entities); !valid) {
    return valid;
  }

  std::unordered_set<std::string> names;
  for (const EntitySnapshot& s : entities) {
    names.insert(s.name);
  }

  const std::vector<flecs::entity> stale_entities = StaleEntities(world, grouping, matching, names);
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
