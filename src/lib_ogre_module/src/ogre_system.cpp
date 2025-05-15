#include "ogre_system.h"

#include "ogre_tools/ogre_tools.h"
#include "ogre_tools/ogre_datatypes.h"

#include <lib_core/core.h>
#include <lib_core/world.h>
#include <lib_core/log.h>

namespace z13::ogre {

void OgreSystem::OnAddedToWorld(WorldPtr world) {
  LOG_INFO("~~~~ OgreSystem::OnAddedToWorld");
  SystemBase::OnAddedToWorld(world);

  // return;
  OgreData ogre_data;
  OgreTools::CreateSDLOgreRoot(ogre_data);

  world->CreateEntity<OgreData>(std::move(ogre_data));
}

void OgreSystem::Update(double delta_time) {
  // LOG_WARN("~~~~ OgreSystem::Update");
  SystemBase::Update(delta_time);
  GetWorld().lock()->GetECS().each([this](OgreData& ogre_data) {
    OgreTools::UpdateSDLOgreWindow(ogre_data);
    if (ogre_data.is_window_closed) {
      GetWorld().lock()->GetCore().Shutdown();
    }
  });
}

}  // namespace z13::ogre
