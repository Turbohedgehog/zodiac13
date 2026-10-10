/*
 * Copyright 2026 Ivan Kulenko / Zodiac13
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://apache.org
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <lib_core/settings/config.h>
#include <lib_core/utils/status.h>

#include <format>
#include <string>
#include <string_view>

namespace z13 {

namespace po = boost::program_options;

namespace {

constexpr std::string_view kQuickSavePathOption = "quick-save-path";
constexpr std::string_view kServerOption = "server";
constexpr std::string_view kConnectOption = "connect";
constexpr std::string_view kStationOption = "station";
constexpr std::string_view kStationSceneOption = "station-scene";
constexpr std::string_view kRenderTourOption = "render-tour";

}  // namespace

Config::Config() {
  options_description_.add_options()
      ("help,h", "Show help message")
      ("skip-main-menu", po::bool_switch(&command_line_.skip_main_menu),
       "Start gameplay immediately, skipping the main menu")
      (kQuickSavePathOption.data(), po::value<std::string>(),
       "Quick save file (default: the game data directory)")
      (kServerOption.data(), po::value<int>()->implicit_value(kDefaultServerPort),
       std::format("Run as a server (no rendering) on an optional port (default: {}); "
                    "mutually exclusive with --connect", kDefaultServerPort).c_str())
      (kConnectOption.data(), po::value<std::string>(),
       "Join host[:port] on startup instead of showing the main menu")
      (kStationOption.data(), po::bool_switch(&command_line_.station),
       "Start in station-building mode (with --server, host it); a client gets the mode from the server")
      (kStationSceneOption.data(), po::value<std::string>(),
       "Blueprint to fill the station with (a file name under assets/station/blueprints/, without .json; "
       "without it the station starts empty); implies --station")
      (kRenderTourOption.data(), po::value<std::string>(),
       "Fly the camera through measuring points of the --station-scene station, write the frame times "
       "to this CSV file and quit");
}

void Config::Clear() {
  variables_map_ = boost::program_options::variables_map();
  command_line_ = {};
}

boost::program_options::options_description& Config::GetOptionsDescription() {
  return options_description_;
}

Status Config::ParseCommandLineArguments(int argc, char *argv[]) {
  Clear();
  try {
    po::store(po::parse_command_line(argc, argv, options_description_), variables_map_);
    po::notify(variables_map_);
  } catch (const po::error& ex) {
    return std::unexpected(ex.what());
  }
  return ValidateAndApplyArguments();
}

Status Config::ValidateAndApplyArguments() {
  if (variables_map_.count(kServerOption.data()) > 0) {
    command_line_.server = true;
    const int raw_port = variables_map_[kServerOption.data()].as<int>();
    if (raw_port < kMinPort || raw_port > kMaxPort) {
      return std::unexpected(std::format("--{} port must be between {} and {}, got {}",
                                          kServerOption, kMinPort, kMaxPort, raw_port));
    }
    command_line_.port = static_cast<uint16_t>(raw_port);
  }

  if (command_line_.server && variables_map_.count(kConnectOption.data()) > 0) {
    return std::unexpected(std::format("--{} and --{} cannot be used together", kServerOption, kConnectOption));
  }

  if (variables_map_.count(kStationSceneOption.data()) > 0) {
    command_line_.station = true;
    command_line_.station_scene = variables_map_[kStationSceneOption.data()].as<std::string>();
  }

  if (variables_map_.count(kRenderTourOption.data()) > 0) {
    if (!command_line_.station_scene) {
      return std::unexpected(std::format("--{} needs --{}", kRenderTourOption, kStationSceneOption));
    }
    command_line_.render_tour = std::filesystem::path(variables_map_[kRenderTourOption.data()].as<std::string>());
  }

  if (command_line_.station && variables_map_.count(kConnectOption.data()) > 0) {
    const std::string_view station_option = command_line_.station_scene ? kStationSceneOption : kStationOption;
    return std::unexpected(std::format("--{} and --{} cannot be used together", station_option, kConnectOption));
  }

  if (variables_map_.count(kConnectOption.data()) > 0) {
    const auto endpoint = ParseEndpoint(variables_map_[kConnectOption.data()].as<std::string>(), kDefaultServerPort);
    if (!endpoint) {
      return std::unexpected(std::format("--{}: {}", kConnectOption, endpoint.error()));
    }
    command_line_.connect_endpoint = *endpoint;
  }

  return {};
}

bool Config::NeedShowHelp() const {
  return variables_map_.count("help") > 0;
}

double Config::GetFPS() const {
  return fps_override_.value_or(core_settings_.fps);
}

double Config::GetSnapshotIntervalSeconds() const {
  return core_settings_.snapshot_interval_seconds;
}

double Config::GetSnapshotRetentionSeconds() const {
  return core_settings_.snapshot_retention_seconds;
}

const CoreSettings& Config::GetCoreSettings() const {
  return core_settings_;
}

void Config::SetCoreSettings(const CoreSettings& settings) {
  core_settings_ = settings;
}

Status Config::AddSchemaOptions(
    const reflection::Schema& schema, const reflection::Object& root) {
  auto collected = schema::CollectCliOptions(schema, root);
  if (!collected) {
    return std::unexpected(collected.error());
  }

  for (const schema::CliOption& option : *collected) {
    if (options_description_.find_nothrow(option.long_name, false) != nullptr) {
      return std::unexpected(std::format("--{} ({}) is already an option", option.long_name, option.path));
    }
    if (option.short_name && options_description_.find_nothrow(std::format("-{}", *option.short_name), false) != nullptr) {
      return std::unexpected(std::format("-{} ({}) is already an option", *option.short_name, option.path));
    }
    const std::string names =
        option.short_name ? std::format("{},{}", option.long_name, *option.short_name) : option.long_name;
    options_description_.add_options()(names.c_str(), po::value<std::string>(), option.help.c_str());
  }
  schema_options_.push_back({.schema = &schema, .root = &root, .options = std::move(*collected)});
  return {};
}

Status Config::ApplySchemaOverrides(
    const reflection::Object& root, flatbuffers::Table& table) const {
  for (const SchemaOptions& registered : schema_options_) {
    if (registered.root != &root) {
      continue;
    }
    for (const schema::CliOption& option : registered.options) {
      const auto given = variables_map_.find(option.long_name);
      if (given == variables_map_.end()) {
        continue;
      }
      if (const auto set = schema::SetFieldFromText(
              *registered.schema, root, table, option.path, given->second.as<std::string>());
          !set) {
        return std::unexpected(std::format("--{}: {}", option.long_name, set.error()));
      }
    }
  }
  return {};
}

void Config::OverrideFps(std::optional<double> fps) {
  fps_override_ = fps;
}

bool Config::SkipMainMenu() const {
  return command_line_.skip_main_menu;
}

std::optional<std::filesystem::path> Config::GetQuickSavePath() const {
  if (const auto it = variables_map_.find(std::string(kQuickSavePathOption));
      it != variables_map_.end()) {
    return std::filesystem::path(it->second.as<std::string>());
  }
  return std::nullopt;
}

bool Config::IsServer() const {
  return command_line_.server;
}

bool Config::IsStation() const {
  return command_line_.station;
}

std::optional<std::string> Config::GetStationScene() const {
  return command_line_.station_scene;
}

std::optional<std::filesystem::path> Config::GetRenderTourPath() const {
  return command_line_.render_tour;
}

uint16_t Config::GetPort() const {
  return command_line_.port;
}

std::optional<Endpoint> Config::GetConnectEndpoint() const {
  return command_line_.connect_endpoint;
}

std::ostream& operator<<(std::ostream& os, const Config& person) {
  return os << person.options_description_;
}

}  // namespace z13
