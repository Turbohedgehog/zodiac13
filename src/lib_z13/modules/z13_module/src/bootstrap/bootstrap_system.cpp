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

#include "bootstrap_system.h"

#include <flecs.h>

#include <lib_core/components.h>
#include <lib_core/flecs_utils.h>
#include <lib_core/world_state.h>

#include <z13/components/bootstrap.h>
#include <z13/components/gameplay.h>
#include <z13/components/net.h>

namespace z13::bootstrap {

namespace {

struct BootstrapComponent {};
struct BootstrapCompleteComponent {
  using Singleton = void;
};

void RegisterComponents(flecs::world world) {
  world.entity().add<BootstrapComponent>();
  z13::flecs_tools::RegisterComponent<BootstrapCompleteComponent>(world);
  z13::flecs_tools::RegisterComponent<z13::net::ServerRole>(world);
  z13::flecs_tools::RegisterComponent<z13::net::ClientRole>(world);
}

void OnLoadConfig(flecs::entity e, const LoadConfigEvent&) {
  // Real input-config loading is handled separately by InputConfigLoader; this
  // step just closes the placeholder so it doesn't linger as a dangling tag.
  e.remove<LoadConfigEvent>();
}

// --server skips the main menu (no menu to show); --connect only sets the role for
// now -- there's no net session yet (a later branch) to act on a JoinRequest.
void OnSelectInitialState(flecs::entity e, const SelectInitialStateEvent&) {
  flecs::world world = e.world();
  const auto config = z13::GetCoreConfig(world);
  if (!config) {
    world.add<z13::gameplay::Pause>();
    e.remove<SelectInitialStateEvent>();
    return;
  }

  if (config->get().IsServer()) {
    world.add<z13::net::ServerRole>();
    world.add<z13::gameplay::Gameplay>();
  } else if (config->get().GetConnectEndpoint()) {
    world.add<z13::net::ClientRole>();
    world.add<z13::gameplay::Pause>();
  } else if (config->get().SkipMainMenu()) {
    world.add<z13::gameplay::Gameplay>();
  } else {
    world.add<z13::gameplay::Pause>();
  }
  e.remove<SelectInitialStateEvent>();
}

void InitBootstrap(flecs::entity e, const BootstrapComponent&) {
  // todo: завязать все загрузки и инициализации систем на последовательности, указанной здесь.
  // Последовательность будет расширена.
  e.add<LoadConfigEvent>();
  e.add<SelectInitialStateEvent>();
  e.world().add<BootstrapCompleteComponent>();
}

void RegisterSystems(flecs::world world) {
  world.observer<LoadConfigEvent>("BootstrapSystem::OnLoadConfig")
    .event(flecs::OnAdd)
    .each(OnLoadConfig);

  world.observer<SelectInitialStateEvent>("BootstrapSystem::OnSelectInitialState")
    .event(flecs::OnAdd)
    .each(OnSelectInitialState);

  world.system<BootstrapComponent>("InitBootstrap")
    .kind<z13::gameplay::PreUpdatePhase>()
    .without<BootstrapCompleteComponent>()
    .each(InitBootstrap);
}

}  // namespace

void BootstrapSystem::Register(flecs::world& world) {
  world.observer<z13::RegisterComponentsEvent>("BootstrapSystem::RegisterComponents")
    .event(flecs::OnAdd)
    .yield_existing()
    .each([world = world](const auto&) { RegisterComponents(world); });

  world.observer<z13::InitSystemsEvent>("BootstrapSystem::RegisterSystems")
    .event(flecs::OnAdd)
    .yield_existing()
    .each([world = world](const auto&) { RegisterSystems(world); });
}

}  // namespace z13::bootstrap
