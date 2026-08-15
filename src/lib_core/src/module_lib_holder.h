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

#include <filesystem>
#include <map>
#include <memory>

#include <boost/dll/shared_library.hpp>

#include <lib_core/module_factory_base.h>

namespace z13 {

class ModuleFactoryBase;

struct LibHolder {
  boost::dll::shared_library lib;
  std::shared_ptr<ModuleFactoryBase> module_factory;
};

class ModuleLibHolder {
 public:
  ModuleFactoryPtr AppendModuleLib(std::filesystem::path lib_path, bool append_platform_extension = true);

 private:
  std::map<std::filesystem::path, LibHolder> lib_holders_;
};

}  // namespace z13
