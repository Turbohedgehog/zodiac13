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
#include <string_view>

#include <flecs.h>

#include <lib_core/core.h>

#include <bullet_module/bullet_module_factory.h>
#include <z13/components/input_event_emitter.h>
#include <z13_module/gameplay/gameplay_entities.h>
#include <z13_module/z13_module_factory.h>

namespace z13::testing {

constexpr std::string_view kTestInputSourceName = "Z13TestWorld::InputSource";
constexpr float kTestEpsilon = 1e-3f;

// Headless z13::Core + z13_module world for integration tests: no raylib/SDL,
// no on-disk input-config writes.
class Z13TestWorld {
 public:
  Z13TestWorld() : world_(CreateWorld(core_, quick_save_path_)) {}

  ~Z13TestWorld() {
    std::error_code ignored;
    std::filesystem::remove(quick_save_path_, ignored);
  }

  Z13TestWorld(const Z13TestWorld&) = delete;
  Z13TestWorld& operator=(const Z13TestWorld&) = delete;

  // A per-world temp file, so quick save/load never touches the real game data dir.
  const std::filesystem::path& QuickSavePath() const { return quick_save_path_; }

  flecs::world& World() { return world_.get(); }

  const z13::Config& Config() const { return core_.GetConfig(); }

  flecs::entity Player() {
    return World().lookup(z13::gameplay::kTestPlayerEntityName.data());
  }

  flecs::entity InputSource() {
    return World().entity(kTestInputSourceName.data());
  }

  template <typename EventT>
  void EmitInput(const EventT& event) {
    z13::input::EmitInputEvent(World(), InputSource(), event);
  }

 private:
  static z13::WorldRef CreateWorld(z13::Core& core, const std::filesystem::path& quick_save_path) {
    auto factory = std::make_shared<z13::Z13ModuleFactory>();
    factory->SetLoadConfigFromFile(false);
    factory->SetQuickSavePath(quick_save_path);
    core.RegisterModuleFactory(factory);
    core.RegisterModuleFactory(std::make_shared<z13::bullet_module::BulletModuleFactory>());
    return core.CreateWorld();
  }

  std::filesystem::path quick_save_path_ {std::filesystem::temp_directory_path() /
      std::format("z13_quick_save_{}_{}.json", reinterpret_cast<std::uintptr_t>(this),
                  std::chrono::steady_clock::now().time_since_epoch().count())};
  z13::Core core_ {0, nullptr};
  z13::WorldRef world_;  // declared after core_ -- initialization order matters
};

}  // namespace z13::testing
