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

#include <bullet_module/bullet_module_factory.h>

#include <flecs.h>

#include <boost/dll/alias.hpp>

#include "bullet_module.h"

namespace z13::bullet_module {

ModuleFactoryPtr BulletModuleFactory::CreateFactory() {
  return std::make_shared<BulletModuleFactory>();
}

void BulletModuleFactory::RegisterModules(flecs::world& world) {
  world.import<BulletModule>();
}

const std::string& BulletModuleFactory::GetName() const {
  static std::string name = "BulletModuleFactory";

  return name;
}

}  // namespace z13::bullet_module

extern "C" {

BOOST_DLL_ALIAS(
    z13::bullet_module::BulletModuleFactory::CreateFactory,
    create_module_factory
)

}  // extern "C"
