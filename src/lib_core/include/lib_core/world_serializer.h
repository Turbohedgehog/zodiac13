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

#pragma once

#include <expected>
#include <functional>
#include <string>
#include <vector>

#include <flecs.h>

namespace z13::flecs_tools {

// A data component and its value, serialized through flecs meta reflection.
struct ComponentValue {
  std::string type;   // component full path, e.g. "z13::Position"
  std::string value;  // flecs meta JSON, e.g. {"x":1, "y":2, "z":3}
};

// A flecs (relation, target) pair. `relation` is "ChildOf" for the parent
// relationship, otherwise the relation entity's full path.
struct Relationship {
  std::string relation;
  std::string target;  // target entity full path
};

struct EntitySnapshot {
  std::string name;                         // full "::"-separated path, no root
  std::vector<std::string> tags;            // zero-size components, sorted
  std::vector<ComponentValue> components;   // data components with meta, sorted by type
  std::vector<Relationship> relationships;  // sorted by (relation, target)
};

struct WorldSnapshot {
  std::vector<EntitySnapshot> entities;  // sorted by name
};

// Decides whether an entity belongs to the serialized state.
using EntityFilter = std::function<bool(flecs::entity)>;

// Decides whether a component (or tag) type is serialized; receives the type's entity.
using ComponentFilter = std::function<bool(flecs::entity)>;

// Named, alive, not under `flecs`, not a component/module definition.
bool DefaultEntityFilter(flecs::entity e);

// Reads matching entities into a snapshot. Component values go through flecs meta
// reflection, so no component type is named here — but every persisted component
// must have its meta registered (see component_meta.h).
WorldSnapshot CaptureWorld(const flecs::world& world, const EntityFilter& accept);
WorldSnapshot CaptureWorld(const flecs::world& world);  // accept = DefaultEntityFilter

// World state: StateEntity entities and state singletons, with only StateComponent
// types; relationships to non-state entities are dropped. Restore leaves a state
// singleton alone when the snapshot has no entry for it.
bool StateEntityFilter(flecs::entity e);
bool StateComponentFilter(flecs::entity component);
WorldSnapshot CaptureState(const flecs::world& world);

// Makes the world's state equal to the snapshot, updating in place: state entities,
// components and relationships it lacks are removed. Validated first; on error the
// world is left untouched.
std::expected<void, std::string> RestoreWorld(flecs::world& world, const WorldSnapshot& snapshot);

// Recreates the snapshot in a world whose components and their meta are already
// registered. Entities are matched/created by name; relationships are applied in
// a second pass once all targets exist.
void ApplyWorld(flecs::world& world, const WorldSnapshot& snapshot);

// Binary world state via reflect-cpp msgpack.
std::vector<char> SaveWorldState(const flecs::world& world, const EntityFilter& accept);
std::vector<char> SaveWorldState(const flecs::world& world);
bool LoadWorldState(flecs::world& world, const std::vector<char>& bytes);

// One entity's state (StateComponent-filtered, like CaptureState) as binary msgpack,
// for a delta join rather than a full snapshot (docs/client-server-plan.md's PlayerJoined).
std::vector<char> SaveEntityState(const flecs::world& world, flecs::entity entity);

// Applies a single-entity (or otherwise partial) state snapshot on top of the world,
// validated like RestoreWorld since it carries untrusted network input. Entities the
// snapshot doesn't mention are left alone.
std::expected<void, std::string> ApplyWorldStateDelta(flecs::world& world, const std::vector<char>& bytes);

// Binary world *state* (CaptureState/RestoreWorld, not the full unfiltered world) via
// reflect-cpp msgpack; see SaveWorldState/LoadWorldState for the full form QuickSave uses.
std::vector<char> SaveState(const flecs::world& world);
// Overload for a snapshot already captured elsewhere (e.g. WorldSnapshotHistory's
// periodic cache), avoiding CaptureState()'s full-world walk.
std::vector<char> SaveState(const WorldSnapshot& snapshot);
std::expected<void, std::string> LoadState(flecs::world& world, const std::vector<char>& bytes);

}  // namespace z13::flecs_tools
