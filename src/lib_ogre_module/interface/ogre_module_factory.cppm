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

#include <boost/config.hpp>
#include <boost/dll/alias.hpp>

export module z13.ogre.ogre_module_factory;

import std.compat;
export import z13.core.module_factory_base;

extern "C" {

export namespace z13::ogre {

class BOOST_SYMBOL_VISIBLE OgreModuleFactory : public z13::ModuleFactoryBase {
 public:
  static ModuleFactoryPtr CreateFactory();

  void RegisterModules(flecs::world& world) override;
  const std::string& GetName() const override;
};

}  // namespace z13::ogre

BOOST_DLL_ALIAS(
    z13::ogre::OgreModuleFactory::CreateFactory,
    create_module_factory
)

}  // extern "C"
