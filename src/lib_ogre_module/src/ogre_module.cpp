#include "ogre_module.h"

#include <flecs.h>

#include <ogre_module/ogre_components.h>

#include "ogre_system.h"
#include "gui_render/gui_system.h"

namespace z13::ogre {

OgreRender::OgreRender(flecs::world& world) {
  // RegisterPipelines(world);
  OgreSystem::Register(world);
  // gui::GuiSystem::Register(world);
}

}  // namespace z13::ogre
