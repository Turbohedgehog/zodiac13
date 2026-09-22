#include <z13_launcher/z13_launcher.h>

#include <filesystem>
#include <iostream>

#include <boost/dll.hpp>
#include <boost/dll/import.hpp>
#include <boost/dll/shared_library.hpp>

#include <lib_core/core.h>
#include <lib_core/log.h>
#include <lib_core/module_factory_base.h>

#include <z13_launcher/module_list.h>

namespace z13 {

namespace {

const std::filesystem::path kConfigRelativePath =
    std::filesystem::path("config") / "z13_config.yaml";
// Used instead of kConfigRelativePath for --server: no raylib_module (no display).
const std::filesystem::path kServerConfigRelativePath =
    std::filesystem::path("config") / "z13_config_server.yaml";

}  // namespace

int Zodiac13Launcher::Run(int argc, char *argv[]) {
  spdlog::flush_on(spdlog::level::debug);
  spdlog::set_level(spdlog::level::debug);

  Core core(argc, argv);

  // Bail out before loading any module on --help or a rejected command line;
  // Core::Run() re-checks both for callers that construct a Core directly.
  if (core.GetConfig().NeedShowHelp()) {
    std::cout << core.GetConfig() << "\n";
    return 0;
  }
  if (const auto error = core.GetConfigError()) {
    log_error("{}", *error);
    return 1;
  }

  const std::filesystem::path exe_dir = boost::dll::program_location().parent_path().string();
  const auto config_path = exe_dir / (core.GetConfig().IsServer() ? kServerConfigRelativePath : kConfigRelativePath);

  const auto modules = ReadModuleList(config_path);
  if (!modules) {
    log_error("{}", modules.error());
    return 1;
  }
  for (const auto& module_path : *modules) {
    core.RegisterModuleFactory(module_path);
  }

  core.CreateWorld();

  return core.Run();
}

}  // namespace z13
