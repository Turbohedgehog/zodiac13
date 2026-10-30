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

#include "interest_system.h"

#include <unordered_set>

#include <lib_core/state/rollback.h>
#include <lib_core/state/world_state.h>
#include <lib_core/time/simulation_clock.h>
#include <lib_core/world/lifecycle.h>
#include <lib_profile/profile.h>

#include <sectors/interest_map.h>
#include <sectors/sector_index.h>
#include <z13/components/block_changes.h>
#include <z13/components/net.h>
#include <z13_settings/net_tuning.h>

#include "interest_events.h"
#include "net_session.h"
#include "sector_system.h"
#include "station_bounds_log.h"
#include "world_edits.h"

namespace z13::net {

namespace {

using sectors::ClientId;
using sectors::InterestMap;
using sectors::MemberKind;

void RegisterPipeline(flecs::world world) {
  // Final: an EntityEntered carries the player as the tick leaves it (gravity, interactions included).
  world.component<InterestPhase>().add(flecs::Phase).add<z13::flecs_tools::FinalStatePhase>().depends_on<SectorPhase>();
  // The edits are read from the changes of the tick, before they are cleared.
  world.component<z13::station::ClearBlockChangesPhase>().add(flecs::Phase).depends_on<InterestPhase>();
}

void RegisterComponents(flecs::world world) {
  z13::flecs_tools::RegisterComponents<InterestMap, InterestKnowledge>(world);
}

InterestChanges UpdateInterest(
    const NetSession& session, InterestMap& map, const sectors::SectorIndex& index, const z13::NetTuning& tuning) {
  Z13_PROFILE_ZONE("InterestSystem::Update");
  const sectors::InterestShape shape {.radius = tuning.interest_radius_sectors, .margin = tuning.interest_margin_sectors};
  std::unordered_set<ClientId> connected;
  InterestChanges changes;
  for (const auto& [connection, player] : session.PlayerConnections()) {
    connected.insert(player);
    if (const auto center = index.Locate({.kind = MemberKind::kPlayer, .id = player})) {
      changes.emplace(player, map.Update(player, *center, shape));
    }
  }
  for (const ClientId client : map.Clients()) {
    if (!connected.contains(client)) {
      map.Remove(client);
    }
  }
  return changes;
}

void RegisterSystems(flecs::world world) {
  world.set(InterestMap {});
  world.set(InterestKnowledge {});
  world.system<const NetSession, InterestMap, sectors::SectorIndex, const z13::NetTuning>(
          "InterestSystem::Update")
      .kind<InterestPhase>()
      .with<z13::net::ServerRole>()
      .each([](flecs::iter& it, size_t, const NetSession& session, InterestMap& map, sectors::SectorIndex& index,
               const z13::NetTuning& tuning) {
        // A replay revisits ticks the clients were told about; what it decided anew would land in their past.
        if (it.world().has<z13::flecs_tools::ReplayInProgress>()) {
          ResendChangedEntries(it.world());
        } else {
          SendInterestEvents(it.world(), UpdateInterest(session, map, index, tuning), index.TakeMovedPlayers());
        }
        SendWorldEdits(it.world());
        SendStationBounds(it.world());
      });
}

}  // namespace

void InterestSystem::Register(flecs::world& world) {
  z13::OnRegisterComponents(world, RegisterComponents);
  z13::OnInitPhases(world, RegisterPipeline);
  z13::OnInitSystems(world, RegisterSystems);
}

}  // namespace z13::net
