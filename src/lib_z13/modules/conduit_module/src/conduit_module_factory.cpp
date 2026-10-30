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


#include <conduit_module/conduit_module_factory.h>

#include <flecs.h>

#include <boost/dll/alias.hpp>

#include "conduit_module.h"

namespace z13::conduit {

ModuleFactoryPtr ConduitModuleFactory::CreateFactory() {
  return std::make_shared<ConduitModuleFactory>();
}

void ConduitModuleFactory::RegisterModules(flecs::world& world) {
  world.import<ConduitModule>();
}

std::string_view ConduitModuleFactory::GetName() const {
  return "ConduitModuleFactory";
}

}  // namespace z13::conduit

extern "C" {

BOOST_DLL_ALIAS(
    z13::conduit::ConduitModuleFactory::CreateFactory,
    create_module_factory
)

}  // extern "C"
