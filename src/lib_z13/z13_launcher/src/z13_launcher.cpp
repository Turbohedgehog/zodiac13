#include <z13_launcher/z13_launcher.h>

#include <spdlog/spdlog.h>

#include <iostream>

#include <lib_core/core.h>
#include <lib_core/log.h>
#include <z13_module/z13_module.h>
#include <ogre_module/ogre_module.h>

namespace z13 {

int Zodiac13Launcher::Run(int argc, char *argv[]) {
  std::cout << "std::cout: Hello from Zodiac 13!!!\n";

  spdlog::flush_on(spdlog::level::debug);
  spdlog::set_level(spdlog::level::debug);

  LOG_INFO("spdlog: Hello from Zodiac 13!!!");

  Core core(argc, argv);

  core.CreateModule<Z13Module>();
  core.CreateModule<z13::ogre::OgreRender>();

  core.InitModules();
  core.CreateWorld();

  return core.Run();
}

}  // namespace z13
