#include "ogre_system.h"

#include <lib_core/core.h>
#include <lib_core/log.h>

#include <ogre_module/ogre_components.h>

#include "ogre_tools/ogre_tools.h"
#include <ogre_module/ogre_datatypes.h>
#include <lib_core/components.h>
#include <ogre_module/ogre_components.h>
#include <z13_module/components/z13.h>
#include <z13_module/components/gameplay.h>
#include <z13_module/components/input.h>
#include <z13_module/components/geometry.h>

namespace z13::ogre {

void OnInit(flecs::world world, gameplay::Gameplay) {
  OgreData ogre_data;

  OgreTools::CreateSdlOgreRoot(world, ogre_data);

  auto init_entity = world.entity();
  world.set(ogre_data);
}

void Shutdown(flecs::entity e, OgreWindowClosed) {
  e.world().get<CoreComponent>().core->get().Shutdown();
}

void OnAddCamera(flecs::entity, const gameplay::Camera& camera, OgreData& ogre_data) {
  OgreTools::CreateCamera(camera, ogre_data);
  // LOG_INFO("~~~~ OnAddCamera");
}

// void OnSetCamera(flecs::entity e, const gameplay::Camera& camera) {
//   // LOG_INFO("~~~~ OnSetCamera");
// }

void OgreSystem::Register(flecs::world& world) {
  world.component<OgreData>().add(flecs::Singleton);

  world.system<OgreData>("ReadEventsSystem")
    .kind<ReadEvents>()
    // .term_at(0).singleton()
    .immediate()
    .each([world](auto& ogre_data) { OgreTools::ReadSdlEvents(world, ogre_data); });

  // world.system<>("PreRenderSystem")
  //   .kind<PreRender>()
  //   .each([]() {
  //     LOG_INFO("~~~~ PreRender");
  //   });

  world.system<OgreData>("FinalizeRenderSystem")
    .kind<FinalizeRender>()
    // .term_at(0).singleton()
    .each([world](auto& ogre_data) { OgreTools::RenderSdlOgreWindow(world, ogre_data); });

  world.system<gameplay::Camera, geometry::Transform, OgreData>("UpdateCamera")
    .kind<PreRender>()
    .each(OgreTools::UpdateCamera);

  world.observer<OgreWindowClosed>("ShutdownObserver")
    .event(flecs::OnAdd)
    .each(Shutdown);

  world.observer<gameplay::Pause*>("OgreTools::DisableRelativeMouseMode")
    .event(flecs::OnAdd)
    .each(OgreTools::DisableRelativeMouseMode);

  world.observer<gameplay::Pause*>("OgreTools::EnableRelativeMouseMode")
    .event(flecs::OnRemove)
    .each(OgreTools::EnableRelativeMouseMode);

  world.observer<const gameplay::Gameplay>("OgreSystem::OnInit")
    // .term_at(0).singleton()
    .event(flecs::OnAdd)
    .yield_existing()
    .each([world](const auto& gameplay) { OnInit(world, gameplay); });

  world.observer<const gameplay::Camera, OgreData>("OnAddCameraObserver")
    .event(flecs::OnAdd)
    .yield_existing()
    .each(OnAddCamera);

  // world.observer<const gameplay::Camera>("OnSetCameraObserver")
  //   .event(flecs::OnSet)
  //   .each(OnSetCamera);
}

}  // namespace z13::ogre
