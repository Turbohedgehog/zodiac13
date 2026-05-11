/*
 * Copyright 2026 Ivan Kulenko / Zodiac13
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://apache.org
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "environment_render_system.h"

#include <flecs.h>

#include <lib_core/log.h>
#include <z13/components/z13.h>

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
