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

#include <lib_core/config.h>

#include <format>
#include <string>
#include <string_view>

namespace z13 {

namespace po = boost::program_options;

namespace {

constexpr std::string_view kQuickSavePathOption = "quick-save-path";
constexpr std::string_view kServerOption = "server";
constexpr std::string_view kConnectOption = "connect";

}  // namespace

Config::Config() {
  options_description_.add_options()
      ("help,h", "Show help message")
      ("skip-main-menu", po::bool_switch(&skip_main_menu_),
       "Start gameplay immediately, skipping the main menu")
      (kQuickSavePathOption.data(), po::value<std::string>(),
       "Quick save file (default: the game data directory)")
      (kServerOption.data(), po::value<int>()->implicit_value(kDefaultServerPort),
       std::format("Run as a server (no rendering) on an optional port (default: {}); "
                    "mutually exclusive with --connect", kDefaultServerPort).c_str())
      (kConnectOption.data(), po::value<std::string>(),
       "Join host[:port] on startup instead of showing the main menu");
}

void Config::Clear() {
  variables_map_ = boost::program_options::variables_map();
  server_ = false;
  port_ = kDefaultServerPort;
  connect_endpoint_.reset();
}

boost::program_options::options_description& Config::GetOptionsDescription() {
  return options_description_;
}

std::expected<void, std::string> Config::ParseCommandLineArguments(int argc, char *argv[]) {
  Clear();
  try {
    po::store(po::parse_command_line(argc, argv, options_description_), variables_map_);
    po::notify(variables_map_);
  } catch (const po::error& ex) {
    return std::unexpected(ex.what());
  }
  return ValidateAndApplyArguments();
}

std::expected<void, std::string> Config::ValidateAndApplyArguments() {
  if (variables_map_.count(kServerOption.data()) > 0) {
    server_ = true;
    const int raw_port = variables_map_[kServerOption.data()].as<int>();
    if (raw_port < kMinPort || raw_port > kMaxPort) {
      return std::unexpected(std::format("--{} port must be between {} and {}, got {}",
                                          kServerOption, kMinPort, kMaxPort, raw_port));
    }
    port_ = static_cast<uint16_t>(raw_port);
  }

  if (server_ && variables_map_.count(kConnectOption.data()) > 0) {
    return std::unexpected(std::format("--{} and --{} cannot be used together", kServerOption, kConnectOption));
  }

  if (variables_map_.count(kConnectOption.data()) > 0) {
    const auto endpoint = ParseEndpoint(variables_map_[kConnectOption.data()].as<std::string>(), kDefaultServerPort);
    if (!endpoint) {
      return std::unexpected(std::format("--{}: {}", kConnectOption, endpoint.error()));
    }
    connect_endpoint_ = *endpoint;
  }

  return {};
}

bool Config::NeedShowHelp() const {
  return variables_map_.count("help") > 0;
}

double Config::GetFPS() const {
  return fps_;
}

double Config::GetSnapshotIntervalSeconds() const {
  return snapshot_interval_seconds_;
}

double Config::GetSnapshotRetentionSeconds() const {
  return snapshot_retention_seconds_;
}

ConnectTimeoutConfig Config::GetConnectTimeout() const {
  return connect_timeout_;
}

bool Config::SkipMainMenu() const {
  return skip_main_menu_;
}

std::optional<std::filesystem::path> Config::GetQuickSavePath() const {
  if (const auto it = variables_map_.find(std::string(kQuickSavePathOption));
      it != variables_map_.end()) {
    return std::filesystem::path(it->second.as<std::string>());
  }
  return std::nullopt;
}

bool Config::IsServer() const {
  return server_;
}

uint16_t Config::GetPort() const {
  return port_;
}

std::optional<Endpoint> Config::GetConnectEndpoint() const {
  return connect_endpoint_;
}

std::ostream& operator<<(std::ostream& os, const Config& person) {
  return os << person.options_description_;
}

}  // namespace z13
