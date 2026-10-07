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

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <format>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <flecs.h>
#include <gtest/gtest.h>

#include <lib_core/state/rollback.h>
#include <lib_core/world/core.h>

#include <building_module/building_module_factory.h>
#include <bullet_module/bullet_module_factory.h>
#include <net_module/in_memory_transport.h>
#include <net_module/net_module_factory.h>
#include <station_module/station_module_factory.h>
#include <z13/components/gameplay.h>
#include <z13/components/input.h>
#include <z13/components/input_event_emitter.h>
#include <z13_module/z13_module_factory.h>
#include <z13_settings/settings.h>
#include <z13_tests/test_time.h>

namespace z13::testing {

constexpr std::string_view kTestInputSourceName = "Z13TestWorld::InputSource";
constexpr float kTestEpsilon = 1e-3f;
constexpr std::string_view kInitBootstrapSystemName = "InitBootstrap";
constexpr std::string_view kTestProgramName = "z13_test_runner";
constexpr std::string_view kSkipMainMenuArg = "--skip-main-menu";
constexpr std::string_view kQuickSavePathArg = "--quick-save-path";
constexpr std::string_view kServerArg = "--server";
constexpr std::string_view kConnectArg = "--connect";
constexpr std::string_view kStationArg = "--station";
constexpr std::string_view kStationSceneArg = "--station-scene";
// assets/station/blueprints/site.json: a floor with one spawn point.
constexpr std::string_view kSiteScene = "site";
// assets/station/blueprints/test.json, from docs/station-layout/generate_layout.py.
constexpr std::string_view kTestScene = "test";

// Headless z13::Core + z13_module world for integration tests: no raylib/SDL,
// no on-disk input-config writes.
class Z13TestWorld {public:
  // args are the command line (e.g. {kServerArg}, {kConnectArg, "host:1234"}); the default,
  // kSkipMainMenuArg, spawns the scene on the first frame.
  // network is this world's virtual network for --server/--connect: pass the same
  // InMemoryNetwork to several Z13TestWorlds so they can reach each other; left null,
  // each world gets its own, so a solo --server/--connect world never touches a socket.
  explicit Z13TestWorld(std::vector<std::string> args = {std::string(kSkipMainMenuArg)},
      std::shared_ptr<z13::net::InMemoryNetwork> network = nullptr,
      const z13::Settings& settings = z13::MakeSettings())
      : network_(network ? std::move(network) : std::make_shared<z13::net::InMemoryNetwork>()),
        core_(MakeCore(quick_save_path_, std::move(args))),
        world_(CreateWorld(core_, network_, settings)) {}

  ~Z13TestWorld() {std::error_code ignored;
    std::filesystem::remove(quick_save_path_, ignored);
  }

  Z13TestWorld(const Z13TestWorld&) = delete;
  Z13TestWorld& operator=(const Z13TestWorld&) = delete;

  // A per-world temp file, so quick save/load never touches the real game data dir.
  const std::filesystem::path& QuickSavePath() const { return quick_save_path_; }

  flecs::world& World() { return world_.get(); }

  // One frame through TickWorld, the world's only entry point (never progress() directly).
  void Tick(float delta_time = kTestDeltaTime) { z13::flecs_tools::TickWorld(World(), delta_time); }

  const z13::Config& Config() const { return core_.GetConfig(); }

  const z13::Core& Core() const { return core_; }

  z13::net::InMemoryNetwork& Network() { return *network_; }

  // This participant's own player: at most one, found by tag, not a fixed name.
  flecs::entity Player() {
    flecs::entity found;
    World().query_builder().with<z13::input::CurrentActionListenerTag>().build().each(
        [&](flecs::entity e) { found = e; });
    return found;
  }

  // Like the main menu's Start Game, which also closes the menu (unpauses).
  void StartGame() {
    World().add<z13::gameplay::Gameplay>();
    World().remove<z13::gameplay::Pause>();
  }
  void ExitToMainMenu() { World().remove<z13::gameplay::Gameplay>(); }

  flecs::entity InputSource() {
    return World().entity(kTestInputSourceName.data());
  }

  template <typename EventT>
  void EmitInput(const EventT& event) {
    z13::input::EmitInputEvent(World(), InputSource(), event);
  }

 private:
  // Goes through the real command line, so the module reads these settings from Config.
  static z13::Core MakeCore(const std::filesystem::path& quick_save_path, std::vector<std::string> args) {
    std::string program {kTestProgramName};
    std::string quick_save_arg = std::format("{}={}", kQuickSavePathArg, quick_save_path.string());
    std::vector<char*> argv {program.data(), quick_save_arg.data()};
    for (std::string& arg : args) {
      argv.push_back(arg.data());
    }
    return z13::Core(static_cast<int>(argv.size()), argv.data());
  }

  static z13::WorldRef CreateWorld(
      z13::Core& core, const std::shared_ptr<z13::net::InMemoryNetwork>& network, const z13::Settings& settings) {
    core.GetConfig().SetCoreSettings(*settings.core);
    auto factory = std::make_shared<z13::Z13ModuleFactory>();
    factory->SetLoadConfigFromFile(false);
    core.RegisterModuleFactory(factory);
    core.RegisterModuleFactory(std::make_shared<z13::building::BuildingModuleFactory>());
    core.RegisterModuleFactory(std::make_shared<z13::bullet_module::BulletModuleFactory>());
    core.RegisterModuleFactory(std::make_shared<z13::station::StationModuleFactory>());

    auto net_factory = std::make_shared<z13::net::NetModuleFactory>();
    net_factory->SetTransportFactories(
        [network](uint16_t port) { return z13::net::CreateInMemoryServerTransport(*network, port); },
        [network](const z13::Endpoint& server, z13::ConnectTimeoutConfig) {
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

  std::filesystem::path quick_save_path_ {std::filesystem::temp_directory_path() /
      std::format("z13_quick_save_{}_{}.json", reinterpret_cast<std::uintptr_t>(this),
                  std::chrono::steady_clock::now().time_since_epoch().count())};
  std::shared_ptr<z13::net::InMemoryNetwork> network_;  // declared before core_/world_ -- must outlive them
  z13::Core core_;
  z13::WorldRef world_;  // declared after core_ -- initialization order matters
};

}  // namespace z13::testing
