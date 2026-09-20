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

#include <utility>

#include <flecs.h>

#include <lib_core/world_state.h>

#include <z13/components/gameplay.h>
#include <z13/components/input.h>
#include <z13_module/tools/z13_environment.h>

#include "z13_module.h"

namespace z13 {

ModuleFactoryPtr Z13ModuleFactory::CreateFactory() {
  return std::make_shared<Z13ModuleFactory>();
}

void Z13ModuleFactory::RegisterModules(flecs::world& world) {
  world.import<Z13Module>();
  z13::flecs_tools::RegisterComponent<z13::input::InputConfigPersistenceSettings>(world);
  world.set<z13::input::InputConfigPersistenceSettings>(
      {.load_config_from_file = load_config_from_file_});

  z13::flecs_tools::RegisterComponent<z13::gameplay::QuickSaveSettings>(world);
  world.set<z13::gameplay::QuickSaveSettings>(
      {.path = quick_save_path_.value_or(z13::tools::environment::GetGameQuickSaveJsonPath())});
}

const std::string& Z13ModuleFactory::GetName() const {
  static std::string name = "Z13ModuleFactory";

  return name;
}

void Z13ModuleFactory::SetLoadConfigFromFile(bool load_config_from_file) {
  load_config_from_file_ = load_config_from_file;
}

void Z13ModuleFactory::SetQuickSavePath(std::filesystem::path path) {
  quick_save_path_ = std::move(path);
}

}  // namespace z13
