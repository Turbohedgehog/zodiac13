#include "module_lib_holder.h"

#include <boost/dll.hpp>
#include <boost/dll/import.hpp>
#include <boost/dll/shared_library.hpp>

#include <lib_core/log.h>
#include <lib_core/module_factory_base.h>

namespace z13 {

ModuleFactoryPtr ModuleLibHolder::AppendModuleLib(std::filesystem::path lib_path, bool append_platform_extension) {
  if (append_platform_extension) {
    lib_path.replace_extension(boost::dll::shared_library::suffix().native());
  }

  if (lib_holders_.contains(lib_path)) {
    LOG_CRITICAL("ModuleLibHolder::AppendModuleLib: lib '{}' already loaded!", lib_path.string());
    return ModuleFactoryPtr();
  }

  if (!std::filesystem::exists(lib_path)) {
    LOG_CRITICAL("ModuleLibHolder::AppendModuleLib: path '{}' does not exist!", lib_path.string());
    return ModuleFactoryPtr();
  }

  boost::dll::fs::path boost_lib_path = lib_path.string();
  boost::dll::shared_library lib(boost_lib_path);

  auto module_factory = lib.get_alias<ModuleFactoryPtr()>("create_module_factory")();

  lib_holders_.emplace(
    lib_path,
    LibHolder { .lib = std::move(lib), .module_factory = module_factory, }
  );

  LOG_INFO("ModuleLibHolder::AppendModuleLib: Module factory '{}' has been loaded", module_factory->GetName());

  auto module_factory_no_deleter = ModuleFactoryPtr(module_factory.get(), [](auto*){});

  return module_factory_no_deleter;
}

}  // namespace z13
