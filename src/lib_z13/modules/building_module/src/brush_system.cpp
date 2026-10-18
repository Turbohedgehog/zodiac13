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

#include "brush_system.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <utility>

#include <Eigen/Dense>
#include <flecs.h>

#include <lib_core/world/lifecycle.h>

#include <z13/components/building.h>
#include <z13/components/player.h>
#include <z13/components/action_listener.h>
#include <z13/components/block.h>
#include <z13/components/block_brush.h>
#include <z13/components/inventory.h>
#include <z13/components/station_mode.h>
#include <primitives/palette.h>
#include <primitives/placement.h>
#include <z13_settings/building_tuning.h>
#include <z13_module/gameplay/gameplay_entities.h>

#include "build_action_ids.h"
#include "held_tool.h"
#include "station_build_phase.h"

namespace z13::building {

namespace {

using z13::building::primitives::BlockPalette;
using z13::building::primitives::Palette;
using z13::building::primitives::Primitive;
using z13::building::primitives::TurnAxis;
using z13::station::BlockBrush;
using z13::station::BlockSpec;
using z13::station::BrushDrag;
using z13::station::BuildPermission;
using z13::station::StationMode;

// Every station player with a build tool may build for now (a sandbox). Granted once, with
// the tool's brush, so it can be taken away.
void EnsureToolBrush(flecs::entity tool, const z13::inventory::CarriedBy& carried, const BuildingTuning& tuning) {
  const int width = tuning.default_brush_width_cells;
  tool.set(BlockBrush {
      .spec = {.type_id = tuning.default_brush_type, .size = {width, 1, width}},
  });
  if (const auto player = z13::gameplay::FindPlayer(tool.world(), carried.player_id)) {
    player->add<BuildPermission>();
  }
}

void DropBrushDrag(flecs::entity player, const BrushDrag&) {
  player.remove<BrushDrag>();
}

Eigen::Vector3i DefaultSize(const Primitive& primitive, int width_cells) {
  return primitive.shape.FitSize(
      Eigen::Vector3i::Constant(width_cells).cwiseMax(primitive.min_size).cwiseMin(primitive.max_size));
}

// A step through the palette from `current` (the first primitive if it isn't there).
uint32_t Stepped(const Palette& palette, uint32_t current, int step) {
  const auto found =
      std::ranges::find_if(palette.primitives, [current](const Primitive& primitive) { return primitive.id == current; });
  const auto count = static_cast<int>(palette.primitives.size());
  const int index = found != palette.primitives.end() ? static_cast<int>(found - palette.primitives.begin()) : 0;
  return palette.primitives[static_cast<size_t>((index + step % count + count) % count)].id;
}

// The primitive this frame's actions pick, if any: the palette window's, or the next or
// previous one.
std::optional<uint32_t> PickedType(
    const z13::input::ActionListener& listener, const BuildActionIds& ids, const Palette& palette, uint32_t current) {
  if (palette.primitives.empty()) {
    return std::nullopt;
  }
  if (IsSwitchedOn(listener, ids.select_primitive)) {
    const long slot = std::lround(listener.Value(*ids.select_primitive)->current_value) - 1;
    return slot >= 0 && static_cast<size_t>(slot) < palette.primitives.size()
               ? std::optional(palette.primitives[static_cast<size_t>(slot)].id)
               : std::nullopt;
  }
  const int step = (IsSwitchedOn(listener, ids.next_primitive) ? 1 : 0) -
                   (IsSwitchedOn(listener, ids.previous_primitive) ? 1 : 0);
  return step != 0 ? std::optional(Stepped(palette, current, step)) : std::nullopt;
}

// Applied to the held tool's brush through get_mut, so this frame's build sees the pick. A
// palette window pick that lands once the tool is put away still reaches the tool.
void ApplyBrushActions(
    flecs::entity player, const z13::input::ActionListener& listener, const BuildActionIds& ids,
    const BlockPalette& palette, const BuildingTuning& tuning) {
  std::optional<flecs::entity> tool = HeldBuildTool(player);
  if (!tool && IsSwitchedOn(listener, ids.select_primitive)) {
    tool = CarriedBuildTool(player);
  }
  if (!tool || !tool->has<BlockBrush>()) {
    return;
  }
  BlockBrush& brush = tool->get_mut<BlockBrush>();
  BlockSpec spec = brush.spec;
  if (const auto type = PickedType(listener, ids, palette.palette, spec.type_id); type && *type != spec.type_id) {
    if (const auto primitive = palette.palette.Find(*type)) {
      spec.type_id = *type;
      spec.size = DefaultSize(primitive->get(), tuning.default_brush_width_cells);
    }
  }

  const std::array turns {
      std::pair {ids.rotate_around_z, TurnAxis::kZ},
      std::pair {ids.rotate_around_y, TurnAxis::kY},
      std::pair {ids.rotate_around_x, TurnAxis::kX},
  };
  for (const auto& [action_id, axis] : turns) {
    if (IsSwitchedOn(listener, action_id)) {
      spec.orientation = z13::building::primitives::QuarterTurn(spec.orientation, axis);
    }
  }

  brush.spec = spec;
}

void RegisterSystems(flecs::world world) {
  world.system<const BrushDrag>("BrushSystem::DropBrushDrag")
      .kind<StationBuildPhase>()
      .without<BuildingTool>()
      .write<BrushDrag>()
      .each(DropBrushDrag);

  world.system<const z13::inventory::CarriedBy, const BuildingTuning>("BrushSystem::EnsureToolBrush")
      .kind<StationBuildPhase>()
      .with<BuildTool>()
      .with<StationMode>()
      .without<BlockBrush>()
      .write<BlockBrush>()
      .write<BuildPermission>()
      .each(EnsureToolBrush);

  world.system<const z13::input::ActionListener, const BuildActionIds, const BlockPalette, const BuildingTuning>(
           "BrushSystem::ApplyBrushActions")
      .kind<StationBuildPhase>()
      .with<z13::gameplay::Player>()
      .with<StationMode>()
      .read<z13::inventory::ActiveItem>()
      .read<z13::inventory::CarriedBy>()
      .write<BlockBrush>()
      .each(ApplyBrushActions);
}

}  // namespace

void BrushSystem::Register(flecs::world& world) {
  OnInitSystems(world, RegisterSystems);
}

}  // namespace z13::building
