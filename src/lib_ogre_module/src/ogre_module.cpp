#include "ogre_module.h"

#include <flecs.h>

#include <ogre_module/ogre_components.h>

#include "ogre_system.h"

namespace z13::ogre {

void RegisterPipelines(flecs::world& world) {
  world.component<ReadEvents>().add(flecs::Phase).depends_on(flecs::PreFrame);
  world.component<PreRender>().add(flecs::Phase).depends_on(flecs::OnStore);
  world.component<Render>().add(flecs::Phase).depends_on<PreRender>();
  world.component<PostRender>().add(flecs::Phase).depends_on<Render>();
  // world.component<PreRenderGui>().add(flecs::Phase).depends_on<Render>();
  // world.component<RenderGui>().add(flecs::Phase).depends_on<PreRenderGui>();
  // world.component<PostRenderGui>().add(flecs::Phase).depends_on<RenderGui>();
  world.component<FinalizeRender>().add(flecs::Phase).depends_on<PostRender>();
}

OgreRender::OgreRender(flecs::world& world) {
  RegisterPipelines(world);
  OgreSystem::Register(world);
}

}  // namespace z13::ogre
