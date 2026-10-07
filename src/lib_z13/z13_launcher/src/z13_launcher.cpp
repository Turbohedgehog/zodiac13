#include <z13_launcher/z13_launcher.h>

#include <filesystem>
#include <iostream>

#include <boost/dll.hpp>
#include <boost/dll/import.hpp>
#include <boost/dll/shared_library.hpp>

#include <lib_core/utils/log.h>
#include <lib_core/world/core.h>
#include <lib_core/world/module_factory_base.h>

#include <z13_launcher/module_list.h>
#include <z13_launcher/settings_loader.h>
#include <z13_launcher/station_scene_check.h>
#include <z13_settings/environment.h>
#include <z13_settings/settings.h>

namespace z13 {

namespace {

const std::filesystem::path kConfigRelativePath =
    std::filesystem::path("config") / "z13_config.yaml";
// Used instead of kConfigRelativePath for --server: no raylib_module (no display).
const std::filesystem::path kServerConfigRelativePath =
    std::filesystem::path("config") / "z13_config_server.yaml";
const std::filesystem::path kAssetsRelativePath = "assets";

}  // namespace

int Zodiac13Launcher::Run(int argc, char *argv[]) {
  spdlog::flush_on(spdlog::level::debug);
  spdlog::set_level(spdlog::level::debug);

  Core core(argc, argv, [](Config& config) { return AddSettingsOptions(config); });

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
  if (const auto scene = CheckStationScene(core.GetConfig(), exe_dir / kAssetsRelativePath); !scene) {
    log_error("{}", scene.error());
    return 1;
  }
  const auto config_path = exe_dir / (core.GetConfig().IsServer() ? kServerConfigRelativePath : kConfigRelativePath);

  const auto settings = ReadSettings(tools::environment::GetGameSettingsJsonPath());
  if (!settings) {
    log_error("{}", settings.error());
    return 1;
  }
  // File first, then the command line, which wins.
  const auto effective = ApplyCliOverrides(core.GetConfig(), *settings);
  if (!effective) {
    log_error("{}", effective.error());
    return 1;
  }
  core.GetConfig().SetCoreSettings(*effective->core);

  const auto modules = ReadModuleList(config_path);
  if (!modules) {
    log_error("{}", modules.error());
    return 1;
  }
  for (const auto& module_path : *modules) {
    if (const auto registered = core.RegisterModuleFactory(module_path); !registered) {
      log_error("{}", registered.error());
      return 1;  // a missing plugin leaves a broken game, not a smaller one
    }
  }

  const auto world = core.CreateWorld();
  if (!world) {
    log_error("{}", world.error());
    return 1;
  }
  InstallSettings(*world, *effective);

  return core.Run();
}

}  // namespace z13
