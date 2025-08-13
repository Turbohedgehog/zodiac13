#include "z13_module.h"

#include <z13_module/components/z13.h>

#include <flecs.h>

namespace z13 {

void RegisterComponents(flecs::world& world) {
  world.component<Z13State>()
    .member<bool>("shutdown");

   world.component<PlayerInfoComponent>()
    .member<uint32_t>("id")
    .member<std::string>("login")
    .member<std::string>("name");
}

void CreateDefaults(flecs::world& world) {
  world.add<Z13State>();
}

Z13Module::Z13Module(flecs::world& world) {
  RegisterComponents(world);
  CreateDefaults(world);
}

}  // namespace z13
