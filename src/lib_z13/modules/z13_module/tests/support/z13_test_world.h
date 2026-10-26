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

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <flecs.h>

#include <lib_core/state/rollback.h>
#include <lib_core/world/core_types.h>
#include <z13/components/action_listener.h>
#include <z13/components/gameplay.h>
#include <z13/components/input_event_emitter.h>
#include <z13_settings/settings_fwd.h>
#include <z13_tests/test_time.h>

namespace z13 {
class Config;
class Core;
}  // namespace z13

namespace z13::net {
class InMemoryNetwork;
}  // namespace z13::net

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

// Headless z13::Core + z13_module world for integration tests: no raylib/SDL,
// no on-disk input-config writes. Built in z13_test_world.cpp, so a test doesn't compile every
// module factory and the settings schemas.
// The schema defaults but with every client holding the whole world: most tests compare peers' worlds, and
// those about interest turn net.interest_events on.
z13::Settings TestSettings();

class Z13TestWorld {
 public:
  // args are the command line (e.g. {kServerArg}, {kConnectArg, "host:1234"}); the default,
  // kSkipMainMenuArg, spawns the scene on the first frame.
  // network is this world's virtual network for --server/--connect: pass the same
  // InMemoryNetwork to several Z13TestWorlds so they can reach each other; left null,
  // each world gets its own, so a solo --server/--connect world never touches a socket.
  explicit Z13TestWorld(std::vector<std::string> args = {std::string(kSkipMainMenuArg)},
                        std::shared_ptr<z13::net::InMemoryNetwork> network = nullptr);
  // With these settings instead of TestSettings().
  Z13TestWorld(std::vector<std::string> args, std::shared_ptr<z13::net::InMemoryNetwork> network,
               const z13::Settings& settings);
  ~Z13TestWorld();

  Z13TestWorld(const Z13TestWorld&) = delete;
  Z13TestWorld& operator=(const Z13TestWorld&) = delete;

  // A per-world temp file, so quick save/load never touches the real game data dir.
  const std::filesystem::path& QuickSavePath() const { return quick_save_path_; }

  flecs::world& World() { return world_.get(); }

  // One frame through TickWorld, the world's only entry point (never progress() directly).
  void Tick(float delta_time = kTestDeltaTime) { z13::flecs_tools::TickWorld(World(), delta_time); }

  const z13::Config& Config() const;

  const z13::Core& Core() const { return *core_; }

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
  std::filesystem::path quick_save_path_;
  std::shared_ptr<z13::net::InMemoryNetwork> network_;  // declared before core_/world_ -- must outlive them
  std::unique_ptr<z13::Core> core_;
  z13::WorldRef world_;  // declared after core_ -- initialization order matters
};

}  // namespace z13::testing
