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

#include <primitives/block_group.h>
#include <primitives/placement.h>
#include <z13/components/gameplay/player.h>

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

// By snapshot name: a lookup of each of thousands of entities by path costs more than a walk over the few players.
using PlayersByName = std::unordered_map<std::string, LocatedPlayer>;

PlayersByName PlayersOf(flecs::world world, const sectors::SectorIndex& index) {
  PlayersByName players;
  world.query_builder<const z13::gameplay::Player>().build().each([&](flecs::entity e, const z13::gameplay::Player& player) {
    players.emplace(
        ft::SnapshotNameOf(e),
        LocatedPlayer {
            .player_id = player.id, .sector = index.Locate({.kind = sectors::MemberKind::kPlayer, .id = player.id})});
  });
  return players;
}

}  // namespace

SplitHashes SplitBySector(flecs::world world, const ft::WorldSnapshot& snapshot, const sectors::SectorIndex& index,
                          int chunk_cells) {
  SplitHashes result;
  const PlayersByName players = PlayersOf(world, index);
  // Entities come sorted by name and groups by id, so both peers add them in the same order.
  for (const ft::SharedEntity& shared : snapshot.entities) {
    const ft::EntitySnapshot& entity = *shared;
    const uint64_t hash = ft::HashEntitySnapshot(shared);
    const auto player = players.find(entity.name);
    if (player != players.end() && player->second.sector) {
      result.by_sector[*player->second.sector].players.push_back({.player_id = player->second.player_id, .hash = hash});
    } else {
      result.global = Add(result.global, hash);
    }
  }
  for (const std::shared_ptr<const ft::GroupSnapshot>& group : snapshot.groups) {
    if (const auto grid = z13::building::primitives::GridOfGroup(group->id)) {
      uint64_t& district = result.by_grid[*grid];
      district = Add(Add(district, group->id), group->fingerprint);
      continue;
    }
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
    bool holds_anything = parts.blocks != 0;
    for (const PlayerHash& player : parts.players) {
      if (!players || players->contains(player.player_id)) {
        total = Add(total, player.hash);
        holds_anything = true;
      }
    }
    // A sector of players the view leaves out is no sector of the client's: it has no entry to compare with.
    if (!holds_anything) {
      continue;
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
  SectorHashes hashes {.global = split.global, .by_sector = HashesOf(split, nullptr)};
  for (const auto& [grid, hash] : split.by_grid) {
    hashes.by_grid.emplace_back(grid, hash);
  }
  return hashes;
}

}  // namespace z13::net
