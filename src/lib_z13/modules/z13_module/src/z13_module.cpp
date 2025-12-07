#include "z13_module.h"

#include <z13_module/components/z13.h>
#include <z13_module/components/gameplay.h>
#include <z13_module/components/input.h>

#include <lib_core/log.h>

#include <flecs.h>

#include "gameplay/gameplay_system.h"
#include "gameplay/gameplay_input_system.h"

namespace z13 {

void RegisterComponents(flecs::world& world) {
  world.component<Z13State>()
    .member(flecs::Bool, "shutdown").add(flecs::Singleton);

  world.component<gameplay::Gameplay>().add(flecs::Singleton);
  world.component<gameplay::Pause>().add(flecs::Singleton);
  world.component<input::SystemInputEvent>();

  world.component<PlayerInfoComponent>()
    .member<uint32_t>("id")
    .member(flecs::String, "login")
    .member(flecs::String, "name");
}

void CreateDefaults(flecs::world& world) {
  LOG_INFO("~~~~ CreateDefaults 1");
  // world.add<z13::input::SystemInputListener>();
  LOG_INFO("~~~~ CreateDefaults 2");
  world.add<Z13State>();
  LOG_INFO("~~~~ CreateDefaults 3");
  world.add<z13::gameplay::Gameplay>();
  LOG_INFO("~~~~ CreateDefaults 4");
}

Z13Module::Z13Module(flecs::world& world) {
  RegisterComponents(world);
  z13::gameplay::GameplaySystem::Register(world);
  z13::gameplay::GameplayInputSystem::Register(world);
  CreateDefaults(world);
}

}  // namespace z13
