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

namespace z13::ogre {

void OnInit(flecs::iter& iter, size_t i, const gameplay::Gameplay& gameplay) {
  LOG_INFO("~~~~ ogre::Init");

  OgreData ogre_data;
  SdlInput input;

  OgreTools::CreateSdlOgreRoot(iter.world(), ogre_data, input);

  auto init_entity = iter.world().entity();
  init_entity.set(ogre_data).set(input);
}

void Shutdown(flecs::entity e, OgreWindowClosed) {
  e.world().get<CoreComponent>().core->get().Shutdown();
}

void OnAddCamera(flecs::entity c, const gameplay::Camera& camera) {
  LOG_INFO("~~~~ OnAddCamera");
}

void OnSetCamera(flecs::entity e, const gameplay::Camera& camera) {
  LOG_INFO("~~~~ OnSetCamera");
}

void OgreSystem::Register(flecs::world& world) {
  world.system<OgreData, SdlInput>()
    .kind<ReadEvents>()
    .each(OgreTools::ReadSdlEvents);

  world.system<OgreData>()
    .kind<FinalizeRender>()
    .each(OgreTools::RenderSdlOgreWindow);

  world.observer<OgreWindowClosed>()
    .event(flecs::OnAdd)
    .each(Shutdown);

  world.observer<gameplay::Pause*>()
    .event(flecs::OnAdd)
    .each(OgreTools::DisableRelativeMouseMode);
  world.observer<gameplay::Pause*>()
    .event(flecs::OnRemove)
    .each(OgreTools::EnableRelativeMouseMode);

  world.observer<const gameplay::Gameplay>()
    .term_at(0).singleton()
    .event(flecs::OnAdd)
    .each(OnInit);

  world.observer<const gameplay::Camera>()
    .event(flecs::OnAdd)
    .each(OnAddCamera);

  world.observer<const gameplay::Camera>()
    .event(flecs::OnSet)
    .each(OnSetCamera);
}

}  // namespace z13::ogre
