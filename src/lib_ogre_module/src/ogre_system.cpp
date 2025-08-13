#include "ogre_system.h"

#include "ogre_tools/ogre_tools.h"
#include "ogre_tools/ogre_datatypes.h"

#include <lib_core/core.h>
#include <lib_core/log.h>

namespace z13::ogre {

void OgreSystem::Init(flecs::iter& iter, size_t i, Z13State& state) {
  OgreData ogre_data;
  OgreTools::CreateSDLOgreRoot(ogre_data);

  iter.world().entity().set(ogre_data);
}

void OgreSystem::Render(CoreComponent& core, Z13State& state, OgreData& ogre_data) {
  OgreTools::UpdateSDLOgreWindow(ogre_data);
  if (ogre_data.is_window_closed) {
    core.core->get().Shutdown();
  }
}

}  // namespace z13::ogre
