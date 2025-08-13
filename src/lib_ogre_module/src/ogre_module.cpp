#include "ogre_module.h"

#include <flecs.h>

#include "components/ogre_components.h"

#include "ogre_system.h"

namespace z13::ogre {

void RegisterPipelines(flecs::world& world) {
  world.component<PreRender>().add(flecs::Phase).depends_on(flecs::OnStore);
  world.component<Render>().add(flecs::Phase).depends_on<PreRender>();
  world.component<PostRender>().add(flecs::Phase).depends_on<Render>();
}

void RegisterSystems(flecs::world& world) {
  world.system<CoreComponent, Z13State, OgreData>()
    .kind<Render>()
    .term_at(0).singleton()
    .term_at(1).singleton()
    .each(OgreSystem::Render);

  world.observer<Z13State>()
    .term_at(0).singleton()
    .event(flecs::OnAdd)
    .each(OgreSystem::Init);
}

OgreRender::OgreRender(flecs::world& world) {
  RegisterPipelines(world);
  RegisterSystems(world);
}

}  // namespace z13::ogre
