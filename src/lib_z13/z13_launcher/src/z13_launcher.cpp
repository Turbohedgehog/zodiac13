#include <z13_launcher/z13_launcher.h>

#include <expected>
#include <filesystem>
#include <format>
#include <iostream>
#include <memory>

#include <lib_core/commands/command_registry.h>
#include <lib_core/state/world_snapshot_history.h>
#include <lib_core/time/tick_log.h>
#include <lib_core/utils/asset_pack.h>
#include <lib_core/utils/asset_source.h>
#include <lib_core/utils/file_pack_reader.h>
#include <lib_core/utils/console_log.h>
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

const std::filesystem::path kAssetsDir = "assets";

// A development build keeps assets/ beside the executable; a package, only the archive.
std::expected<AssetSource, std::string> OpenInstalledAssets() {
  const std::filesystem::path install_dir = InstallDir();
  if (std::filesystem::is_directory(install_dir / kAssetsDir)) {
    return AssetSource(install_dir);
  }
  auto pack = AssetPack::Open(std::make_unique<FilePackReader>(install_dir / kAssetPackFile));
  if (!pack) {
    return std::unexpected(std::format("no assets next to the executable: {}", pack.error()));
  }
  return AssetSource(std::make_shared<const AssetPack>(*std::move(pack)));
}

}  // namespace

int Zodiac13Launcher::Run(int argc, char *argv[]) {
  spdlog::flush_on(kLogLevel);
  spdlog::set_level(kLogLevel);
  const ConsoleLog console_log;
  spdlog::default_logger()->sinks().push_back(console_log.Sink());

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

  const auto opened = assets_ ? std::expected<AssetSource, std::string>(*assets_) : OpenInstalledAssets();
  if (!opened) {
    log_error("{}", opened.error());
    return 1;
  }
  const AssetSource& assets = *opened;
  const Status checked =
      CheckBlockPalette(assets).and_then([&] { return CheckStationScene(core.GetConfig(), assets); });
  if (!checked) {
    log_error("{}", checked.error());
    return 1;
  }
  core.SetAssetSource(assets);
  core.SetConsoleLog(console_log);

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
  for (const std::string& name : CommandsWithoutHandler(world->get())) {
    log_warn("Command '{}' has no handler", name);
  }
  InstallSettings(*world, *effective);
  flecs_tools::ShowTickInLog(*world);
  // Only a network session rolls back, and it sets its own rate: playing alone takes no snapshots.
  world->get().set<flecs_tools::SnapshotCaptureRate>({.per_interval = 0});

  return core.Run();
}

}  // namespace z13
