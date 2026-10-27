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

#include <memory>
#include <utility>

#include <Eigen/Core>

#include <primitives/placement.h>
#include <sectors/sector_index.h>
#include <z13/components/player.h>
#include <z13_settings/building_tuning.h>
#include <z13_settings/net_tuning.h>

namespace z13::net {

namespace {

// A player's reach is within one sector (enforced by net.interest_radius_sectors' rule).
constexpr int kPresenceRing = 1;

InterestView ViewOfSectors(
    flecs::world world, sectors::ClientId client, SectorSet subscribed, const sectors::SectorBox& bounds) {
  InterestView view;
  view.sectors = std::move(subscribed);
  view.presence = PresenceOf(bounds);
  const auto& index = world.get<sectors::SectorIndex>();
  for (const sectors::SectorKey& sector : view.presence.Sectors()) {
    for (const sectors::SectorMember& member : index.MembersIn(sector)) {
      if (member.kind == sectors::MemberKind::kPlayer) {
        view.players.insert(static_cast<uint32_t>(member.id));
      }
    }
  }
  view.players.insert(client);
  return view;
}

}  // namespace

sectors::SectorBox PresenceOf(const sectors::SectorBox& subscribed) {
  return subscribed.Grown(kPresenceRing);
}

InterestView ViewAround(flecs::world world, sectors::ClientId client, const sectors::SectorKey& center) {
  const auto& tuning = world.get<z13::NetTuning>();
  const int reach = tuning.interest_radius_sectors + tuning.interest_margin_sectors;
  const sectors::SectorBox bounds {
      .min = center - sectors::SectorKey::Constant(reach), .max = center + sectors::SectorKey::Constant(reach)};
  const std::vector<sectors::SectorKey> subscribed = bounds.Sectors();
  return ViewOfSectors(world, client, SectorSet {subscribed.begin(), subscribed.end()}, bounds);
}

std::optional<InterestView> ViewOf(flecs::world world, sectors::ClientId client) {
  const auto& map = world.get<sectors::InterestMap>();
  const std::optional<sectors::SectorBox> bounds = map.BoundsOf(client);
  if (!bounds) {
    return std::nullopt;
  }
  const std::vector<sectors::SectorKey> subscribed = map.SectorsOf(client);
  return ViewOfSectors(world, client, SectorSet {subscribed.begin(), subscribed.end()}, *bounds);
}

z13::flecs_tools::WorldSnapshot FilterSnapshot(
    flecs::world world, const z13::flecs_tools::WorldSnapshot& snapshot, const InterestView& view) {
  const int chunk_cells = world.get<z13::BuildingTuning>().index_chunk_cells;
  const int sector_cells = world.get<z13::NetTuning>().sector_size_cells;
  z13::flecs_tools::WorldSnapshot filtered;
  for (const z13::flecs_tools::EntitySnapshot& entity : snapshot.entities) {
    const flecs::entity found = world.lookup(entity.name.c_str());
    const auto* player = found ? found.try_get<z13::gameplay::Player>() : nullptr;
    if (!player || view.players.contains(player->id)) {
      filtered.entities.push_back(entity);
    }
  }
  for (const auto& group : snapshot.groups) {
    const auto chunk = z13::building::primitives::UnpackChunk(group->id);
    if (view.sectors.contains(sectors::SectorOf(chunk * chunk_cells, sector_cells))) {
      filtered.groups.push_back(group);
    }
  }
  return filtered;
}

}  // namespace z13::net
