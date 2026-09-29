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

#include "module_lib_holder.h"

#include <format>
#include <string_view>

#include <boost/dll.hpp>
#include <boost/dll/import.hpp>
#include <boost/dll/shared_library.hpp>

#include <lib_core/log.h>
#include <lib_core/module_factory_base.h>

namespace z13 {

namespace {

constexpr std::string_view kFactoryAlias = "create_module_factory";

}  // namespace

std::expected<ModuleFactoryPtr, std::string> ModuleLibHolder::AppendModuleLib(std::filesystem::path lib_path, bool append_platform_extension) {
  std::filesystem::path abs_path = boost::dll::program_location().parent_path().string();
  auto full_path = std::filesystem::absolute(abs_path / lib_path);
  if (append_platform_extension) {
    full_path.replace_extension(boost::dll::shared_library::suffix().native());
  }

  if (lib_holders_.contains(full_path)) {
    return std::unexpected(std::format("lib '{}' already loaded", full_path.string()));
  }

  if (!std::filesystem::exists(full_path)) {
    return std::unexpected(std::format("path '{}' does not exist", full_path.string()));
  }

  boost::dll::fs::path boost_lib_path = full_path.string();
  boost::dll::fs::error_code error;
  boost::dll::shared_library lib(boost_lib_path, boost::dll::load_mode::load_with_altered_search_path, error);
  if (error) {
    return std::unexpected(std::format("cannot load '{}': {}", full_path.string(), error.message()));
  }
  if (!lib.has(kFactoryAlias.data())) {
    return std::unexpected(std::format("'{}' has no '{}' alias", full_path.string(), kFactoryAlias));
  }

  auto module_factory = lib.get_alias<ModuleFactoryPtr()>(kFactoryAlias.data())();
  lib_holders_.emplace(
    full_path,
    LibHolder { .lib = std::move(lib), .module_factory = module_factory, }
  );

  log_info("ModuleLibHolder::AppendModuleLib: Module factory '{}' has been loaded", module_factory->GetName());

  auto module_factory_no_deleter = ModuleFactoryPtr(module_factory.get(), [](auto*){});

  return module_factory_no_deleter;
}

}  // namespace z13
