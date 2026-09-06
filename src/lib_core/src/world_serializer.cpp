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
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

#include <lib_core/log.h>

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

void CaptureComponentsAndTags(const flecs::world& world, flecs::entity e, EntitySnapshot& s) {
  e.each([&](flecs::id id) {
    if (!id.is_entity()) {
      return;  // pairs handled separately
    }

    std::string path = PathOf(id.entity());
    if (IsUnderFlecs(path)) {
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

void CaptureRelationships(flecs::entity e, EntitySnapshot& s) {
  e.each([&](flecs::id id) {
    if (!id.is_pair()) {
      return;
    }

    const flecs::entity relation = id.first();
    const flecs::entity target = id.second();
    if (!target || !target.is_alive()) {
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

WorldSnapshot CaptureWorld(const flecs::world& world, const EntityFilter& accept) {
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
        CaptureComponentsAndTags(w, e, s);
        CaptureRelationships(e, s);
        snapshot.entities.push_back(std::move(s));
      });

  std::sort(snapshot.entities.begin(), snapshot.entities.end(),
            [](const EntitySnapshot& a, const EntitySnapshot& b) { return a.name < b.name; });

  return snapshot;
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

std::vector<char> SaveWorldState(const flecs::world& world, const EntityFilter& accept) {
  return rfl::msgpack::write(CaptureWorld(world, accept));
}

std::vector<char> SaveWorldState(const flecs::world& world) {
  return SaveWorldState(world, DefaultEntityFilter);
}

bool LoadWorldState(flecs::world& world, const std::vector<char>& bytes) {
  auto result = rfl::msgpack::read<WorldSnapshot>(bytes);
  if (!result) {
    LOG_ERROR("z13::LoadWorldState: {}", result.error().what());
    return false;
  }

  ApplyWorld(world, result.value());
  return true;
}

}  // namespace z13::flecs_tools
