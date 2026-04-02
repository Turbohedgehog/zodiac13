#include "ogre_system.h"

#include <lib_core/core.h>
#include <lib_core/log.h>

#include <ogre_module/ogre_components.h>

#include "ogre_tools/ogre_tools.h"
#include <ogre_module/ogre_datatypes.h>
#include <lib_core/components.h>
#include <ogre_module/ogre_components.h>
#include <z13/components/z13.h>
#include <z13/components/gameplay.h>
#include <z13/components/input.h>
#include <z13/components/geometry.h>

namespace z13::ogre {

void RegisterPipelines(flecs::world& world) {
  world.component<ReadEvents>().add(flecs::Phase).depends_on(flecs::PreFrame);
  world.get_alive(flecs::PreUpdate).add(flecs::Phase).depends_on<ReadEvents>();

  world.component<PreRender>().add(flecs::Phase).depends_on(flecs::OnStore);
  world.component<Render>().add(flecs::Phase).depends_on<PreRender>();
  world.component<PostRender>().add(flecs::Phase).depends_on<Render>();
  world.component<FinalizeRender>().add(flecs::Phase).depends_on<PostRender>();
  world.get_alive(flecs::PostFrame).add(flecs::Phase).depends_on<FinalizeRender>();
  // world.component<FinalizeRender>().add(flecs::Phase).depends_on<PostRender>();
}

void OnInit(flecs::world world, gameplay::Gameplay) {
  OgreData ogre_data;

  OgreTools::CreateSdlOgreRoot(world, ogre_data);

  auto init_entity = world.entity();
  world.set(ogre_data);
}

void Shutdown(flecs::entity e, OgreWindowClosed, OgreData& ogre_data) {
  OgreTools::DestroySdlOgreWindow(ogre_data);
  e.world().remove<OgreData>();
  e.world().get<CoreComponent>().core->get().Shutdown();
}

void OnAddCamera(flecs::entity e, const gameplay::Camera& camera, OgreData& ogre_data) {
  OgreTools::CreateCamera(e, camera, ogre_data);
}

void OgreSystem::Register(flecs::world& world) {
  RegisterPipelines(world);

  world.component<OgreData>().add(flecs::Singleton);

  world.system<OgreData>("ReadEventsSystem")
    .kind<ReadEvents>()
    .immediate()
    .each([world](auto& ogre_data) { OgreTools::ReadSdlEvents(world, ogre_data); });

  world.system<OgreData>("FinalizeRenderSystem")
    .kind<FinalizeRender>()
    .each([world](auto& ogre_data) { OgreTools::RenderSdlOgreWindow(world, ogre_data); });

  world.system<gameplay::Camera, geometry::Transform, OgreSceneNode>("UpdateCamera")
    .kind<PreRender>()
    .each(OgreTools::UpdateCamera);

  world.observer<OgreWindowClosed, OgreData>("ShutdownObserver")
    .event(flecs::OnAdd)
    .each(Shutdown);

  world.observer<OgreData, gameplay::Pause>("OgreTools::DisableRelativeMouseMode")
    .event(flecs::OnAdd)
    .yield_existing()
    .each(OgreTools::DisableRelativeMouseMode);

  world.observer<gameplay::Pause*>("OgreTools::EnableRelativeMouseMode")
    .event(flecs::OnRemove)
    .yield_existing()
    .each(OgreTools::EnableRelativeMouseMode);

  world.observer<const gameplay::Gameplay>("OgreSystem::OnInit")
    .event(flecs::OnAdd)
    .yield_existing()
    .each([world](const auto& gameplay) { OnInit(world, gameplay); });

  world.observer<const gameplay::Camera, OgreData>("OnAddCameraObserver")
    .event(flecs::OnAdd)
    .yield_existing()
    .each(OnAddCamera);
}

}  // namespace z13::ogre
