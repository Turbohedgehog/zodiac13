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

#include "render_module.h"

#include <ogre_module/ogre_components.h>

#include "environment_render_system.h"
#include "../gui_render/gui_system.h"

namespace z13::ogre {

GameplayRenderModule::GameplayRenderModule(flecs::world& world) {
  EnvironmentRenderSystem::Register(world);
  z13::ogre::gui::GuiSystem::Register(world);
}

}  // namespace z13::ogre
