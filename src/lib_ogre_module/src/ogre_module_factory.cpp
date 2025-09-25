#include <ogre_module/ogre_module_factory.h>

#include <flecs.h>

#include "ogre_module.h"
#include "render/render_module.h"

 void OgreModuleFactory::RegisterModules(flecs::world& world) {
  world.import<z13::ogre::OgreRender>();
  world.import<z13::ogre::GameplayRenderModule>();
 }

 const std::string& OgreModuleFactory::GetName() const {
  static std::string name = "OgreModuleFactory";

  return name;
}
