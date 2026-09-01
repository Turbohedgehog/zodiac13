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

module;

#include <filesystem>
#include <map>
#include <memory>
#include <utility>

#include <boost/dll.hpp>
#include <boost/dll/import.hpp>
#include <boost/dll/shared_library.hpp>

#include <lib_core/log.h>
#include <lib_core/module_factory_base.h>

module zodiac13.core;

import :module_lib_holder;

namespace z13 {

struct ModuleLibHolder::Impl {
  struct LibHolder {
    boost::dll::shared_library lib;
    ModuleFactoryPtr module_factory;
  };

  std::map<std::filesystem::path, LibHolder> lib_holders;
};

ModuleLibHolder::ModuleLibHolder() : impl_(std::make_unique<Impl>()) {}

ModuleLibHolder::~ModuleLibHolder() = default;

ModuleFactoryPtr ModuleLibHolder::AppendModuleLib(std::filesystem::path lib_path, bool append_platform_extension) {
  std::filesystem::path abs_path = boost::dll::program_location().parent_path().string();
  auto full_path = std::filesystem::absolute(abs_path / lib_path);
  if (append_platform_extension) {
    full_path.replace_extension(boost::dll::shared_library::suffix().native());
  }

  if (impl_->lib_holders.contains(full_path)) {
    LOG_CRITICAL("ModuleLibHolder::AppendModuleLib: lib '{}' already loaded!", full_path.string());
    return ModuleFactoryPtr();
  }

  if (!std::filesystem::exists(full_path)) {
    LOG_CRITICAL("ModuleLibHolder::AppendModuleLib: path '{}' does not exist!", full_path.string());
    return ModuleFactoryPtr();
  }

  boost::dll::fs::path boost_lib_path = full_path.string();
  boost::dll::shared_library lib(boost_lib_path, boost::dll::load_mode::load_with_altered_search_path);

  auto module_factory = lib.get_alias<ModuleFactoryPtr()>("create_module_factory")();
  impl_->lib_holders.emplace(
    full_path,
    Impl::LibHolder { .lib = std::move(lib), .module_factory = module_factory, }
  );

  LOG_INFO("ModuleLibHolder::AppendModuleLib: Module factory '{}' has been loaded", module_factory->GetName());

  auto module_factory_no_deleter = ModuleFactoryPtr(module_factory.get(), [](auto*){});

  return module_factory_no_deleter;
}

}  // namespace z13
