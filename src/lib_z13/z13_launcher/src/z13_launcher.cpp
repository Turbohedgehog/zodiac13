#include <z13_launcher/z13_launcher.h>

#include <filesystem>
#include <functional>
#include <vector>

#include <boost/dll.hpp>
#include <boost/dll/import.hpp>
#include <boost/dll/shared_library.hpp>

#include <iostream>

#include <lib_core/log.h>

import z13.core;

namespace z13 {



int Zodiac13Launcher::Run(int argc, char *argv[]) {
  spdlog::flush_on(spdlog::level::debug);
  spdlog::set_level(spdlog::level::debug);

  Core core(argc, argv);

  core.RegisterModuleFactory("modules/z13_module/z13_module");
  core.RegisterModuleFactory("modules/ogre/ogre_module");
  core.RegisterModuleFactory("modules/test_dll/test_dll_module");

  core.CreateWorld();

  return core.Run();
}

}  // namespace z13
