#include <z13_launcher/z13_launcher.h>

#include <filesystem>
#include <string>
#include <vector>

#include <boost/dll.hpp>
#include <boost/dll/import.hpp>
#include <boost/dll/shared_library.hpp>

#include <yaml-cpp/yaml.h>

#include <iostream>

#include <lib_core/core.h>
#include <lib_core/log.h>
#include <lib_core/module_factory_base.h>

namespace z13 {

namespace {

const std::filesystem::path kConfigRelativePath =
    std::filesystem::path("config") / "z13_config.yaml";

// Ordered module library paths from the YAML config's `modules:` sequence.
std::vector<std::string> ReadModuleList(const std::filesystem::path& config_path) {
  std::vector<std::string> modules;

  if (!std::filesystem::exists(config_path)) {
    LOG_CRITICAL("Zodiac13Launcher: config file '{}' does not exist!", config_path.string());
    return modules;
  }

  try {
    const auto root = YAML::LoadFile(config_path.string());
    const auto modules_node = root["modules"];
    if (!modules_node || !modules_node.IsSequence()) {
      LOG_CRITICAL("Zodiac13Launcher: '{}' has no 'modules' sequence!", config_path.string());
      return modules;
    }

    for (const auto& entry : modules_node) {
      if (entry.IsScalar()) {
        modules.push_back(entry.as<std::string>());
      } else if (entry.IsMap() && entry["path"]) {
        modules.push_back(entry["path"].as<std::string>());
      } else {
        LOG_WARN("Zodiac13Launcher: skipping malformed module entry in '{}'", config_path.string());
      }
    }
  } catch (const YAML::Exception& ex) {
    LOG_CRITICAL("Zodiac13Launcher: failed to parse '{}': {}", config_path.string(), ex.what());
  }

  return modules;
}

}  // namespace

int Zodiac13Launcher::Run(int argc, char *argv[]) {
  spdlog::flush_on(spdlog::level::debug);
  spdlog::set_level(spdlog::level::debug);

  Core core(argc, argv);

  const std::filesystem::path exe_dir = boost::dll::program_location().parent_path().string();
  const auto config_path = exe_dir / kConfigRelativePath;

  for (const auto& module_path : ReadModuleList(config_path)) {
    core.RegisterModuleFactory(module_path);
  }

  core.CreateWorld();

  return core.Run();
}

}  // namespace z13
