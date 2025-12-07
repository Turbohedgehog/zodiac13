#include "render_module.h"

#include <ogre_module/ogre_components.h>

#include "environment_render_system.h"
#include "gui_render_system.h"

namespace z13::ogre {

GameplayRenderModule::GameplayRenderModule(flecs::world& world) {
  EnvironmentRenderSystem::Register(world);
  RenderGuiSystem::Register(world);
}

}  // namespace z13::ogre
