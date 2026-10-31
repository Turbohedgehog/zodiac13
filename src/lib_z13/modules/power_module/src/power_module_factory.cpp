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


#include <power_module/power_module_factory.h>

#include <flecs.h>

#include <boost/dll/alias.hpp>

#include "power_module.h"

namespace z13::power_module {

ModuleFactoryPtr PowerModuleFactory::CreateFactory() {
  return std::make_shared<PowerModuleFactory>();
}

void PowerModuleFactory::RegisterModules(flecs::world& world) {
  world.import<PowerModule>();
}

std::string_view PowerModuleFactory::GetName() const {
  return "PowerModuleFactory";
}

}  // namespace z13::power_module

extern "C" {

BOOST_DLL_ALIAS(
    z13::power_module::PowerModuleFactory::CreateFactory,
    create_module_factory
)

}  // extern "C"
