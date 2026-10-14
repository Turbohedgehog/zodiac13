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

#include "interaction_system.h"

#include <algorithm>
#include <optional>
#include <ranges>
#include <tuple>
#include <vector>

#include <Eigen/Dense>
#include <flecs.h>

#include <actions_generated.h>

#include <lib_core/state/world_state.h>
#include <lib_core/utils/log.h>
#include <lib_core/utils/math.h>
#include <lib_core/world/lifecycle.h>

#include <z13/components/action_listener.h>
#include <z13/components/action_map.h>
#include <z13/components/block.h>
#include <z13/components/input_events.h>
#include <z13/components/interaction.h>
#include <z13/components/player.h>
#include <z13/components/rooms.h>
#include <z13/components/station_mode.h>
#include <grid/block_index.h>
#include <z13_module/input/input_config_loader.h>
#include <primitives/palette.h>
#include <primitives/placement.h>
#include <z13_settings/building_tuning.h>

namespace z13::station {

namespace {

using z13::building::grid::BlockIndex;
using z13::building::primitives::BlockPalette;
using z13::building::primitives::CellBox;
using z13::building::primitives::OccupiedCells;
using z13::input::ActionListener;
using z13::input::ActionValueHolder;
using Methods = fbs::station::InteractionMethods;
using PrimitiveInteraction = z13::building::primitives::Interaction;

void OnConfigUpdated(UseActionId& id, z13::input::OnConfigUpdatedEvent, const z13::input::ActionMap& action_map) {
  id.use = z13::gameplay::input::InputConfigLoader::FindActionId(
      action_map.action_map, z13::gameplay::input::kActionsEnumName, z13::fbs::actions::Action::USE);
  if (!id.use) {
    log_error("Interaction: cannot find its action in '{}'", z13::gameplay::input::kActionsEnumName);
  }
}

std::optional<PrimitiveInteraction> InteractionOf(const Block& block, const BlockPalette& palette) {
  const auto primitive = palette.palette.Find(block.spec.type_id);
  return primitive ? primitive->get().interaction : std::nullopt;
}

std::optional<flecs::entity> BlockEntity(flecs::world world, z13::building::grid::BlockId id) {
  if (!world.is_alive(id)) {
    return std::nullopt;
  }
  const flecs::entity block = world.entity(id);
  return block.has<Block>() ? std::optional(block) : std::nullopt;
}

// Use on the tick the action goes down, Hold on every tick it is down, that one included.
Methods RequestedMethods(const ActionListener& listener, const UseActionId& id) {
  const std::optional<ActionValueHolder> value = id.use ? listener.Value(*id.use) : std::nullopt;
  if (!value) {
    return Methods::NONE;
  }
  const bool held = value->current_value >= ActionValueHolder::kSwithValue - ActionValueHolder::kInputValueEps;
  return (value->IsSwitchedOn() ? Methods::Use : Methods::NONE) | (held ? Methods::Hold : Methods::NONE);
}

void AddUses(flecs::entity player, const Eigen::Matrix4f& transform, Methods requested, const BlockIndex& index,
             const BlockPalette& palette, float reach, std::vector<Interaction>& found) {
  const Eigen::Vector3f eye = z13::math::ExtractTranslation<float>(transform) / kCellSize;
  const Eigen::Vector3f end = (transform * Eigen::Vector3f(reach, 0.f, 0.f).homogeneous()).head<3>() / kCellSize;
  const auto hit = index.Raycast(eye, end);
  const auto block = hit ? BlockEntity(player.world(), *hit) : std::nullopt;
  const auto interaction = block ? InteractionOf(block->get<Block>(), palette) : std::nullopt;
  if (!interaction) {
    return;
  }
  for (const Methods method : {Methods::Use, Methods::Hold}) {
    if ((requested & method) == method && interaction->Accepts(method)) {
      found.push_back({.actor = player.id(), .target = block->id(), .method = method});
    }
  }
}

int WidestSensor(const BlockPalette& palette) {
  const auto sensor = [](const z13::building::primitives::Primitive& primitive) {
    return primitive.interaction ? primitive.interaction->sensor_cells : 0;
  };
  const auto& primitives = palette.palette.primitives;
  return primitives.empty() ? 0 : std::ranges::max(primitives | std::views::transform(sensor));
}

// Blocks found through the index come in no particular order; their cells order them the same
// on every participant.
bool CellBefore(const flecs::entity& a, const flecs::entity& b) {
  const Eigen::Vector3i& first = a.get<Block>().cell;
  const Eigen::Vector3i& second = b.get<Block>().cell;
  return std::tie(first.x(), first.y(), first.z()) < std::tie(second.x(), second.y(), second.z());
}

void AddPresence(flecs::entity player, const Eigen::Matrix4f& transform, const BlockIndex& index,
                 const BlockPalette& palette, std::vector<Interaction>& found) {
  const int widest = WidestSensor(palette);
  if (widest == 0) {
    return;
  }
  const Eigen::Vector3i cell =
      (z13::math::ExtractTranslation<float>(transform) / kCellSize).array().floor().cast<int>();
  const CellBox around =
      CellBox {.min = cell, .extent = Eigen::Vector3i::Ones()}.Grown(Eigen::Vector3i::Constant(widest));
  std::vector<flecs::entity> sensing;
  for (const z13::building::grid::BlockId id : index.Overlapping(around)) {
    const auto block = BlockEntity(player.world(), id);
    const auto interaction = block ? InteractionOf(block->get<Block>(), palette) : std::nullopt;
    if (interaction && interaction->Accepts(Methods::Presence) &&
        OccupiedCells(block->get<Block>()).Grown(Eigen::Vector3i::Constant(interaction->sensor_cells)).Contains(cell)) {
      sensing.push_back(*block);
    }
  }
  std::ranges::sort(sensing, CellBefore);
  for (const flecs::entity& block : sensing) {
    found.push_back({.actor = player.id(), .target = block.id(), .method = Methods::Presence});
  }
}

void FindInteractions(flecs::entity player, const z13::gameplay::Player&, const ActionListener& listener,
                      const Eigen::Matrix4f& transform, Interactions& interactions, const BlockIndex& index,
                      const BlockPalette& palette, const z13::BuildingTuning& tuning, const UseActionId& id) {
  if (const Methods requested = RequestedMethods(listener, id); requested != Methods::NONE) {
    AddUses(player, transform, requested, index, palette, tuning.use_reach_distance, interactions.list);
  }
  AddPresence(player, transform, index, palette, interactions.list);
}

void RegisterComponents(flecs::world world) {
  z13::flecs_tools::RegisterComponents<Interactions, UseActionId>(world);
}

void RegisterPipeline(flecs::world world) {
  world.component<InteractionPhase>().add(flecs::Phase).depends_on<StationTopologyPhase>();
}

void RegisterSystems(flecs::world world) {
  world.set<Interactions>({});
  world.set<UseActionId>({});

  world.observer<UseActionId, z13::input::OnConfigUpdatedEvent, z13::input::ActionMap>(
           "InteractionSystem::OnConfigUpdated")
      .event<z13::input::SystemInputEventType>()
      .each(OnConfigUpdated);

  world.system<Interactions>("InteractionSystem::Clear")
      .kind<InteractionPhase>()
      .each([](Interactions& interactions) { interactions = {}; });
  world.system<const z13::gameplay::Player, const ActionListener, const Eigen::Matrix4f, Interactions,
               const BlockIndex, const BlockPalette, const z13::BuildingTuning, const UseActionId>(
           "InteractionSystem::Find")
      .kind<InteractionPhase>()
      .with<StationMode>()
      .read<Block>()
      .order_by<z13::gameplay::Player>(z13::gameplay::CompareByPlayerId)
      .each(FindInteractions);
}

}  // namespace

void InteractionSystem::Register(flecs::world& world) {
  OnRegisterComponents(world, RegisterComponents);
  OnInitPhases(world, RegisterPipeline);
  OnInitSystems(world, RegisterSystems);
}

}  // namespace z13::station
