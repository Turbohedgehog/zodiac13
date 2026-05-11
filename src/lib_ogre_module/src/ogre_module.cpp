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

#include "ogre_module.h"

#include <flecs.h>

#include <ogre_module/ogre_components.h>

#include "ogre_system.h"
#include "building/ogre_building_system.h"
#include "gui_render/gui_system.h"

namespace z13::ogre {

OgreRender::OgreRender(flecs::world& world) {
  // RegisterPipelines(world);
  OgreSystem::Register(world);
  OgreBuildingSystem::Register(world);
  // gui::GuiSystem::Register(world);
}

}  // namespace z13::ogre
