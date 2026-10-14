#include <z13_launcher/z13_launcher.h>

#include <iostream>

#include <lib_core/utils/asset_source.h>
#include <lib_core/utils/install_dir.h>
#include <lib_core/utils/log.h>
#include <lib_core/world/core.h>
#include <lib_core/world/module_factory_base.h>

#include <z13_launcher/module_list.h>
#include <z13_launcher/settings_loader.h>
#include <z13_launcher/station_assets_check.h>
#include <z13_settings/environment.h>
#include <z13_settings/settings.h>

namespace z13 {

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

  const AssetSource assets(InstallDir());
  const Status checked =
      CheckBlockPalette(assets).and_then([&] { return CheckStationScene(core.GetConfig(), assets); });
  if (!checked) {
    log_error("{}", checked.error());
    return 1;
  }
  core.SetAssetSource(assets);

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

  const auto modules =
      ReadModuleList(assets, core.GetConfig().IsServer() ? kServerModuleList : kGameModuleList);
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
