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

#include "block_entities.h"

namespace z13::station {

namespace {

z13::primitives::Placement PlacementOf(const Block& block) {
  return {.cell = block.cell, .size = block.size, .orientation = block.orientation};
}

bool Intersects(const PlayerSphere& player, const z13::primitives::CellBox& cells) {
  const Eigen::Vector3f min = cells.min.cast<float>() * kCellSize;
  const Eigen::Vector3f max = cells.End().cast<float>() * kCellSize;
  const Eigen::Vector3f closest = player.center.cwiseMax(min).cwiseMin(max);
  return (closest - player.center).squaredNorm() < player.radius * player.radius;
}

}  // namespace

std::expected<void, std::string> ValidateBuild(
    const Block& block, const z13::primitives::Palette& palette, const BlockIndex& index,
    std::span<const PlayerSphere> players, std::span<const z13::primitives::CellBox> spawn_clearances) {
  const auto primitive = palette.Find(block.type_id);
  if (!primitive) {
    return std::unexpected(std::format("unknown primitive {}", block.type_id));
  }
  if (!z13::primitives::IsValidOrientation(block.orientation)) {
    return std::unexpected(std::format("orientation {} out of range", block.orientation));
  }
  const auto& limits = primitive->get();
  if ((block.size.array() < limits.min_size.array()).any() || (block.size.array() > limits.max_size.array()).any()) {
    return std::unexpected(std::format(
        "size {}x{}x{} outside the limits of '{}'", block.size.x(), block.size.y(), block.size.z(), limits.name));
  }

  const z13::primitives::CellBox cells = PlacementOf(block).Occupied();
  if (index.Overlaps(cells)) {
    return std::unexpected(std::string {"cells are taken"});
  }
  if (std::ranges::any_of(players, [&cells](const PlayerSphere& player) { return Intersects(player, cells); })) {
    return std::unexpected(std::string {"a player is in the way"});
  }
  if (std::ranges::any_of(
          spawn_clearances, [&cells](const z13::primitives::CellBox& clearance) { return clearance.Overlaps(cells); })) {
    return std::unexpected(std::string {"players spawn there"});
  }
  if (limits.Has(z13::primitives::PrimitiveFlags::Spawn) && index.Overlaps(SpawnClearance(block))) {
    return std::unexpected(std::string {"no room above the spawn point"});
  }
  return {};
}

std::expected<void, std::string> ValidateDestroy(
    const Block& block, const z13::primitives::Palette& palette, int spawn_points) {
  const auto primitive = palette.Find(block.type_id);
  if (primitive && primitive->get().Has(z13::primitives::PrimitiveFlags::Spawn) && spawn_points <= 1) {
    return std::unexpected(std::string {"the last spawn point"});
  }
  return {};
}

}  // namespace z13::station
