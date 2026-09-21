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

#include "z13_module.h"

#include <cstdint>

#include <lib_core/components.h>
#include <lib_core/log.h>
#include <lib_core/world_state.h>

#include <z13/components/building.h>
#include <z13/components/z13.h>
#include <z13/components/status.h>
#include <z13/components/gameplay.h>
#include <z13/components/input.h>
#include <z13_module/gameplay/camera_look.h>
#include <z13_module/state/replay.h>

#include <flecs.h>

#include "bootstrap/bootstrap_system.h"
#include "gameplay/gameplay_system.h"
#include "input/gameplay_input_system.h"
#include "building/building_system.h"
#include "building/building_input_system.h"
#include "state/quick_save_input_system.h"
#include "state/player_action_recorder.h"

namespace z13 {

namespace {

void OnRegisterComponents(flecs::world world) {
  world.component<status::Z13State>()
    .member(flecs::Bool, "shutdown").add(flecs::Singleton);
  flecs_tools::RegisterComponents<
      gameplay::Gameplay, gameplay::IdCounters, gameplay::Player, gameplay::Camera,
      gameplay::PlayerCollider, gameplay::LookAngles, building::BuildingTool, building::BasicBlock,
      gameplay::Pause, input::ActionMap, input::InputConfig, input::InputConfigPersistenceSettings>(world);
  world.component<input::SystemInputEventType>();
  world.component<PlayerInfoComponent>()
    .member<uint32_t>("id")
    .member(flecs::String, "login")
    .member(flecs::String, "name");
}

void OnCreateDefaults(flecs::world world) {
  world.add<status::Z13State>();
  // Pause vs Gameplay is decided by BootstrapSystem from Config::SkipMainMenu().
  world.add<input::ActionMap>();
  world.add<input::InputConfig>();
  // Safety net default; add<T>() is a no-op if the factory already set a value.
  world.add<input::InputConfigPersistenceSettings>();
  world.add<z13::status::OnStartupGameEvent>();
}

}  // namespace

Z13Module::Z13Module(flecs::world& world) {
  world.observer<RegisterComponentsEvent>("Z13Module::OnRegisterComponents")
    .event(flecs::OnAdd)
    .yield_existing()
    .each([world = world](const auto&) { OnRegisterComponents(world); });

  world.observer<InitWorldDataEvent>("Z13Module::OnCreateDefaults")
    .event(flecs::OnAdd)
    .yield_existing()
    .each([world = world](const auto&) { OnCreateDefaults(world); });

  z13::bootstrap::BootstrapSystem::Register(world);
  z13::gameplay::GameplaySystem::Register(world);
  z13::gameplay::input::GameplayInputSystem::Register(world);
  z13::building::BuildingSystem::Register(world);
  z13::building::BuildingInputSystem::Register(world);
  z13::state::QuickSaveInputSystem::Register(world);
  z13::state::PlayerActionRecorder::Register(world);
  z13::state::Replay::Register(world);
}

}  // namespace z13
