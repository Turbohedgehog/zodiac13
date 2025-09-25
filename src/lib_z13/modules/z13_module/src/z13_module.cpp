#include "z13_module.h"

#include <z13_module/components/z13.h>
#include <z13_module/components/gameplay.h>

#include <lib_core/log.h>

#include <flecs.h>

#include "gameplay/gameplay_system.h"

namespace z13 {

void RegisterComponents(flecs::world& world) {
  world.component<Z13State>()
    .member(flecs::Bool, "shutdown");

  world.component<gameplay::Gameplay>();

  world.component<PlayerInfoComponent>()
    .member<uint32_t>("id")
    .member(flecs::String, "login")
    .member(flecs::String, "name");
}

void CreateDefaults(flecs::world& world) {
  LOG_INFO("~~~~ CreateDefaults");
  world.add<Z13State>();
  world.add<gameplay::Gameplay>();
}

Z13Module::Z13Module(flecs::world& world) {
  RegisterComponents(world);
  z13::gameplay::GameplaySystem::Register(world);
  CreateDefaults(world);
}

}  // namespace z13
