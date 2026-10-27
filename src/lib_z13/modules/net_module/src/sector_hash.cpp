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

#include "sector_hash.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <tuple>
#include <unordered_map>

#include <Eigen/Core>

#include <lib_core/state/snapshot_grouping.h>
#include <lib_core/state/snapshot_hash.h>
#include <lib_core/utils/fnv_hash.h>

#include <primitives/placement.h>
#include <z13/components/player.h>

namespace z13::net {

namespace {

namespace ft = z13::flecs_tools;

uint64_t Add(uint64_t total, uint64_t hash) {
  return ft::MixHash(total ^ hash);
}

struct LocatedPlayer {
  uint32_t player_id {};
  std::optional<sectors::SectorKey> sector;
};

std::optional<LocatedPlayer> PlayerOfEntity(
    flecs::world world, const ft::EntitySnapshot& entity, const sectors::SectorIndex& index) {
  const flecs::entity found = world.lookup(entity.name.c_str());
  const auto* player = found ? found.try_get<z13::gameplay::Player>() : nullptr;
  if (!player) {
    return std::nullopt;
  }
  return LocatedPlayer {
      .player_id = player->id, .sector = index.Locate({.kind = sectors::MemberKind::kPlayer, .id = player->id})};
}

}  // namespace

SplitHashes SplitBySector(flecs::world world, const ft::WorldSnapshot& snapshot, const sectors::SectorIndex& index,
                          int chunk_cells) {
  SplitHashes result;
  // Entities come sorted by name and groups by id, so both peers add them in the same order.
  for (const ft::EntitySnapshot& entity : snapshot.entities) {
    const uint64_t hash = ft::HashEntitySnapshot(entity);
    const auto player = PlayerOfEntity(world, entity, index);
    if (player && player->sector) {
      result.by_sector[*player->sector].players.push_back({.player_id = player->player_id, .hash = hash});
    } else {
      result.global = Add(result.global, hash);
    }
  }
  for (const std::shared_ptr<const ft::GroupSnapshot>& group : snapshot.groups) {
    const Eigen::Vector3i chunk = z13::building::primitives::UnpackChunk(group->id);
    uint64_t& blocks = result.by_sector[sectors::SectorOf(chunk * chunk_cells, index.SectorCells())].blocks;
    blocks = Add(Add(blocks, group->id), group->fingerprint);
  }
  return result;
}

std::vector<fbs::net::SectorHashWire> HashesOf(const SplitHashes& split, const std::unordered_set<uint32_t>* players) {
  std::vector<fbs::net::SectorHashWire> hashes;
  for (const auto& [sector, parts] : split.by_sector) {
    uint64_t total = parts.blocks;
    for (const PlayerHash& player : parts.players) {
      if (!players || players->contains(player.player_id)) {
        total = Add(total, player.hash);
      }
    }
    hashes.emplace_back(sector.x(), sector.y(), sector.z(), total);
  }
  std::ranges::sort(hashes, [](const fbs::net::SectorHashWire& a, const fbs::net::SectorHashWire& b) {
    return std::tuple(a.x(), a.y(), a.z()) < std::tuple(b.x(), b.y(), b.z());
  });
  return hashes;
}

SectorHashes HashBySector(flecs::world world, const ft::WorldSnapshot& snapshot, const sectors::SectorIndex& index,
                          int chunk_cells) {
  SplitHashes split = SplitBySector(world, snapshot, index, chunk_cells);
  return {.global = split.global, .by_sector = HashesOf(split, nullptr)};
}

}  // namespace z13::net
