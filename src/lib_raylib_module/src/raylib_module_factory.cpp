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

#include <raylib_module/raylib_module_factory.h>

#include <flecs.h>

#include "raylib_module.h"

namespace z13::raylib {

void RaylibModuleFactory::RegisterModules(flecs::world& world) {
  world.import<z13::raylib::RaylibRender>();
}

const std::string& RaylibModuleFactory::GetName() const {
  static std::string name = "RaylibModuleFactory";
  return name;
}

ModuleFactoryPtr RaylibModuleFactory::CreateFactory() {
  return std::make_shared<RaylibModuleFactory>();
}

}  // namespace z13::raylib
