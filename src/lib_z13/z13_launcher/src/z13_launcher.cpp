#include <z13_launcher/z13_launcher.h>

#include <iostream>

#include <lib_core/time/tick_log.h>
#include <lib_core/utils/asset_source.h>
#include <lib_core/utils/install_dir.h>
#include <lib_core/utils/log.h>
#include <lib_core/world/core.h>
#include <lib_core/world/module_factory_base.h>

#include <z13_launcher/log_file.h>
#include <z13_launcher/module_list.h>
#include <z13_settings/settings_file.h>
#include <z13_launcher/station_assets_check.h>
#include <z13_settings/environment.h>
#include <z13_settings/settings.h>

namespace z13 {

namespace {

// The per-tick session entries are for debugging; a release log keeps what a player's report needs.
#ifdef NDEBUG
constexpr spdlog::level::level_enum kLogLevel = spdlog::level::info;
#else
constexpr spdlog::level::level_enum kLogLevel = spdlog::level::debug;
#endif

}  // namespace

int Zodiac13Launcher::Run(int argc, char *argv[]) {
  spdlog::flush_on(kLogLevel);
  spdlog::set_level(kLogLevel);

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
  // Settings come before the log file, which they can turn on; only their own errors miss it.
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

  std::optional<LogFileOption> log_file_option = core.GetConfig().GetLogFile();
  if (!log_file_option && effective->log_to_file) {
    log_file_option = LogFileOption {};
  }
  if (log_file_option) {
    const std::filesystem::path logs =
        log_file_option->directory.value_or(tools::environment::GetGameLogsDirectory());
    if (const Status removed = RemoveOldLogs(logs); !removed) {
      log_warn("{}", removed.error());
    }
    if (const auto log_file = AddLogFile(logs)) {
      log_info("Logging to {}", log_file->string());
    } else {
      log_warn("{}", log_file.error());
    }
  }

  const AssetSource assets(InstallDir());
  const Status checked =
      CheckBlockPalette(assets).and_then([&] { return CheckStationScene(core.GetConfig(), assets); });
  if (!checked) {
    log_error("{}", checked.error());
    return 1;
  }
  core.SetAssetSource(assets);

  const bool windowless = core.GetConfig().IsServer() || core.GetConfig().IsHeadless();
  const auto modules = ReadModuleList(assets, windowless ? kServerModuleList : kGameModuleList);
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
  flecs_tools::ShowTickInLog(*world);

  return core.Run();
}

}  // namespace z13
