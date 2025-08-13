#include <ogre_module/ogre_module_factory.h>

#include <flecs.h>

#include "ogre_module.h"

namespace z13::ogre {

 void OgreModuleFactory::RegisterModules(flecs::world& world) {
  world.import<OgreRender>();
 }

 const std::string& OgreModuleFactory::GetName() const {
  static std::string name = "OgreModuleFactory";

  return name;
 }

}  // namespace z13::ogre
