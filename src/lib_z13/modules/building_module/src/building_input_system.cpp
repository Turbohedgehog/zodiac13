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

#include "building_input_system.h"

#include <algorithm>
#include <array>
#include <optional>

#include <flecs.h>

#include <building_generated.h>

#include <lib_core/state/world_state.h>
#include <lib_core/utils/flecs_utils.h>
#include <lib_core/utils/log.h>
#include <lib_core/world/components.h>
#include <lib_core/world/lifecycle.h>

#include <z13/components/gameplay.h>
#include <z13/components/action_listener.h>
#include <z13/components/action_map.h>
#include <z13/components/input_events.h>
#include <z13/components/input_phases.h>
#include <z13/components/building.h>
#include <z13/components/block_brush.h>
#include <z13/components/wall.h>
#include <z13_settings/building_tuning.h>
#include <z13/components/station_mode.h>
#include <z13/components/inventory.h>
#include <z13/components/tool_mode_info.h>
#include <z13/components/touch_controls.h>
#include <lib_core/utils/enum_cycle.h>
#include <z13_module/input/input_config_loader.h>
#include <lib_core/state/rollback.h>

#include "build_action_ids.h"
#include "held_tool.h"

namespace z13::building {

namespace {

// todo: убрать константу и брать из z13.fbs.building.Action.action_group
constexpr std::string_view kBuildingActionGroup = "Building";
// Its actions shadow the hotbar's wheel, so the group is on only in the Wall and Merge modes.
constexpr std::string_view kWallActionGroup = "BuildingWall";

void OnAppendInputSchema(
    flecs::iter it,
    size_t,
    const z13::input::AppendInputSchema,
    z13::input::ActionMap& action_map) {
  z13::input::FlatbufferBinarySchema ev {
      .binary_schema = std::span {
        z13::fbs::building::BlockBinarySchema::data(),
        z13::fbs::building::BlockBinarySchema::size()
      },
  };
  if (const auto appended = z13::gameplay::input::InputConfigLoader::AppendFlatbufActionsFromBinarySchema(ev, action_map);
      !appended) {
    log_error("cannot read building actions from schema: {}", appended.error());
  }
}

void OnConfigUpdated(flecs::entity e, z13::input::OnConfigUpdatedEvent, const z13::input::ActionMap& action_map) {
  using EnumValueType = z13::input::ActionInfo::EnumValueType;
  auto& build_action_ids = e.world().ensure<BuildActionIds>();
  build_action_ids = BuildActionIds();
  const auto& enum_value = action_map.action_map.get<z13::input::ActionMap::EnumNameEnumValueTag>();

  auto find_action_id = [&](const auto action_value, std::string_view enum_name = "z13.fbs.building.Action") {
    auto action_id_holder = z13::gameplay::input::InputConfigLoader::FindActionId(
        action_map.action_map,
        enum_name,
        action_value);
    if (!action_id_holder) {
      log_error(
        "OnConfigUpdated: Cannot find action id '{}' for enum 'z13.fbs.building'",
        static_cast<EnumValueType>(action_value)
      );
    }

    return action_id_holder;
  };

  build_action_ids.build_block = find_action_id(z13::fbs::building::Action::BUILD_BLOCK);
  build_action_ids.destroy_block = find_action_id(z13::fbs::building::Action::DESTROY_BLOK);
  build_action_ids.previous_primitive = find_action_id(z13::fbs::building::Action::PREVIOUS_PRIMITIVE);
  build_action_ids.next_primitive = find_action_id(z13::fbs::building::Action::NEXT_PRIMITIVE);
  build_action_ids.rotate_around_z = find_action_id(z13::fbs::building::Action::ROTATE_AROUND_Z);
  build_action_ids.rotate_around_y = find_action_id(z13::fbs::building::Action::ROTATE_AROUND_Y);
  build_action_ids.rotate_around_x = find_action_id(z13::fbs::building::Action::ROTATE_AROUND_X);
  build_action_ids.select_primitive = find_action_id(z13::fbs::building::Action::SELECT_PRIMITIVE);
  build_action_ids.show_palette = find_action_id(z13::fbs::building::Action::SHOW_PALETTE);
  build_action_ids.cut_modifier = find_action_id(z13::fbs::building::Action::CUT_MODIFIER);
  build_action_ids.cut_in_modifier = find_action_id(z13::fbs::building::Action::CUT_IN_MODIFIER);
  build_action_ids.cancel_brush_drag = find_action_id(z13::fbs::building::Action::CANCEL_BRUSH_DRAG);
  build_action_ids.cycle_tool_mode = find_action_id(z13::fbs::building::Action::CYCLE_TOOL_MODE);
  constexpr std::string_view kWallActions = "z13.fbs.building.WallAction";
  build_action_ids.thicker_wall = find_action_id(z13::fbs::building::WallAction::THICKER_WALL, kWallActions);
  build_action_ids.thinner_wall = find_action_id(z13::fbs::building::WallAction::THINNER_WALL, kWallActions);
  build_action_ids.flip_diagonal = find_action_id(z13::fbs::building::WallAction::FLIP_DIAGONAL, kWallActions);
  build_action_ids.bend_wall = find_action_id(z13::fbs::building::WallAction::BEND_WALL, kWallActions);
  build_action_ids.toggle_opening_kind =
      find_action_id(z13::fbs::building::WallAction::TOGGLE_OPENING_KIND, kWallActions);
}

void SetActionGroup(z13::input::ActionListener& action_listener, std::string_view group, bool wanted) {
  auto& groups = action_listener.action_group_priority;
  const bool has_group = std::ranges::find(groups, group) != groups.end();
  if (wanted == has_group) {
    return;
  }

  if (has_group) {
    std::erase(groups, group);
  } else {
    groups.emplace_back(group);
  }
}

void SyncTouchLayer(flecs::entity e, z13::input::TouchControls& controls) {
  if (const std::optional<flecs::entity> tool = HeldBuildTool(e)) {
    controls.layer = ToolModeInfoOf(tool->get<BuildTool>().mode).touch_layer;
    return;
  }
  const auto* active = e.try_get<z13::inventory::ActiveItem>();
  const auto* item = active ? e.world().entity(active->item).try_get<z13::input::ItemTouchLayer>() : nullptr;
  controls.layer = item ? item->layer : z13::input::TouchLayer::kWalk;
}

// The Building input groups follow the BuildingTool tag and the held tool's mode, so they
// stay consistent however these got there (toggle, restored state).
void SyncBuildingActionGroup(flecs::entity e, z13::input::ActionListener& action_listener) {
  SetActionGroup(action_listener, kBuildingActionGroup, e.has<BuildingTool>());
  const std::optional<flecs::entity> tool = HeldBuildTool(e);
  const bool wheel = tool && ToolModeInfoOf(tool->get<BuildTool>().mode).uses_wheel;
  SetActionGroup(action_listener, kWallActionGroup, wheel);
}

template <class Tag>
void SetPresent(flecs::entity e, bool present) {
  if (present && !e.has<Tag>()) {
    e.add<Tag>();
  } else if (!present && e.has<Tag>()) {
    e.remove<Tag>();
  }
}

// The mode key cycles the held tool's mode; get_mut, so this frame's actions see it at once.
ToolMode ApplyModeKey(flecs::entity tool, const z13::input::ActionListener& listener, const BuildActionIds& ids) {
  BuildTool& state = tool.get_mut<BuildTool>();
  if (IsSwitchedOn(listener, ids.cycle_tool_mode)) {
    state.mode = z13::NextEnumerator(state.mode);
  }
  return EffectiveToolMode(state.mode, IsHeld(listener, ids.cut_modifier), IsHeld(listener, ids.cut_in_modifier));
}

// The flip key turns the diagonal of a bridge or wall in the making; the bend key gives a wall a
// stick of its own at its second node (and takes it away again).
void TurnDraft(flecs::entity e, ToolMode mode, const z13::input::ActionListener& listener, const BuildActionIds& ids) {
  const bool flip = IsSwitchedOn(listener, ids.flip_diagonal);
  if (mode == ToolMode::kBridge && e.has<z13::station::WallBridgeDraft>() && flip) {
    z13::station::WallBridgeDraft& draft = e.get_mut<z13::station::WallBridgeDraft>();
    draft.flipped = !draft.flipped;
  }
  if (mode != ToolMode::kWall || !e.has<z13::station::WallDraft>()) {
    return;
  }
  z13::station::WallDraft& draft = e.get_mut<z13::station::WallDraft>();
  if (flip) {
    draft.flipped = !draft.flipped;
  }
  if (IsSwitchedOn(listener, ids.bend_wall)) {
    draft.bent = !draft.bent;
    if (!draft.bent && draft.stage == z13::station::WallDraft::Stage::kTip) {
      draft.stage = z13::station::WallDraft::Stage::kHeight;
    }
  }
}

// A click lays the next point of a wall, bridge or merge, or cuts an opening; the wheel sets the
// wall's or bridge's thickness, the opening's width or steps the merge target; the opening key
// switches a door and a window; the destroy key drops what is in the making (else it destroys,
// as in any mode).
void ApplyWallTool(
    flecs::entity e, flecs::entity tool, ToolMode mode, const z13::input::ActionListener& listener,
    const BuildActionIds& ids, const BuildingTuning& tuning) {
  BuildTool& state = tool.get_mut<BuildTool>();
  const int wheel =
      (IsSwitchedOn(listener, ids.thicker_wall) ? 1 : 0) - (IsSwitchedOn(listener, ids.thinner_wall) ? 1 : 0);
  if (mode == ToolMode::kWall || mode == ToolMode::kBridge) {
    state.wall_thickness = std::clamp(state.wall_thickness + wheel, 1, tuning.wall_max_thickness_cells);
  } else if (mode == ToolMode::kOpening) {
    state.opening_width = std::clamp(state.opening_width + wheel, 1, tuning.opening_max_width_steps);
    if (IsSwitchedOn(listener, ids.toggle_opening_kind)) {
      state.opening_kind = z13::NextEnumerator(state.opening_kind);
    }
  } else if (wheel != 0) {
    state.merge_at = z13::NextEnumerator(state.merge_at);
  }
  TurnDraft(e, mode, listener, ids);
  if (IsSwitchedOn(listener, ids.build_block)) {
    e.add<RequestBuildBlock>();
  }
  if (!IsSwitchedOn(listener, ids.destroy_block)) {
    return;
  }
  if (e.has<z13::station::WallDraft>() || e.has<z13::station::WallMergeDraft>() ||
      e.has<z13::station::WallBridgeDraft>()) {
    e.remove<z13::station::WallDraft>();
    e.remove<z13::station::WallMergeDraft>();
    e.remove<z13::station::WallBridgeDraft>();
  } else {
    e.add<RequestDestroyBlock>();
  }
}

void ApplyBuildActionListener(
    flecs::entity e,
    z13::input::ActionListener& action_listener,
    const BuildActionIds& build_action_ids,
    const BuildingTuning& tuning) {
  const std::optional<flecs::entity> tool = HeldBuildTool(e);
  SetPresent<BuildingTool>(e, tool.has_value());
  const std::optional<ToolMode> mode =
      tool ? std::optional(ApplyModeKey(*tool, action_listener, build_action_ids)) : std::nullopt;
  SetPresent<CutModifier>(e, mode == ToolMode::kCut);
  SetPresent<CutInModifier>(e, mode == ToolMode::kCutIn);

  if (mode != ToolMode::kWall && e.has<z13::station::WallDraft>()) {
    e.remove<z13::station::WallDraft>();
  }
  if (mode != ToolMode::kMerge && e.has<z13::station::WallMergeDraft>()) {
    e.remove<z13::station::WallMergeDraft>();
  }
  if (mode != ToolMode::kBridge && e.has<z13::station::WallBridgeDraft>()) {
    e.remove<z13::station::WallBridgeDraft>();
  }
  // Building/destroying only makes sense while the brush is out.
  if (!mode) {
    return;
  }
  if (ToolModeInfoOf(*mode).uses_wheel) {
    ApplyWallTool(e, *tool, *mode, action_listener, build_action_ids, tuning);
    return;
  }

  if (IsSwitchedOn(action_listener, build_action_ids.destroy_block) ||
      (mode == ToolMode::kDestroy && IsSwitchedOn(action_listener, build_action_ids.build_block))) {
    e.add<RequestDestroyBlock>();
  }
  if (mode == ToolMode::kDestroy) {
    if (e.has<z13::station::BrushDrag>()) {
      e.remove<z13::station::BrushDrag>();
    }
    return;
  }

  // Station blocks are placed on release, so a drag in between can size them.
  if (!e.world().has<z13::station::StationMode>()) {
    if (IsSwitchedOn(action_listener, build_action_ids.build_block)) {
      e.add<RequestBuildBlock>();
    }
  } else if (IsSwitchedOn(action_listener, build_action_ids.build_block)) {
    e.add<RequestBrushDrag>();
  } else if (IsSwitchedOn(action_listener, build_action_ids.cancel_brush_drag)) {
    e.remove<z13::station::BrushDrag>();
  } else if (IsSwitchedOff(action_listener, build_action_ids.build_block) && e.has<z13::station::BrushDrag>()) {
    e.add<RequestBuildBlock>();
  }
}

// Sent as an action, so the pick reaches the server and replays like a key press.
void SendPaletteChoice(
    z13::input::ActionListener& action_listener, const BuildActionIds& build_action_ids,
    z13::station::PaletteChoice& choice) {
  if (!choice.slot || !build_action_ids.select_primitive) {
    return;
  }
  if (*choice.slot < z13::station::kPaletteWindowSlots) {
    action_listener.action_values[*build_action_ids.select_primitive].current_value =
        static_cast<float>(*choice.slot + 1);
  }
  choice.slot.reset();
}

// Paused input reads as released, which would end a drag by placing its block.
void CancelPausedBrushDrag(z13::input::ActionListener& action_listener, const BuildActionIds& build_action_ids) {
  if (build_action_ids.cancel_brush_drag) {
    action_listener.action_values[*build_action_ids.cancel_brush_drag].current_value = 1.f;
  }
}

// The palette shows while its key is held with the brush out; the local player's input
// alone decides, since the cursor is the local machine's.
void SyncFreeCursor(flecs::entity e, const z13::input::ActionListener& action_listener, const BuildActionIds& ids) {
  const auto value = ids.show_palette ? action_listener.Value(*ids.show_palette) : std::nullopt;
  const bool held = value && value->current_value >= kHeldActionValue && e.has<BuildingTool>();
  flecs::world world = e.world();
  if (held && !world.has<z13::gameplay::FreeCursor>()) {
    world.add<z13::gameplay::FreeCursor>();
  } else if (!held && world.has<z13::gameplay::FreeCursor>()) {
    world.remove<z13::gameplay::FreeCursor>();
  }
}

// Clicks pick from the palette, so they build nothing; the release that ending a drag
// would read as is cancelled, like a pause's.
void SuppressBuildingWhileCursorFree(
    flecs::entity e, z13::input::ActionListener& action_listener, const BuildActionIds& ids) {
  for (const auto& action_id : {ids.build_block, ids.destroy_block}) {
    if (action_id) {
      action_listener.action_values[*action_id].current_value = 0.f;
    }
  }
  if (e.has<z13::station::BrushDrag>() && ids.cancel_brush_drag) {
    action_listener.action_values[*ids.cancel_brush_drag].current_value = 1.f;
  }
}

void RegisterComponents(flecs::world world) {
  z13::flecs_tools::RegisterComponents<BuildActionIds, z13::station::PaletteChoice>(world);
}

void RegisterSystems(flecs::world world) {
  world.set<z13::station::PaletteChoice>({});

  world.observer<z13::input::OnConfigUpdatedEvent, z13::input::ActionMap>()
      .event<z13::input::SystemInputEventType>()
      .each(OnConfigUpdated);

  // First phase of the frame, before the input values are routed by group.
  world.system<z13::input::ActionListener>("gameplay_input_system::SyncBuildingActionGroup")
      .kind<z13::input::ClearActionFramePhase>()
      .read<z13::inventory::ActiveItem>()
      .read<BuildTool>()
      .each(SyncBuildingActionGroup);

  world.system<z13::input::TouchControls>("gameplay_input_system::SyncTouchLayer")
      .kind<z13::input::ClearActionFramePhase>()
      .with<z13::input::CurrentActionListenerTag>()
      .read<z13::inventory::ActiveItem>()
      .read<BuildTool>()
      .read<z13::input::ItemTouchLayer>()
      .each(SyncTouchLayer);

  world.system<z13::input::ActionListener, BuildActionIds, const BuildingTuning>(
           "gameplay_input_system::ApplyBuildActionListener")
      .kind<z13::input::ApplyActionFramePhase>()
      .read<z13::station::StationMode>()
      .read<z13::inventory::ActiveItem>()
      .write<BuildTool>()
      .write<z13::station::BrushDrag>()
      .write<BuildingTool>()
      .write<CutModifier>()
      .write<CutInModifier>()
      .write<z13::station::WallDraft>()
      .write<z13::station::WallMergeDraft>()
      .write<z13::station::WallBridgeDraft>()
      .each(ApplyBuildActionListener);

  // After CalculateInputValues (same phase, registered earlier), before the recorder logs the frame.
  world.system<z13::input::ActionListener, const BuildActionIds, z13::station::PaletteChoice>(
           "gameplay_input_system::SendPaletteChoice")
      .kind<z13::input::CalculateActionFramePhase>()
      .with<z13::input::CurrentActionListenerTag>()
      .without<z13::gameplay::Pause>()
      .without<z13::flecs_tools::ReplayInProgress>()
      .each(SendPaletteChoice);

  world.system<const z13::input::ActionListener, const BuildActionIds>("gameplay_input_system::SyncFreeCursor")
      .kind<z13::input::CalculateActionFramePhase>()
      .with<z13::input::CurrentActionListenerTag>()
      .with<z13::station::StationMode>()
      .without<z13::flecs_tools::ReplayInProgress>()
      .write<z13::gameplay::FreeCursor>()
      .each(SyncFreeCursor);

  world.system<z13::input::ActionListener, const BuildActionIds>(
           "gameplay_input_system::SuppressBuildingWhileCursorFree")
      .kind<z13::input::CalculateActionFramePhase>()
      .with<z13::input::CurrentActionListenerTag>()
      .with<z13::gameplay::FreeCursor>()
      .without<z13::flecs_tools::ReplayInProgress>()
      .read<z13::station::BrushDrag>()
      .each(SuppressBuildingWhileCursorFree);

  world.system<z13::input::ActionListener, const BuildActionIds>("gameplay_input_system::CancelPausedBrushDrag")
      .kind<z13::input::CalculateActionFramePhase>()
      .with<z13::input::CurrentActionListenerTag>()
      .with<z13::station::BrushDrag>()
      .with<z13::gameplay::Pause>()
      .without<z13::flecs_tools::ReplayInProgress>()
      .each(CancelPausedBrushDrag);

  world.observer<z13::input::AppendInputSchema, z13::input::ActionMap>()
      .event<z13::input::AppendInputSchema>()
      .each(OnAppendInputSchema);
}

}  // namespace

void BuildingInputSystem::Register(flecs::world& world) {
  OnRegisterComponents(world, RegisterComponents);

  OnInitSystems(world, RegisterSystems);
}

}  // namespace z13::building
