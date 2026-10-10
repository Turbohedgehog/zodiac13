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
#include <filesystem>
#include <optional>
#include <ostream>
#include <string>
#include <vector>
#include <boost/program_options.hpp>
#include <flatbuffers/reflection.h>

#include <lib_core/settings/core_settings.h>
#include <lib_core/settings/schema_attributes.h>
#include <lib_core/utils/endpoint.h>
#include <lib_core/utils/status.h>

namespace z13 {

inline constexpr uint16_t kDefaultServerPort = 26213;

class Config {
 public:
  Config();
  void Clear();

  // Returns the rejection reason instead of applying anything; see Clear().
  Status ParseCommandLineArguments(int argc, char *argv[]);
  boost::program_options::options_description& GetOptionsDescription();
  bool NeedShowHelp() const;
  friend std::ostream& operator<<(std::ostream& os, const Config& person);
  double GetFPS() const;
  double GetSnapshotIntervalSeconds() const;
  double GetSnapshotRetentionSeconds() const;
  const CoreSettings& GetCoreSettings() const;
  void SetCoreSettings(const CoreSettings& settings);
  // Registers an option per `cli` field of `root` (schema_attributes.h); call before parsing.
  // The schema must outlive this Config.
  Status AddSchemaOptions(const reflection::Schema& schema, const reflection::Object& root);
  Status ApplySchemaOverrides(const reflection::Object& root, flatbuffers::Table& table) const;
  void OverrideFps(std::optional<double> fps);
  bool SkipMainMenu() const;
  std::optional<std::filesystem::path> GetQuickSavePath() const;

  // --server[=PORT]: a dedicated (or listen-) server; mutually exclusive with
  // --connect. PORT defaults to kDefaultServerPort when omitted.
  bool IsServer() const;
  uint16_t GetPort() const;
  // --station: start in station-building mode; mutually exclusive with --connect.
  bool IsStation() const;
  // --station-scene NAME: the blueprint to fill the station with; implies --station.
  // std::nullopt when not given.
  std::optional<std::string> GetStationScene() const;
  // --render-tour PATH: measure the frame time along a camera tour of the station, write it
  // to PATH and quit; needs --station-scene. std::nullopt when not given.
  std::optional<std::filesystem::path> GetRenderTourPath() const;
  // --connect host[:port]: join that endpoint on startup instead of showing the
  // main menu. std::nullopt when not given.
  std::optional<Endpoint> GetConnectEndpoint() const;

 private:
  Status ValidateAndApplyArguments();

  boost::program_options::options_description options_description_;
  boost::program_options::variables_map variables_map_;
  struct SchemaOptions {
    const reflection::Schema* schema {};
    const reflection::Object* root {};
    std::vector<schema::CliOption> options;
  };

  CoreSettings core_settings_;
  std::vector<SchemaOptions> schema_options_;
  std::optional<double> fps_override_;
  // Everything the last parse produced; Clear() resets it as one.
  struct CommandLine {
    bool skip_main_menu {false};
    bool server {false};
    bool station {false};
    std::optional<std::string> station_scene;
    std::optional<std::filesystem::path> render_tour;
    uint16_t port {kDefaultServerPort};
    std::optional<Endpoint> connect_endpoint;
  };
  CommandLine command_line_;
};

}  // namespace z13
