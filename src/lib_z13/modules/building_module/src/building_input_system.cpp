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
#include <z13/components/input.h>
#include <z13/components/building.h>
#include <z13/components/station.h>
#include <z13_module/input/input_config_loader.h>
#include <lib_core/state/rollback.h>

#include "build_action_ids.h"

namespace z13::building {

namespace {

// todo: убрать константу и брать из z13.fbs.building.Action.action_group
constexpr std::string_view kBuildingActionGroup = "Building";

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

  auto find_action_id = [&](const auto action_value) {
    auto action_id_holder = z13::gameplay::input::InputConfigLoader::FindActionId(
        action_map.action_map,
        "z13.fbs.building.Action",
        action_value);
    if (!action_id_holder) {
      log_error(
        "OnConfigUpdated: Cannot find action id '{}' for enum 'z13.fbs.building'",
        static_cast<EnumValueType>(action_value)
      );
    }

    return action_id_holder;
  };

  build_action_ids.toggle_building_mode = find_action_id(z13::fbs::building::Action::TOGGLE_BUILDING_MODE);
  build_action_ids.build_block = find_action_id(z13::fbs::building::Action::BUILD_BLOCK);
  build_action_ids.destroy_block = find_action_id(z13::fbs::building::Action::DESTROY_BLOK);
  build_action_ids.previous_primitive = find_action_id(z13::fbs::building::Action::PREVIOUS_PRIMITIVE);
  build_action_ids.next_primitive = find_action_id(z13::fbs::building::Action::NEXT_PRIMITIVE);
  constexpr std::array kSlotActions {
      z13::fbs::building::Action::SELECT_SLOT_1, z13::fbs::building::Action::SELECT_SLOT_2,
      z13::fbs::building::Action::SELECT_SLOT_3, z13::fbs::building::Action::SELECT_SLOT_4,
      z13::fbs::building::Action::SELECT_SLOT_5, z13::fbs::building::Action::SELECT_SLOT_6,
      z13::fbs::building::Action::SELECT_SLOT_7, z13::fbs::building::Action::SELECT_SLOT_8,
      z13::fbs::building::Action::SELECT_SLOT_9,
  };
  std::ranges::transform(kSlotActions, build_action_ids.select_slot.begin(), find_action_id);
  build_action_ids.rotate_around_z = find_action_id(z13::fbs::building::Action::ROTATE_AROUND_Z);
  build_action_ids.rotate_around_y = find_action_id(z13::fbs::building::Action::ROTATE_AROUND_Y);
  build_action_ids.rotate_around_x = find_action_id(z13::fbs::building::Action::ROTATE_AROUND_X);
  build_action_ids.select_primitive = find_action_id(z13::fbs::building::Action::SELECT_PRIMITIVE);
  build_action_ids.cancel_brush_drag = find_action_id(z13::fbs::building::Action::CANCEL_BRUSH_DRAG);
}

void ToggleBuildingMode(flecs::entity e) {
  // e.has<>() rather than a fetched BuildingTool* pointer: add/remove is deferred
  // within this iteration, so a pointer captured up front wouldn't reflect this entity's
  // own change.
  if (!e.has<BuildingTool>()) {
    e.add<BuildingTool>();
  } else {
    e.remove<BuildingTool>();
  }
}

// The Building input group follows the BuildingTool tag, so it stays consistent
// however the tag got there (toggle, restored state).
void SyncBuildingActionGroup(flecs::entity e, z13::input::ActionListener& action_listener) {
  auto& groups = action_listener.action_group_priority;
  const bool has_group = std::ranges::find(groups, kBuildingActionGroup) != groups.end();
  if (e.has<BuildingTool>() == has_group) {
    return;
  }

  if (has_group) {
    std::erase(groups, kBuildingActionGroup);
  } else {
    groups.emplace_back(kBuildingActionGroup);
  }
}

void ApplyBuildActionListener(
    flecs::entity e,
    z13::input::ActionListener& action_listener,
    const BuildActionIds& build_action_ids) {
  if (IsSwitchedOn(action_listener, build_action_ids.toggle_building_mode)) {
    ToggleBuildingMode(e);
  }

  // Building/destroying only makes sense while the brush is out.
  if (!e.has<BuildingTool>()) {
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

  if (IsSwitchedOn(action_listener, build_action_ids.destroy_block)) {
    e.add<RequestDestroyBlock>();
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
      .each(SyncBuildingActionGroup);

  world.system<z13::input::ActionListener, BuildActionIds>("gameplay_input_system::ApplyBuildActionListener")
      .kind<z13::input::ApplyActionFramePhase>()
      .read<z13::station::StationMode>()
      .write<z13::station::BrushDrag>()
      .write<BuildingTool>()
      .each(ApplyBuildActionListener);

  // After CalculateInputValues (same phase, registered earlier), before the recorder logs the frame.
  world.system<z13::input::ActionListener, const BuildActionIds, z13::station::PaletteChoice>(
           "gameplay_input_system::SendPaletteChoice")
      .kind<z13::input::CalculateActionFramePhase>()
      .with<z13::input::CurrentActionListenerTag>()
      .without<z13::gameplay::Pause>()
      .without<z13::flecs_tools::ReplayInProgress>()
      .each(SendPaletteChoice);

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
