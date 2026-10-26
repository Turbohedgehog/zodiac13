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
using SectorTotals = std::unordered_map<sectors::SectorKey, uint64_t, sectors::SectorHash>;

uint64_t Add(uint64_t total, uint64_t hash) {
  return ft::MixHash(total ^ hash);
}

std::optional<sectors::SectorKey> SectorOfEntity(
    flecs::world world, const ft::EntitySnapshot& entity, const sectors::SectorIndex& index) {
  const flecs::entity found = world.lookup(entity.name.c_str());
  if (!found) {
    return std::nullopt;
  }
  const auto* player = found.try_get<z13::gameplay::Player>();
  return player ? index.Locate({.kind = sectors::MemberKind::kPlayer, .id = player->id}) : std::nullopt;
}

}  // namespace

SectorHashes HashBySector(flecs::world world, const ft::WorldSnapshot& snapshot, const sectors::SectorIndex& index,
                          int chunk_cells) {
  SectorHashes result;
  // Entities come sorted by name and groups by id, so both peers add them in the same order.
  SectorTotals totals;
  for (const ft::EntitySnapshot& entity : snapshot.entities) {
    const uint64_t hash = ft::HashEntitySnapshot(entity);
    if (const auto sector = SectorOfEntity(world, entity, index)) {
      uint64_t& total = totals[*sector];
      total = Add(total, hash);
    } else {
      result.global = Add(result.global, hash);
    }
  }
  for (const std::shared_ptr<const ft::GroupSnapshot>& group : snapshot.groups) {
    const Eigen::Vector3i chunk = z13::building::primitives::UnpackChunk(group->id);
    uint64_t& total = totals[sectors::SectorOf(chunk * chunk_cells, index.SectorCells())];
    total = Add(Add(total, group->id), group->fingerprint);
  }
  for (const auto& [sector, hash] : totals) {
    result.by_sector.emplace_back(sector.x(), sector.y(), sector.z(), hash);
  }
  std::ranges::sort(result.by_sector, [](const fbs::net::SectorHashWire& a, const fbs::net::SectorHashWire& b) {
    return std::tuple(a.x(), a.y(), a.z()) < std::tuple(b.x(), b.y(), b.z());
  });
  return result;
}

}  // namespace z13::net
