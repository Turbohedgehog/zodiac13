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

#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <ostream>
#include <string>
#include <boost/program_options.hpp>

#include "endpoint.h"

namespace z13 {

inline constexpr uint16_t kDefaultServerPort = 26213;

// enet_peer_timeout()'s three parameters (net_module/enet_transport.h); defaults
// bound ENet's own (up to 30s) so a dead/unreachable server disconnects in a few
// seconds instead.
struct ConnectTimeoutConfig {
  uint32_t limit = 32;
  uint32_t min_timeout_ms = 1000;
  uint32_t max_timeout_ms = 3000;
};

class Config {
 public:
  Config();
  void Clear();

  // Returns the rejection reason instead of applying anything; see Clear().
  std::expected<void, std::string> ParseCommandLineArguments(int argc, char *argv[]);
  boost::program_options::options_description& GetOptionsDescription();
  bool NeedShowHelp() const;
  friend std::ostream& operator<<(std::ostream& os, const Config& person);
  double GetFPS() const;
  double GetSnapshotIntervalSeconds() const;
  double GetSnapshotRetentionSeconds() const;
  ConnectTimeoutConfig GetConnectTimeout() const;
  bool SkipMainMenu() const;
  std::optional<std::filesystem::path> GetQuickSavePath() const;

  // --server[=PORT]: a dedicated (or listen-) server; mutually exclusive with
  // --connect. PORT defaults to kDefaultServerPort when omitted.
  bool IsServer() const;
  uint16_t GetPort() const;
  // --connect host[:port]: join that endpoint on startup instead of showing the
  // main menu. std::nullopt when not given.
  std::optional<Endpoint> GetConnectEndpoint() const;

 private:
  std::expected<void, std::string> ValidateAndApplyArguments();

  boost::program_options::options_description options_description_;
  boost::program_options::variables_map variables_map_;
  double fps_ {60.f};
  double snapshot_interval_seconds_ {1.0};
  double snapshot_retention_seconds_ {5.0};
  ConnectTimeoutConfig connect_timeout_;
  bool skip_main_menu_ {false};
  bool server_ {false};
  uint16_t port_ {kDefaultServerPort};
  std::optional<Endpoint> connect_endpoint_;
};

}  // namespace z13
