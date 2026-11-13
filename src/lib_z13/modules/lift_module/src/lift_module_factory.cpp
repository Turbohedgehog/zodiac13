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

#include <lift_module/lift_module_factory.h>

#include <flecs.h>

#include <boost/dll/alias.hpp>

#include "lift_module.h"

namespace z13::lift_module {

ModuleFactoryPtr LiftModuleFactory::CreateFactory() {
  return std::make_shared<LiftModuleFactory>();
}

void LiftModuleFactory::RegisterModules(flecs::world& world) {
  world.import<LiftModule>();
}

std::string_view LiftModuleFactory::GetName() const {
  return "LiftModuleFactory";
}

}  // namespace z13::lift_module

extern "C" {

BOOST_DLL_ALIAS(
    z13::lift_module::LiftModuleFactory::CreateFactory,
    create_module_factory
)

}  // extern "C"
