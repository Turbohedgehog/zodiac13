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

#include "z13_test_world.h"

#include <chrono>
#include <cstdint>
#include <format>
#include <system_error>
#include <utility>

#include <gtest/gtest.h>

#include <lib_core/settings/config.h>
#include <lib_core/world/core.h>

#include <building_module/building_module_factory.h>
#include <bullet_module/bullet_module_factory.h>
#include <net_module/in_memory_transport.h>
#include <net_module/net_module_factory.h>
#include <station_module/station_module_factory.h>
#include <conduit_module/conduit_module_factory.h>
#include <conduit_render_module/conduit_render_module_factory.h>
#include <z13_module/z13_module_factory.h>
#include <z13_settings/settings.h>
#include <z13_tests/shipped_station.h>

namespace z13::testing {

namespace {

std::filesystem::path QuickSavePathFor(const void* owner) {
  return std::filesystem::temp_directory_path() /
         std::format("z13_quick_save_{}_{}.json", reinterpret_cast<std::uintptr_t>(owner),
                     std::chrono::steady_clock::now().time_since_epoch().count());
}

// Goes through the real command line, so the module reads these settings from Config.
std::unique_ptr<z13::Core> MakeCore(const std::filesystem::path& quick_save_path, std::vector<std::string> args) {
  std::string program {kTestProgramName};
  std::string quick_save_arg = std::format("{}={}", kQuickSavePathArg, quick_save_path.string());
  std::vector<char*> argv {program.data(), quick_save_arg.data()};
  for (std::string& arg : args) {
    argv.push_back(arg.data());
  }
  return std::make_unique<z13::Core>(static_cast<int>(argv.size()), argv.data());
}

z13::WorldRef CreateWorld(
    z13::Core& core, const std::shared_ptr<z13::net::InMemoryNetwork>& network, const z13::Settings& settings) {
  core.GetConfig().SetCoreSettings(*settings.core);
  core.SetAssetSource(SourceAssets());
  auto factory = std::make_shared<z13::Z13ModuleFactory>();
  factory->SetLoadConfigFromFile(false);
  core.RegisterModuleFactory(factory);
  core.RegisterModuleFactory(std::make_shared<z13::building::BuildingModuleFactory>());
  core.RegisterModuleFactory(std::make_shared<z13::bullet_module::BulletModuleFactory>());
  core.RegisterModuleFactory(std::make_shared<z13::station::StationModuleFactory>());
  core.RegisterModuleFactory(std::make_shared<z13::conduit::ConduitModuleFactory>());
  core.RegisterModuleFactory(std::make_shared<z13::conduit_render::ConduitRenderModuleFactory>());

  auto net_factory = std::make_shared<z13::net::NetModuleFactory>();
  net_factory->SetTransportFactories(
      [network](uint16_t port, z13::SessionTimeoutConfig) {
        return z13::net::CreateInMemoryServerTransport(*network, port);
      },
      [network](const z13::Endpoint& server, z13::ConnectTimeoutConfig, z13::SessionTimeoutConfig) {
        return z13::net::CreateInMemoryClientTransport(*network, server.port);
      });
  core.RegisterModuleFactory(net_factory);

  const auto created = core.CreateWorld();
  if (!created) {
    ADD_FAILURE() << created.error();  // value() below then aborts the fixture
  }
  z13::WorldRef world = created.value();
  z13::InstallSettings(world.get(), settings);
  // Runs the one-shot bootstrap without a frame, so tick-counting tests see tick 0 as before.
  ecs_run(world.get(), world.get().lookup(kInitBootstrapSystemName.data()).id(), 0.f, nullptr);
  return world;
}

}  // namespace

z13::Settings TestSettings() {
  z13::Settings settings = z13::MakeSettings();
  settings.net->interest_events = false;
  return settings;
}

Z13TestWorld::Z13TestWorld(std::vector<std::string> args, std::shared_ptr<z13::net::InMemoryNetwork> network)
    : Z13TestWorld(std::move(args), std::move(network), TestSettings()) {}

Z13TestWorld::Z13TestWorld(std::vector<std::string> args, std::shared_ptr<z13::net::InMemoryNetwork> network,
                           const z13::Settings& settings)
    : quick_save_path_(QuickSavePathFor(this)),
      network_(network ? std::move(network) : std::make_shared<z13::net::InMemoryNetwork>()),
      core_(MakeCore(quick_save_path_, std::move(args))),
      world_(CreateWorld(*core_, network_, settings)) {}

Z13TestWorld::~Z13TestWorld() {
  std::error_code ignored;
  std::filesystem::remove(quick_save_path_, ignored);
}

const z13::Config& Z13TestWorld::Config() const {
  return core_->GetConfig();
}

}  // namespace z13::testing
