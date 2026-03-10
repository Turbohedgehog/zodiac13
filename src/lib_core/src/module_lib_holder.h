#pragma once

#include <filesystem>
#include <map>

#include <boost/dll/shared_library.hpp>

#include <lib_core/core_types.h>

namespace z13 {

struct LibHolder {
  boost::dll::shared_library lib;
  ModuleFactoryPtr module_factory;
};

class ModuleLibHolder {
 public:
  ModuleFactoryPtr AppendModuleLib(std::filesystem::path lib_path, bool append_platform_extension = true);

 private:
  std::map<std::filesystem::path, LibHolder> lib_holders_;
};

}  // namespace z13
