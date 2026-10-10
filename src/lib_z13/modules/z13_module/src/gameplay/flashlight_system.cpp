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

#include "flashlight_system.h"

#include <flecs.h>

#include <actions_generated.h>

#include <lib_core/state/world_state.h>
#include <lib_core/utils/log.h>
#include <lib_core/world/lifecycle.h>

#include <z13/components/gameplay.h>
#include <z13/components/input.h>
#include <z13_module/input/input_config_loader.h>

namespace z13::gameplay {

namespace {

void OnConfigUpdated(
    FlashlightActionId& id, z13::input::OnConfigUpdatedEvent, const z13::input::ActionMap& action_map) {
  id.toggle = z13::gameplay::input::InputConfigLoader::FindActionId(
      action_map.action_map, input::kActionsEnumName, z13::fbs::actions::Action::TOGGLE_FLASHLIGHT);
  if (!id.toggle) {
    log_error("Flashlight: cannot find its action in '{}'", input::kActionsEnumName);
  }
}

void ToggleFlashlight(
    Flashlight& flashlight, const z13::input::ActionListener& listener, const FlashlightActionId& id) {
  const auto value = id.toggle ? listener.Value(*id.toggle) : std::nullopt;
  if (value && value->IsSwitchedOn()) {
    flashlight.on = !flashlight.on;
  }
}

void RegisterComponents(flecs::world world) {
  z13::flecs_tools::RegisterComponent<FlashlightActionId>(world);
}

void RegisterSystems(flecs::world world) {
  // Set here, not next to `.add(flecs::Singleton)` (see PhysicsSystem::RegisterSystems).
  world.set<FlashlightActionId>({});

  world.observer<FlashlightActionId, z13::input::OnConfigUpdatedEvent, z13::input::ActionMap>(
           "FlashlightSystem::OnConfigUpdated")
      .event<z13::input::SystemInputEventType>()
      .each(OnConfigUpdated);

  world.system<Flashlight, const z13::input::ActionListener, const FlashlightActionId>(
           "FlashlightSystem::ToggleFlashlight")
      .kind<z13::input::ApplyActionFramePhase>()
      .each(ToggleFlashlight);
}

}  // namespace

void FlashlightSystem::Register(flecs::world& world) {
  OnRegisterComponents(world, RegisterComponents);
  OnInitSystems(world, RegisterSystems);
}

}  // namespace z13::gameplay
