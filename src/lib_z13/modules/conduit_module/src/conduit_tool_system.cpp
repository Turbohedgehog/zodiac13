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


#include "conduit_tool_system.h"

#include <algorithm>
#include <expected>
#include <filesystem>
#include <format>
#include <span>
#include <string>
#include <string_view>

#include <Eigen/Dense>

#include <conduit_generated.h>

#include <grid/block_index.h>
#include <lib_core/state/state_markers.h>
#include <lib_core/state/world_state.h>
#include <lib_core/time/simulation_clock.h>
#include <lib_core/utils/asset_source.h>
#include <lib_core/utils/log.h>
#include <lib_core/utils/math.h>
#include <lib_core/world/components.h>
#include <lib_core/world/lifecycle.h>
#include <primitives/palette.h>
#include <z13/components/action_map.h>
#include <z13/components/block.h>
#include <z13/components/block_brush.h>
#include <z13/components/building.h>
#include <z13/components/conduit.h>
#include <z13/components/input_events.h>
#include <z13/components/input_phases.h>
#include <z13/components/player.h>
#include <z13/components/spawn_stamp.h>
#include <z13/components/state_changes.h>
#include <z13/components/station_mode.h>
#include <z13/components/touch_controls.h>
#include <z13_module/input/input_config_loader.h>
#include <z13_settings/building_tuning.h>

#include "conduit_index.h"
#include "conduit_kinds.h"
#include "conduit_run.h"

namespace z13::conduit {

namespace {

namespace fbs = z13::fbs::conduit;
using z13::gameplay::Player;
using z13::inventory::ActiveItem;
using z13::station::kCellSize;

constexpr std::string_view kActionGroup = "Conduit";
constexpr std::string_view kActionsEnumName = "z13.fbs.conduit.Action";
// The index of the conduit tool among the items one player is given in one tick (the build tool is the first).
constexpr uint32_t kConduitToolIndex = 1;
constexpr std::string_view kItemPrefix = "Item";
constexpr std::string_view kConduitPrefix = "Conduit";

bool IsSwitchedOn(const z13::input::ActionListener& listener, const std::optional<ConduitActionIds::IdType>& id) {
  const std::optional<z13::input::ActionValueHolder> value = id ? listener.Value(*id) : std::nullopt;
  return value && value->IsSwitchedOn();
}

void OnAppendInputSchema(flecs::iter&, size_t, const z13::input::AppendInputSchema, z13::input::ActionMap& map) {
  z13::input::FlatbufferBinarySchema schema {
      .binary_schema = std::span {fbs::ConduitKindsBinarySchema::data(), fbs::ConduitKindsBinarySchema::size()},
  };
  if (const auto appended = z13::gameplay::input::InputConfigLoader::AppendFlatbufActionsFromBinarySchema(schema, map);
      !appended) {
    log_error("ConduitToolSystem: cannot read the conduit actions from the schema: {}", appended.error());
  }
}

void OnConfigUpdated(
    ConduitActionIds& ids, z13::input::OnConfigUpdatedEvent, const z13::input::ActionMap& action_map) {
  const auto find = [&action_map](fbs::Action action) {
    const auto id = z13::gameplay::input::InputConfigLoader::FindActionId(
        action_map.action_map, kActionsEnumName, action);
    if (!id) {
      log_error("ConduitToolSystem: cannot find action {} in '{}'", static_cast<int>(action), kActionsEnumName);
    }
    return id;
  };
  ids = {
      .lay = find(fbs::Action::LAY_CONDUIT),
      .remove = find(fbs::Action::REMOVE_CONDUIT),
      .previous_kind = find(fbs::Action::PREVIOUS_CONDUIT_KIND),
      .next_kind = find(fbs::Action::NEXT_CONDUIT_KIND),
      .toggle_depth = find(fbs::Action::TOGGLE_CONDUIT_DEPTH),
  };
}

std::optional<flecs::entity> HeldConduitTool(flecs::entity player) {
  const auto* active = player.try_get<ActiveItem>();
  if (active == nullptr) {
    return std::nullopt;
  }
  const flecs::entity item = player.world().entity(active->item);
  return item.is_alive() && item.has<ConduitTool>() ? std::optional(item) : std::nullopt;
}

void SyncActionGroup(flecs::entity player, z13::input::ActionListener& listener) {
  auto& groups = listener.action_group_priority;
  const bool wanted = HeldConduitTool(player).has_value();
  if (wanted == std::ranges::contains(groups, kActionGroup)) {
    return;
  }
  if (wanted) {
    groups.emplace_back(kActionGroup);
  } else {
    std::erase(groups, kActionGroup);
  }
}

void GiveConduitTool(flecs::entity player, const Player& owner) {
  flecs::world world = player.world();
  const z13::gameplay::SpawnStamp stamp {
      .tick = world.get<z13::flecs_tools::SimulationClock>().tick, .player_id = owner.id};
  world.entity(z13::gameplay::SpawnName(kItemPrefix, stamp, kConduitToolIndex).c_str())
      .add<z13::flecs_tools::StateEntity>()
      .set(z13::inventory::Item {.kind = z13::inventory::ItemKind::kConduitTool})
      .set(z13::inventory::CarriedBy {.player_id = owner.id, .cell = kConduitToolCell})
      .set(ConduitTool {.kind = world.get<ConduitKinds>().kinds.front().id});
  player.add<ConduitToolGiven>();
}

// What the hotbar shows on the tool and which touch buttons it has; derived, so a restored tool gets them too.
void DeriveToolLook(flecs::entity item, const ConduitTool& tool, const ConduitKinds& kinds) {
  const auto kind = kinds.Find(tool.kind);
  const std::string name = kind ? kind->name : std::string("Conduit");
  const std::string text = tool.depth == ConduitDepth::kOnSurface ? std::format("{} (surface)", name) : name;
  if (!item.has<z13::inventory::ItemCaption>() || item.get<z13::inventory::ItemCaption>().text != text) {
    item.set(z13::inventory::ItemCaption {.text = text});
  }
  if (!item.has<z13::input::ItemTouchLayer>()) {
    item.set(z13::input::ItemTouchLayer {.layer = z13::input::TouchLayer::kBlock});
  }
}

uint32_t SteppedKind(const ConduitKinds& kinds, uint32_t kind, int step) {
  const auto found = std::ranges::find(kinds.kinds, kind, &ConduitKind::id);
  const auto size = static_cast<int>(kinds.kinds.size());
  const int at = found == kinds.kinds.end() ? 0 : static_cast<int>(found - kinds.kinds.begin());
  return kinds.kinds[static_cast<size_t>((at + step + size) % size)].id;
}

// The player's view ray, in cells.
struct ViewRay {
  Eigen::Vector3f eye = Eigen::Vector3f::Zero();
  Eigen::Vector3f reach = Eigen::Vector3f::Zero();
};

ViewRay ViewRayOf(const Eigen::Matrix4f& transform, float reach_m) {
  const Eigen::Vector3f origin = z13::math::ExtractTranslation<float>(transform);
  const Eigen::Vector3f forward = transform.block<3, 3>(0, 0).col(0);
  return {.eye = origin / kCellSize, .reach = (origin + forward * reach_m) / kCellSize};
}

// The cell of the aimed block, or the free one in front of it; nothing while no block is aimed at.
std::optional<Eigen::Vector3i> AimedCell(
    const ViewRay& ray, const z13::building::grid::BlockIndex& blocks, ConduitDepth depth) {
  const auto hit = blocks.RaycastHit(ray.eye, ray.reach);
  if (!hit || hit->normal.isZero()) {
    return std::nullopt;
  }
  const float side = depth == ConduitDepth::kInside ? -0.5f : 0.5f;
  return (hit->point + hit->normal.cast<float>() * side).array().floor().cast<int>().eval();
}

// The ray a removal looks along: through the open and one cell into the first block, not beyond it.
ViewRay RemovalRay(const ViewRay& ray, const z13::building::grid::BlockIndex& blocks) {
  const auto hit = blocks.RaycastHit(ray.eye, ray.reach);
  return hit && !hit->normal.isZero() ? ViewRay {.eye = ray.eye, .reach = hit->point - hit->normal.cast<float>()}
                                      : ray;
}

std::optional<std::string> NetworkOf(const ConduitKinds& kinds, uint32_t kind) {
  const auto found = kinds.Find(kind);
  return found ? std::optional(found->network) : std::nullopt;
}

// The world's conduits with this tick's edits so far, built at the tick's first edit.
using TickConduits = std::optional<ConduitIndex>;

ConduitIndex& IndexOf(TickConduits& conduits, flecs::world world, const ConduitKinds& kinds) {
  if (!conduits) {
    conduits.emplace();
    world.each([&](flecs::entity entity, const Conduit& conduit) {
      if (const auto network = NetworkOf(kinds, conduit.kind)) {
        conduits->Insert(entity.id(), *network, conduit);
      }
    });
  }
  return *conduits;
}

// What one player's tool does this tick.
struct ToolUse {
  flecs::entity player;
  uint32_t player_id {};
  ViewRay ray;
};

void Lay(const ToolUse& use, const Conduit& run, TickConduits& conduits, const ConduitKinds& kinds,
         int max_length) {
  flecs::world world = use.player.world();
  const std::optional<std::string> network = NetworkOf(kinds, run.kind);
  if (!network || run.length > max_length) {
    log_debug("conduit: run of kind {} and {} cells refused", run.kind, run.length);
    return;
  }
  ConduitIndex& index = IndexOf(conduits, world, kinds);
  if (index.Overlaps(*network, run)) {
    log_debug("conduit: a {} conduit is there already", *network);
    return;
  }
  const z13::gameplay::SpawnStamp stamp {
      .tick = world.get<z13::flecs_tools::SimulationClock>().tick, .player_id = use.player_id};
  const flecs::entity entity = world.entity(z13::gameplay::SpawnName(kConduitPrefix, stamp, 0).c_str())
                                   .add<z13::flecs_tools::StateEntity>()
                                   .set(run);
  z13::gameplay::RecordStateChange(entity, z13::gameplay::StateChangeKind::kSet);
  index.Insert(entity.id(), *network, run);
}

void Remove(const ToolUse& use, flecs::entity tool, const z13::building::grid::BlockIndex& blocks,
            TickConduits& conduits, const ConduitKinds& kinds) {
  flecs::world world = use.player.world();
  const std::optional<std::string> network = NetworkOf(kinds, tool.get<ConduitTool>().kind);
  if (!network) {
    return;
  }
  const ViewRay ray = RemovalRay(use.ray, blocks);
  ConduitIndex& index = IndexOf(conduits, world, kinds);
  const auto hit = index.Raycast(ray.eye, ray.reach, *network);
  if (!hit) {
    return;
  }
  const flecs::entity target = world.entity(*hit);
  index.Erase(*hit, target.get<Conduit>());
  z13::gameplay::RecordStateChange(target, z13::gameplay::StateChangeKind::kRemoved);
  target.destruct();
}

struct ToolContext {
  const ConduitKinds& kinds;
  const z13::building::grid::BlockIndex& blocks;
  const ConduitActionIds& ids;
  int max_length {};
  float reach_m {};
};

void UseTool(const ToolContext& context, flecs::entity player, uint32_t player_id, const Eigen::Matrix4f& transform,
             const z13::input::ActionListener& listener, TickConduits& conduits) {
  const std::optional<flecs::entity> tool = HeldConduitTool(player);
  if (!tool) {
    player.remove<ConduitDrag>();
    return;
  }
  ConduitTool& state = tool->get_mut<ConduitTool>();
  const int step = (IsSwitchedOn(listener, context.ids.next_kind) ? 1 : 0) -
                   (IsSwitchedOn(listener, context.ids.previous_kind) ? 1 : 0);
  if (step != 0) {
    state.kind = SteppedKind(context.kinds, state.kind, step);
  }
  if (IsSwitchedOn(listener, context.ids.toggle_depth)) {
    state.depth = state.depth == ConduitDepth::kInside ? ConduitDepth::kOnSurface : ConduitDepth::kInside;
  }
  if (!player.has<z13::station::BuildPermission>()) {
    return;
  }
  const ToolUse use {.player = player, .player_id = player_id, .ray = ViewRayOf(transform, context.reach_m)};
  if (IsSwitchedOn(listener, context.ids.remove)) {
    if (player.has<ConduitDrag>()) {
      player.remove<ConduitDrag>();
    } else {
      Remove(use, *tool, context.blocks, conduits, context.kinds);
    }
  }
  if (!IsSwitchedOn(listener, context.ids.lay)) {
    return;
  }
  const std::optional<Eigen::Vector3i> cell = AimedCell(use.ray, context.blocks, state.depth);
  if (!cell) {
    return;
  }
  if (!player.has<ConduitDrag>()) {
    player.set(ConduitDrag {.anchor = *cell});
    return;
  }
  const Eigen::Vector3i anchor = player.get<ConduitDrag>().anchor;
  player.remove<ConduitDrag>();
  Lay(use, RunBetween(state.kind, anchor, *cell), conduits, context.kinds, context.max_length);
}

std::expected<ConduitKinds, std::string> LoadKinds(const z13::AssetSource& assets) {
  const std::filesystem::path file = ConduitKindsFile();
  return assets.ReadAsset(file).and_then(ParseConduitKinds).transform_error([&file](const std::string& error) {
    return std::format("{} ({})", error, file.string());
  });
}

void RegisterComponents(flecs::world world) {
  z13::flecs_tools::RegisterComponents<ConduitKinds, Conduit, ConduitTool, ConduitDrag, ConduitToolGiven,
                                       ConduitActionIds>(world);
}

void RegisterSystems(flecs::world world) {
  // Set here, not next to `.add(flecs::Singleton)` (see PhysicsSystem::RegisterSystems).
  world.set<ConduitActionIds>({});
  if (auto kinds = LoadKinds(world.get<z13::Assets>().source)) {
    world.set(std::move(*kinds));
  } else {
    log_error("ConduitToolSystem: {}", kinds.error());
  }

  world.observer<z13::input::AppendInputSchema, z13::input::ActionMap>("ConduitToolSystem::OnAppendInputSchema")
      .event<z13::input::AppendInputSchema>()
      .each(OnAppendInputSchema);
  world.observer<ConduitActionIds, z13::input::OnConfigUpdatedEvent, z13::input::ActionMap>(
           "ConduitToolSystem::OnConfigUpdated")
      .event<z13::input::SystemInputEventType>()
      .each(OnConfigUpdated);

  // After the starter kit's build tool, in player id order.
  world.system<const Player>("ConduitToolSystem::GiveConduitTool")
      .kind<z13::inventory::InventoryPhase>()
      .with<z13::station::StationMode>()
      .with<ConduitKinds>()
      .without<ConduitToolGiven>()
      .order_by<Player>(z13::gameplay::CompareByPlayerId)
      .write<z13::inventory::Item>()
      .write<z13::inventory::CarriedBy>()
      .write<ConduitTool>()
      .write<ConduitToolGiven>()
      .each(GiveConduitTool);

  world.system<z13::input::ActionListener>("ConduitToolSystem::SyncActionGroup")
      .kind<z13::input::ClearActionFramePhase>()
      .read<ActiveItem>()
      .read<ConduitTool>()
      .each(SyncActionGroup);

  world.system<const ConduitTool, const ConduitKinds>("ConduitToolSystem::DeriveToolLook")
      .kind<z13::input::ClearActionFramePhase>()
      .write<z13::inventory::ItemCaption>()
      .write<z13::input::ItemTouchLayer>()
      .each(DeriveToolLook);

  const auto players = world.query_builder<const Player, const Eigen::Matrix4f, const z13::input::ActionListener>(
                                "ConduitToolSystem::Players")
                           .order_by<Player>(z13::gameplay::CompareByPlayerId)
                           .build();
  // A system of singletons runs once a frame; the players go in id order, each seeing the edits of those before.
  world.system<const ConduitKinds, const z13::building::grid::BlockIndex, const ConduitActionIds,
               const z13::BuildingTuning, const z13::building::primitives::BlockPalette, z13::gameplay::EditActor>(
           "ConduitToolSystem::UseTool")
      .kind<z13::building::UpdateBuildingToolPhase>()
      .with<z13::station::StationMode>()
      .read<ActiveItem>()
      .read<z13::station::BuildPermission>()
      .write<ConduitTool>()
      .write<ConduitDrag>()
      .write<Conduit>()
      .each([players](flecs::iter&, size_t, const ConduitKinds& kinds,
                      const z13::building::grid::BlockIndex& blocks, const ConduitActionIds& ids,
                      const z13::BuildingTuning& tuning, const z13::building::primitives::BlockPalette& palette,
                      z13::gameplay::EditActor& actor) {
        const ToolContext context {.kinds = kinds,
                                   .blocks = blocks,
                                   .ids = ids,
                                   .max_length = palette.palette.max_size_cells,
                                   .reach_m = tuning.destroy_reach_distance};
        TickConduits conduits;
        players.each([&](flecs::entity player, const Player& owner, const Eigen::Matrix4f& transform,
                         const z13::input::ActionListener& listener) {
          const z13::gameplay::ActorScope scope(actor, owner.id);
          UseTool(context, player, owner.id, transform, listener, conduits);
        });
      });
}

}  // namespace

void ConduitToolSystem::Register(flecs::world& world) {
  z13::OnRegisterComponents(world, RegisterComponents);
  z13::OnInitSystems(world, RegisterSystems);
}

}  // namespace z13::conduit
