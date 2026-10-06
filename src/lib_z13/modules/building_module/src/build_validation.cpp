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

#include "build_validation.h"

#include <algorithm>
#include <format>

#include <z13_primitives/placement.h>

#include <lib_core/utils/status.h>

#include "block_entities.h"

namespace z13::building {

using z13::station::Block;
using z13::station::BlockSpec;
using z13::station::kCellSize;

namespace {

using z13::building::primitives::CellBox;

bool Intersects(const PlayerSphere& player, const CellBox& cells) {
  const Eigen::Vector3f min = cells.min.cast<float>() * kCellSize;
  const Eigen::Vector3f max = cells.End().cast<float>() * kCellSize;
  const Eigen::Vector3f closest = player.center.cwiseMax(min).cwiseMin(max);
  return (closest - player.center).squaredNorm() < player.radius * player.radius;
}

}  // namespace

Status ValidateBuild(
    const Block& block, const z13::building::primitives::Palette& palette, const grid::BlockIndex& index,
    std::span<const PlayerSphere> players, std::span<const CellBox> spawn_clearances, const BuildingTuning& tuning) {
  const BlockSpec& spec = block.spec;
  const auto primitive = palette.Find(spec.type_id);
  if (!primitive) {
    return std::unexpected(std::format("unknown primitive {}", spec.type_id));
  }
  const auto& limits = primitive->get();
  if ((spec.size.array() < limits.min_size.array()).any() || (spec.size.array() > limits.max_size.array()).any()) {
    return std::unexpected(std::format(
        "size {}x{}x{} outside the limits of '{}'", spec.size.x(), spec.size.y(), spec.size.z(), limits.name));
  }

  const CellBox cells = z13::building::primitives::OccupiedCells(block);
  if (index.Overlaps(cells)) {
    return std::unexpected(std::string {"cells are taken"});
  }
  if (std::ranges::any_of(players, [&cells](const PlayerSphere& player) { return Intersects(player, cells); })) {
    return std::unexpected(std::string {"a player is in the way"});
  }
  if (std::ranges::any_of(spawn_clearances, [&cells](const CellBox& clearance) { return clearance.Overlaps(cells); })) {
    return std::unexpected(std::string {"players spawn there"});
  }
  if (limits.Has(z13::building::primitives::PrimitiveFlags::Spawn) && index.Overlaps(SpawnClearance(block, tuning))) {
    return std::unexpected(std::string {"no room above the spawn point"});
  }
  return {};
}

Status ValidateDestroy(
    const Block& block, const z13::building::primitives::Palette& palette, int spawn_points) {
  const auto primitive = palette.Find(block.spec.type_id);
  if (primitive && primitive->get().Has(z13::building::primitives::PrimitiveFlags::Spawn) && spawn_points <= 1) {
    return std::unexpected(std::string {"the last spawn point"});
  }
  return {};
}

}  // namespace z13::building
