#include "bootstrap_system.h"

#include <flecs.h>

#include <z13/components/bootstrap.h>
#include <z13/components/gameplay.h>

namespace z13::bootstrap {

struct BootstrapComponent {};
struct BootstrapCompleteComponent {};

void RegisterComponents(flecs::world& world) {
  world.entity().add<BootstrapComponent>();
  world.component<BootstrapCompleteComponent>().add(flecs::Singleton);
}

void InitBootstrap(flecs::entity e, const BootstrapComponent&) {
  // todo: завязать все загрузки и инициализации систем на последовательности, указанной здесь.
  // Последовательность будет расширена.
  e.add<LoadConfigEvent>();
  e.add<CreatePlayerEvent>();

  e.world().add<BootstrapCompleteComponent>();
}

void BootstrapSystem::Register(flecs::world& world) {
  world.system<BootstrapComponent>("InitBootstrap")
    .kind<z13::gameplay::PreUpdatePhase>()
    .without<BootstrapCompleteComponent>()
    .each(InitBootstrap);
}

}