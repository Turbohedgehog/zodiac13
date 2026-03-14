#include <z13_launcher/z13_launcher.h>

#include <filesystem>
#include <functional>
#include <vector>

#include <boost/dll.hpp>
#include <boost/dll/import.hpp>
#include <boost/dll/shared_library.hpp>
// #include <boost/dll/library_info.hpp>

// #include <spdlog/spdlog.h>

#include <iostream>

#include <lib_core/core.h>
#include <lib_core/log.h>
#include <lib_core/module_factory_base.h>
#include <z13_module/z13_module_factory.h>
#include <ogre_module/ogre_module_factory.h>

namespace z13 {



int Zodiac13Launcher::Run(int argc, char *argv[]) {
  spdlog::flush_on(spdlog::level::debug);
  spdlog::set_level(spdlog::level::debug);

  Core core(argc, argv);

  // core.RegisterModuleFactory("ogre_module");
  core.RegisterModuleFactory<Z13ModuleFactory>();
  core.RegisterModuleFactory<z13::ogre::OgreModuleFactory>();
  // core.RegisterModuleFactory("modules/ogre/ogre_module");
  
  core.RegisterModuleFactory("modules/test_dll/test_dll_module");
  core.CreateWorld();


  return core.Run();
}

}  // namespace z13
