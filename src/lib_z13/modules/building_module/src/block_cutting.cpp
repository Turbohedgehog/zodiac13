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

#include "block_cutting.h"

#include <algorithm>
#include <optional>
#include <tuple>
#include <utility>

#include <primitives/cut.h>

#include "block_entities.h"

namespace z13::building {

namespace {

using z13::building::primitives::PrimitiveFlags;
using z13::station::Block;

std::optional<Block> BlockOf(flecs::entity entity, const MadeBlocks& made) {
  if (const auto it = made.find(entity.id()); it != made.end()) {
    return it->second;
  }
  return entity.is_alive() && entity.has<Block>() ? std::optional(entity.get<Block>()) : std::nullopt;
}

bool CellOrder(const Block& a, const Block& b) {
  return std::tuple(a.cell.z(), a.cell.y(), a.cell.x()) < std::tuple(b.cell.z(), b.cell.y(), b.cell.x());
}

}  // namespace

std::expected<CutPlan, std::string> PlanCut(
    flecs::world world, const BuildView& view, const MadeBlocks& made, const CutRequest& request) {
  const z13::building::primitives::Palette& palette = view.palette;
  const z13::building::primitives::CellBox& cells = request.cells;
  std::vector<std::pair<flecs::entity, Block>> hit;
  for (const grid::BlockId id : view.index.get().Overlapping(cells)) {
    const flecs::entity entity = world.entity(id);
    if (const auto block = BlockOf(entity, made)) {
      hit.emplace_back(entity, *block);
    }
  }
  std::ranges::sort(hit, [](const auto& a, const auto& b) { return CellOrder(a.second, b.second); });

  CutPlan plan;
  for (const auto& [entity, block] : hit) {
    const auto primitive = palette.Find(block.spec.type_id);
    if (!primitive) {
      plan.removed.push_back(entity);
      continue;
    }
    if (primitive->get().Has(PrimitiveFlags::Spawn)) {
      ++plan.spawn_points_removed;
    }
    plan.removed.push_back(entity);
    for (const Block& remainder : z13::building::primitives::LeftAfterCut(block, primitive->get(), cells)) {
      plan.remainders.push_back(remainder);
    }
  }
  const int spawn_points_left = request.spawn_points - plan.spawn_points_removed + request.spawn_points_added;
  if (plan.spawn_points_removed > 0 && spawn_points_left < 1) {
    return std::unexpected(std::string {"the last spawn point"});
  }
  return plan;
}

void ApplyCut(flecs::world world, const CutPlan& plan, BuildRun& run) {
  for (const flecs::entity entity : plan.removed) {
    run.index.get().Erase(entity.id());
    run.made.erase(entity.id());
    entity.destruct();
  }
  for (const Block& remainder : plan.remainders) {
    const flecs::entity created =
        CreateBlock(world, NextBlockName(world, run.counters), remainder, run.palette, run.tuning);
    run.index.get().Insert(created.id(), z13::building::primitives::OccupiedCells(remainder));
    run.made.emplace(created.id(), remainder);
  }
}

}  // namespace z13::building
