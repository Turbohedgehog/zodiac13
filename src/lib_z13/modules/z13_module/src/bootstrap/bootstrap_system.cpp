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

#include <lib_core/state/component_meta.h>
#include <lib_core/state/world_state.h>
#include <lib_core/utils/flecs_utils.h>
#include <lib_core/world/components.h>
#include <lib_core/world/lifecycle.h>

#include <z13/components/bootstrap.h>
#include <z13/components/gameplay.h>
#include <z13/components/net.h>
#include <z13/components/station.h>

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
  world.component<z13::net::StartServerRequest>();
  world.component<z13::net::JoinRequest>();
  world.component<z13::net::LeaveRequest>();
  z13::flecs_tools::RegisterComponent<z13::net::ConnectionStatus>(world);
  // BlockSpec first: Block and BlockBrush hold one.
  z13::flecs_tools::RegisterComponentMeta<z13::station::BlockSpec>(world);
  z13::flecs_tools::RegisterComponents<z13::station::StationMode, z13::station::StationSceneChoice,
                                       z13::station::SpawnPoint, z13::station::Block, z13::station::BlockBrush>(world);
  world.component<z13::gameplay::PopulateSceneEvent>();
}

void OnLoadConfig(flecs::entity e, const LoadConfigEvent&) {
  // Real input-config loading is handled separately by InputConfigLoader; this
  // step just closes the placeholder so it doesn't linger as a dangling tag.
  e.remove<LoadConfigEvent>();
}

// --server skips the main menu: net_module opens the transport and starts Gameplay.
// --connect keeps the menu (Pause) up until Welcome/Rejected resolves the JoinRequest.
// --station sets the mode before either kind of host starts its scene.
void OnSelectInitialState(flecs::entity e, const SelectInitialStateEvent&) {
  flecs::world world = e.world();
  const auto config = z13::GetCoreConfig(world);
  if (!config) {
    world.add<z13::gameplay::Pause>();
    e.remove<SelectInitialStateEvent>();
    return;
  }

  if (config->get().IsStation()) {
    world.add<z13::station::StationMode>();
    world.set(z13::station::StationSceneChoice {.scene = config->get().GetStationScene()});
  }

  if (config->get().IsServer()) {
    world.entity().set<z13::net::StartServerRequest>({.port = config->get().GetPort()});
  } else if (const auto endpoint = config->get().GetConnectEndpoint()) {
    world.add<z13::gameplay::Pause>();
    world.entity().set<z13::net::JoinRequest>({.endpoint = *endpoint});
  } else if (config->get().SkipMainMenu() || config->get().IsStation()) {
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
  // Set here, not next to its registration (see PhysicsSystem::RegisterSystems). The menu
  // reads it before any session has written it.
  world.set<z13::net::ConnectionStatus>({});
  world.set<z13::station::StationSceneChoice>({});

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
  z13::OnRegisterComponents(world, RegisterComponents);

  z13::OnInitSystems(world, RegisterSystems);
}

}  // namespace z13::bootstrap
