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

#include <gravity_module/gravity_module_factory.h>

#include <flecs.h>

#include <boost/dll/alias.hpp>

#include "gravity_module.h"

namespace z13::gravity_module {

ModuleFactoryPtr GravityModuleFactory::CreateFactory() {
  return std::make_shared<GravityModuleFactory>();
}

void GravityModuleFactory::RegisterModules(flecs::world& world) {
  world.import<GravityModule>();
}

std::string_view GravityModuleFactory::GetName() const {
  return "GravityModuleFactory";
}

}  // namespace z13::gravity_module

extern "C" {

BOOST_DLL_ALIAS(
    z13::gravity_module::GravityModuleFactory::CreateFactory,
    create_module_factory
)

}  // extern "C"
