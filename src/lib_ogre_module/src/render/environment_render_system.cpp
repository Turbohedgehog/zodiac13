#include "environment_render_system.h"

#include <flecs.h>

#include <lib_core/log.h>
#include <z13_module/components/z13.h>

#include <ogre_module/ogre_components.h>

namespace z13::ogre {

// void OnRootAdded(flecs::iter& it, size_t i, OgreData& ogre_data) {
void OnRootAdded(flecs::entity e, OgreData& ogre_data) {
  // LOG_INFO("~~~~ CreateScene");
}

void EnvironmentRenderSystem::Register(flecs::world& world) {
  // LOG_INFO("~~~~ EnvironmentRenderSystem::Register");
  world.observer<OgreData>("OnRootAddedObserver")
    .event(flecs::OnSet)
    .each(OnRootAdded);
}

}  // namespace z13::ogre
