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

module;

#include <flecs.h>

module z13.ogre.module;

import z13.core;
import z13.components;
import z13.ogre.system;
import z13.ogre.building;

namespace z13::ogre {

OgreRender::OgreRender(flecs::world& world) {
  // RegisterPipelines(world);
  OgreSystem::Register(world);
  OgreBuildingSystem::Register(world);
  // gui::GuiSystem::Register(world);
}

}  // namespace z13::ogre
