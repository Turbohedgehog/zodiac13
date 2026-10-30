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
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <flecs.h>

#include <lib_core/state/world_serializer.h>
#include <z13/components/player_action.h>

#include <net_module/protocol.h>

#include "interest_view.h"
#include "station_bounds_log.h"

namespace z13::net {

struct CatchUpPayload {
  uint64_t server_tick {};
  uint64_t snapshot_tick {};
  z13::flecs_tools::WorldSnapshot snapshot;
  std::vector<z13::gameplay::PlayerActionRecord> actions;
  std::vector<z13::gameplay::PlayerActionRecord> held_values;
  std::vector<z13::gameplay::PlayerActionRecord> pending;
  z13::gameplay::ConfirmedInputTicks confirmed;
  // Under interest, the sectors whose blocks the snapshot holds; otherwise the whole world.
  std::optional<SectorSet> sectors;
  BoundsByTick station_bounds;
};

// With a view, only what the view holds: its players and sectors, and their input.
std::expected<std::unique_ptr<fbs::net::CatchUpT>, std::string> MakeCatchUp(
    flecs::world world, const std::optional<InterestView>& view = std::nullopt);

// One player's input, for a peer that has none of it (docs/interest-design.md): what CatchUp
// carries for everyone.
struct PlayerInput {
  std::vector<z13::gameplay::PlayerActionRecord> held_values;  // on the snapshot's tick
  std::vector<z13::gameplay::PlayerActionRecord> pending;
  z13::gameplay::ConfirmedInputTicks confirmed;
};

// Everyone's input at one snapshot tick, by player, to take several players' out of.
struct InputAtTick {
  std::vector<z13::gameplay::PlayerActionRecord> held_values;
  std::vector<z13::gameplay::PlayerActionRecord> pending;
};

InputAtTick InputAt(flecs::world world, uint64_t snapshot_tick);
PlayerInput InputOf(flecs::world world, const InputAtTick& everyone, uint32_t player_id);
PlayerInput InputOf(flecs::world world, uint32_t player_id, uint64_t snapshot_tick);
// Adds what the log and the queue lack, so applying it again on a replay changes nothing.
void MergePlayerInput(flecs::world world, PlayerInput input);

std::expected<CatchUpPayload, std::string> DecodeCatchUp(const std::unique_ptr<fbs::net::CatchUpT>& catch_up);
// The span comes from the network, so it is bounded.
bool IsPlausibleCatchUp(flecs::world world, uint64_t snapshot_tick, uint64_t target_tick);
// Replaces the local log, queue and snapshots: on a resync they are what diverged.
void AdoptCatchUp(flecs::world world, CatchUpPayload payload, uint64_t target_tick);

}  // namespace z13::net
