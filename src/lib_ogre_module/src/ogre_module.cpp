#include <ogre_module/ogre_module.h>

#include <lib_core/world.h>

#include "ogre_system.h"

namespace z13::ogre {

const std::string& OgreRender::GetName() const {
  static const std::string kName = "OgreRender";
  
  return kName;
}

void OgreRender::OnWorldCreated(WorldWeakPtr world) {
  ModuleBase::OnWorldCreated(world);

  world.lock()->CreateSystem<OgreSystem>();
}

}  // namespace z13::ogre
