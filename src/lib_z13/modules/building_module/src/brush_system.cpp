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
#include <z13/components/gameplay.h>
#include <z13/components/input.h>
#include <z13/components/station.h>
#include <z13_primitives/palette.h>
#include <z13_primitives/placement.h>

#include "build_action_ids.h"
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

// The first brush, before any pick: a 2 m square wall panel.
constexpr uint32_t kDefaultBrushType = 2;
// A picked primitive spans 2 m along each axis it stretches along, until a drag resizes it.
constexpr int kDefaultBrushWidthCells = 8;

// Every station player may build for now (a sandbox). Granted once, with the brush, so
// it can be taken away.
void EnsureBlockBrush(flecs::entity player, const z13::gameplay::Player&) {
  player
      .set(BlockBrush {
          .spec = {.type_id = kDefaultBrushType, .size = {kDefaultBrushWidthCells, 1, kDefaultBrushWidthCells}},
      })
      .add<BuildPermission>();
}

void DropBrushDrag(flecs::entity player, const BrushDrag&) {
  player.remove<BrushDrag>();
}

Eigen::Vector3i DefaultSize(const Primitive& primitive) {
  return Eigen::Vector3i::Constant(kDefaultBrushWidthCells).cwiseMax(primitive.min_size).cwiseMin(primitive.max_size);
}

// A step through the palette from `current` (the first primitive if it isn't there).
uint32_t Stepped(const Palette& palette, uint32_t current, int step) {
  const auto found =
      std::ranges::find_if(palette.primitives, [current](const Primitive& primitive) { return primitive.id == current; });
  const auto count = static_cast<int>(palette.primitives.size());
  const int index = found != palette.primitives.end() ? static_cast<int>(found - palette.primitives.begin()) : 0;
  return palette.primitives[static_cast<size_t>((index + step % count + count) % count)].id;
}

// The primitive this frame's actions pick, if any: the palette window's, a slot's, or
// the next or previous one.
std::optional<uint32_t> PickedType(
    const z13::input::ActionListener& listener, const BuildActionIds& ids, const Palette& palette, uint32_t current) {
  if (palette.primitives.empty()) {
    return std::nullopt;
  }
  if (IsSwitchedOn(listener, ids.select_primitive)) {
    return static_cast<uint32_t>(std::lround(listener.Value(*ids.select_primitive)->current_value));
  }
  const size_t slots = std::min(kPaletteSlots, palette.primitives.size());
  for (size_t slot = 0; slot < slots; ++slot) {
    if (IsSwitchedOn(listener, ids.select_slot[slot])) {
      return palette.primitives[slot].id;
    }
  }
  const int step = (IsSwitchedOn(listener, ids.next_primitive) ? 1 : 0) -
                   (IsSwitchedOn(listener, ids.previous_primitive) ? 1 : 0);
  return step != 0 ? std::optional(Stepped(palette, current, step)) : std::nullopt;
}

void ApplyBrushActions(
    BlockBrush& brush, const z13::input::ActionListener& listener, const BuildActionIds& ids,
    const BlockPalette& palette) {
  BlockSpec spec = brush.spec;
  if (const auto type = PickedType(listener, ids, palette.palette, spec.type_id); type && *type != spec.type_id) {
    if (const auto primitive = palette.palette.Find(*type)) {
      spec.type_id = *type;
      spec.size = DefaultSize(primitive->get());
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

  world.system<const z13::gameplay::Player>("BrushSystem::EnsureBlockBrush")
      .kind<StationBuildPhase>()
      .with<StationMode>()
      .without<BlockBrush>()
      .write<BlockBrush>()
      .write<BuildPermission>()
      .each(EnsureBlockBrush);

  world.system<BlockBrush, const z13::input::ActionListener, const BuildActionIds, const BlockPalette>(
           "BrushSystem::ApplyBrushActions")
      .kind<StationBuildPhase>()
      .with<z13::gameplay::Player>()
      .with<BuildingTool>()
      .with<StationMode>()
      .each(ApplyBrushActions);
}

}  // namespace

void BrushSystem::Register(flecs::world& world) {
  OnInitSystems(world, RegisterSystems);
}

}  // namespace z13::building
