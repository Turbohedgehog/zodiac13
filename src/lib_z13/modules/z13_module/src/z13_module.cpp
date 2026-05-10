#include "z13_module.h"

#include <z13/components/z13.h>
#include <z13/components/status.h>
#include <z13/components/gameplay.h>
#include <z13/components/input.h>

#include <lib_core/log.h>

#include <flecs.h>

#include "bootstrap/bootstrap_system.h"
#include "gameplay/gameplay_system.h"
#include "input/gameplay_input_system.h"
#include "building/building_system.h"
#include "building/building_input_system.h"

namespace z13 {

void RegisterTypes(flecs::world& world) {
  // flecs::enum_type<z13::input::PlayerAction>(world);
}

void RegisterComponents(flecs::world& world) {
  world.component<status::Z13State>()
    .member(flecs::Bool, "shutdown").add(flecs::Singleton);

  world.component<gameplay::Gameplay>().add(flecs::Singleton);
  world.component<gameplay::Pause>().add(flecs::Singleton);
  world.component<input::SystemInputEventType>();
  world.component<input::ActionMap>().add(flecs::Singleton);
  world.add<input::ActionMap>();
  world.component<input::InputConfig>().add(flecs::Singleton);
  world.add<input::InputConfig>();

  world.component<PlayerInfoComponent>()
    .member<uint32_t>("id")
    .member(flecs::String, "login")
    .member(flecs::String, "name");
}

void CreateDefaults(flecs::world& world) {
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

Z13Module::Z13Module(flecs::world& world) {
  RegisterTypes(world);
  RegisterComponents(world);
  z13::bootstrap::BootstrapSystem::Register(world);
  z13::gameplay::GameplaySystem::Register(world);
  z13::gameplay::input::GameplayInputSystem::Register(world);
  z13::building::BuildingSystem::Register(world);
  z13::building::BuildingInputSystem::Register(world);
  CreateDefaults(world);
}

}  // namespace z13
