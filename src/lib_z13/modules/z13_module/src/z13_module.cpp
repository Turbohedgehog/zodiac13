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

#include <lib_core/components.h>
#include <lib_core/log.h>

#include <z13/components/z13.h>
#include <z13/components/status.h>
#include <z13/components/gameplay.h>
#include <z13/components/input.h>

#include <flecs.h>

#include "bootstrap/bootstrap_system.h"
#include "gameplay/gameplay_system.h"
#include "input/gameplay_input_system.h"
#include "building/building_system.h"
#include "building/building_input_system.h"

namespace z13 {

namespace {

void OnRegisterComponents(flecs::world world) {
  world.component<status::Z13State>()
    .member(flecs::Bool, "shutdown").add(flecs::Singleton);
  world.component<gameplay::Gameplay>().add(flecs::Singleton);
  world.component<gameplay::Pause>().add(flecs::Singleton);
  world.component<input::SystemInputEventType>();
  world.component<input::ActionMap>().add(flecs::Singleton);
  world.component<input::InputConfig>().add(flecs::Singleton);
  world.component<PlayerInfoComponent>()
    .member<uint32_t>("id")
    .member(flecs::String, "login")
    .member(flecs::String, "name");
}

void OnCreateDefaults(flecs::world world) {
  world.add<status::Z13State>();
  world.add<z13::gameplay::Gameplay>();
  world.add<input::ActionMap>();
  world.add<input::InputConfig>();
  world.add<z13::status::OnStartupGameEvent>();
}

void CreateDefaults(flecs::world world) {
  // LOG_INFO("~~~~ CreateDefaults 1");
  // world.add<z13::input::SystemInputListener>();
  // LOG_INFO("~~~~ CreateDefaults 2");
  world.add<status::Z13State>();
  // LOG_INFO("~~~~ CreateDefaults 3");
  world.add<z13::gameplay::Gameplay>();
  // LOG_INFO("~~~~ CreateDefaults 4");
  world.add<z13::status::OnStartupGameEvent>();
  // LOG_INFO("~~~~ CreateDefaults 5");
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
}

}  // namespace z13
