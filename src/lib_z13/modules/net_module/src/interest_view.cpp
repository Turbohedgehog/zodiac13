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

#include "interest_view.h"

#include <algorithm>
#include <iterator>
#include <memory>
#include <utility>
#include <vector>

#include <Eigen/Core>

#include <sectors/sector_index.h>
#include <z13/components/player.h>
#include <z13_settings/net_tuning.h>

#include "chunk_reach.h"
#include "interest_events.h"
#include "player_positions.h"
#include "simulated_set.h"

namespace z13::net {

namespace {

// The players of a view under a budget: those already simulated, or for a joiner the nearest to where it spawns.
std::unordered_set<uint32_t> Simulated(
    flecs::world world, const std::vector<uint32_t>& present, const std::unordered_set<uint32_t>* current,
    const Eigen::Vector3f& from) {
  if (current) {
    std::unordered_set<uint32_t> kept;
    std::ranges::copy_if(present, std::inserter(kept, kept.end()), [&](uint32_t id) { return current->contains(id); });
    return kept;
  }
  const auto& positions = CurrentPlayerPositions(world).by_player;
  std::vector<SimulatedCandidate> candidates;
  for (const uint32_t id : present) {
    if (const auto position = positions.find(id); position != positions.end()) {
      candidates.push_back({.player_id = id, .distance_m = (position->second - from).norm()});
    }
  }
  const auto& tuning = world.get<z13::NetTuning>();
  return ChooseSimulated(
      candidates, {},
      {.max_players = tuning.simulated_players_max,
       .slack = tuning.simulated_players_slack,
       .radius_m = tuning.simulated_radius_m,
       .hysteresis_m = tuning.simulated_hysteresis_m});
}

// `current` is what the client simulates now, when the server knows; `from` is where it stands or will.
InterestView ViewOfSectors(
    flecs::world world, sectors::ClientId client, SectorSet subscribed, const std::unordered_set<uint32_t>* current,
    const Eigen::Vector3f& from) {
  InterestView view;
  view.sectors = std::move(subscribed);
  const auto& index = world.get<sectors::SectorIndex>();
  std::vector<uint32_t> present;
  for (const sectors::SectorKey& sector : view.sectors) {
    for (const sectors::SectorMember& member : index.MembersIn(sector)) {
      if (member.kind == sectors::MemberKind::kPlayer) {
        present.push_back(static_cast<uint32_t>(member.id));
      }
    }
  }
  if (world.get<z13::NetTuning>().ghosts) {
    view.players = Simulated(world, present, current, from);
  } else {
    view.players.insert(present.begin(), present.end());
  }
  view.players.insert(client);
  return view;
}

}  // namespace

InterestView ViewAround(
    flecs::world world, sectors::ClientId client, const sectors::SectorKey& center, const Eigen::Vector3f& spawn) {
  const auto& tuning = world.get<z13::NetTuning>();
  const int reach = tuning.interest_radius_sectors + tuning.interest_margin_sectors;
  const sectors::SectorBox bounds {
      .min = center - sectors::SectorKey::Constant(reach), .max = center + sectors::SectorKey::Constant(reach)};
  const std::vector<sectors::SectorKey> subscribed = bounds.Sectors();
  return ViewOfSectors(world, client, SectorSet {subscribed.begin(), subscribed.end()}, nullptr, spawn);
}

std::optional<InterestView> ViewOf(flecs::world world, sectors::ClientId client) {
  const auto& map = world.get<sectors::InterestMap>();
  if (!map.BoundsOf(client)) {
    return std::nullopt;
  }
  const std::vector<sectors::SectorKey> subscribed = map.SectorsOf(client);
  const auto& knowledge = world.get<InterestKnowledge>().by_client;
  const auto known = knowledge.find(client);
  const auto& positions = CurrentPlayerPositions(world).by_player;
  const auto own = positions.find(client);
  return ViewOfSectors(
      world, client, SectorSet {subscribed.begin(), subscribed.end()},
      known == knowledge.end() ? nullptr : &known->second.simulated,
      own == positions.end() ? Eigen::Vector3f::Zero().eval() : own->second);
}

z13::flecs_tools::WorldSnapshot FilterSnapshot(
    flecs::world world, const z13::flecs_tools::WorldSnapshot& snapshot, const InterestView& view) {
  z13::flecs_tools::WorldSnapshot filtered;
  for (const z13::flecs_tools::EntitySnapshot& entity : snapshot.entities) {
    const flecs::entity found = world.lookup(entity.name.c_str());
    const auto* player = found ? found.try_get<z13::gameplay::Player>() : nullptr;
    if (!player || view.players.contains(player->id)) {
      filtered.entities.push_back(entity);
    }
  }
  const ChunkReach reach = ChunkReach::Of(world);
  std::ranges::copy_if(snapshot.groups, std::back_inserter(filtered.groups),
                       [&](const auto& group) { return reach.TouchesAny(group->id, view.sectors); });
  return filtered;
}

}  // namespace z13::net
