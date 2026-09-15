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

#include <z13_module/z13_module_factory.h>

#include <flecs.h>

#include <z13/components/input.h>

#include "z13_module.h"

namespace z13 {

ModuleFactoryPtr Z13ModuleFactory::CreateFactory() {
  return std::make_shared<Z13ModuleFactory>();
}

void Z13ModuleFactory::RegisterModules(flecs::world& world) {
  world.import<Z13Module>();
  // Register the Singleton trait before set(): set() implicitly registers
  // the component and locks in its traits, so a later
  // .add(flecs::Singleton) in OnRegisterComponents would fail with
  // "component is already in use". The registration in OnRegisterComponents
  // becomes a no-op once this has already run.
  world.component<z13::input::InputConfigPersistenceSettings>().add(flecs::Singleton);
  world.set<z13::input::InputConfigPersistenceSettings>(
      {.use_disk = use_disk_for_input_config_});
}

const std::string& Z13ModuleFactory::GetName() const {
  static std::string name = "Z13ModuleFactory";

  return name;
}

void Z13ModuleFactory::SetUseDiskForInputConfig(bool use_disk) {
  use_disk_for_input_config_ = use_disk;
}

}  // namespace z13
