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

#pragma once

#include <map>
#include <string>
#include <memory>
#include <vector>
#include <filesystem>

#include "core_types.h"
#include "config.h"

#include <flecs.h>

namespace z13 {

class Core {
 public:
  Core(int argc, char *argv[]);
  ~Core();  // for forward declared unique_ptr
  const Config& GetConfig() const;
  bool RegisterModuleFactory(ModuleFactoryPtr module_factory);
  WorldRef CreateWorld();

  template <typename T, typename... Ts>
  bool RegisterModuleFactory(Ts&&... params) {
    return RegisterModuleFactory(std::make_shared<T>(std::forward<Ts>(params)...));
  }

  bool RegisterModuleFactory(const std::filesystem::path& module_lib_path, bool append_platform_extension = true);

  void Update(float delta_time);
  int Run();
  void Shutdown();
  bool IsPendingShutDown() const;

 private:
  Config config_;

  std::map<WorldId, flecs::world> worlds_;
  WorldId new_world_id_ = 0;
  // std::map<std::string, ModuleFactoryPtr> module_factories_;
  std::vector<ModuleFactoryPtr> module_factories_;
  bool pending_shutdown_ = false;
  std::unique_ptr<ModuleLibHolder> module_lib_holder_;
  // std::unique_ptr<ModuleLibHolder> module_lib_holder_;
};

}  // namespace z13
