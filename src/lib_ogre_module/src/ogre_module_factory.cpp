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
#include <boost/config.hpp>
#include <boost/dll/alias.hpp>

module z13.ogre.ogre_module_factory;

import std.compat;

import z13.ogre.ogre_module;
import z13.ogre.render_module;

namespace z13::ogre {

void OgreModuleFactory::RegisterModules(flecs::world& world) {
  world.import<z13::ogre::OgreRender>();
  world.import<z13::ogre::GameplayRenderModule>();
}

const std::string& OgreModuleFactory::GetName() const {
  static std::string name = "OgreModuleFactory";

  return name;
}

ModuleFactoryPtr OgreModuleFactory::CreateFactory() {
  return std::make_shared<OgreModuleFactory>();
}

}  // namespace z13::ogre

BOOST_DLL_ALIAS(
    z13::ogre::OgreModuleFactory::CreateFactory,
    create_module_factory
)