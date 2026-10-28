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

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include <Eigen/Dense>
#include <flecs.h>

namespace z13::gameplay {

enum class StateChangeKind : uint8_t { kSet, kRemoved };

// The player whose command is being carried out, for the edits it makes (ActorScope). A system that derives
// state from state has no actor.
struct EditActor {
  using Singleton = void;
  std::optional<uint32_t> player_id;
};

// An edit of world state by a player's command, on an entity of its own until ClearBlockChangesPhase. Not state.
// The server tells the clients that don't simulate the actor (docs/network-scaling-plan.md, stage 5b). A removal
// keeps the entity's `name` and `cell`, since the entity is gone by the end of the tick; a set entity is found by
// `entity` then, when a deferred spawn has a name too.
struct StateChange {
  StateChangeKind kind {};
  flecs::entity_t entity {};
  std::string name;
  std::optional<Eigen::Vector3i> cell;
  uint32_t actor {};
};

// Sets the actor for its scope; the previous one comes back after. Takes the system's own term, since a singleton
// written through the world while a system is deferred would not be seen by the spawns it makes.
class ActorScope {
 public:
  ActorScope(EditActor& actor, uint32_t player_id) : actor_(actor), previous_(actor.player_id) {
    actor_.get().player_id = player_id;
  }
  ~ActorScope() { actor_.get().player_id = previous_; }
  ActorScope(const ActorScope&) = delete;
  ActorScope& operator=(const ActorScope&) = delete;

 private:
  std::reference_wrapper<EditActor> actor_;
  std::optional<uint32_t> previous_;
};

// Notes the edit of `entity` by the current actor, and nothing when none is acting.
inline void RecordStateChange(
    flecs::entity entity, StateChangeKind kind, const std::optional<Eigen::Vector3i>& cell = std::nullopt) {
  flecs::world world = entity.world();
  const auto* acting = world.try_get<EditActor>();
  if (!acting || !acting->player_id) {
    return;
  }
  world.entity().set(StateChange {
      .kind = kind,
      .entity = entity.id(),
      .name = kind == StateChangeKind::kRemoved ? std::string(entity.name().c_str()) : std::string(),
      .cell = cell,
      .actor = *acting->player_id});
}

}  // namespace z13::gameplay
