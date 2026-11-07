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

// Loads the dedicated server's plugins through boost::dll, as the launcher does, and runs
// a few frames: fails where each plugin gets its own copy of flecs (a static flecs build).

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include <boost/dll/runtime_symbol_info.hpp>

#include <lib_core/time/simulation_clock.h>
#include <lib_core/utils/log.h>
#include <lib_core/world/core.h>
#include <z13_launcher/module_list.h>

namespace {

// Relative to bin/tests/, where this runs; config/, assets/ and the module paths in the config
// are relative to bin/.
const std::filesystem::path kBinDir = "..";

constexpr uint64_t kFrames {10};
constexpr float kDeltaTime {1.f / 60.f};

}  // namespace

int main() {
  std::string program {"z13_plugin_smoke"};
  std::string skip_main_menu {"--skip-main-menu"};
  std::vector<char*> argv {program.data(), skip_main_menu.data()};
  z13::Core core(static_cast<int>(argv.size()), argv.data());

  const std::filesystem::path exe_dir = boost::dll::program_location().parent_path().string();
  const z13::AssetSource assets(exe_dir / kBinDir);
  core.SetAssetSource(assets);
  auto modules = z13::ReadModuleList(assets, z13::kServerModuleList);
  if (!modules) {
    z13::log_error("{}", modules.error());
    return EXIT_FAILURE;
  }
  if (std::ranges::any_of(*modules, [&core](const std::string& module_path) {
        const auto registered = core.RegisterModuleFactory(kBinDir / module_path);
        if (!registered) {
          z13::log_error("{}", registered.error());
        }
        return !registered;
      })) {
    return EXIT_FAILURE;
  }

  const auto created = core.CreateWorld();
  if (!created) {
    z13::log_error("{}", created.error());
    return EXIT_FAILURE;
  }
  flecs::world& world = created->get();
  for (uint64_t frame = 0; frame < kFrames; ++frame) {
    core.Update(kDeltaTime);
  }

  const uint64_t tick = world.get<z13::flecs_tools::SimulationClock>().tick;
  if (tick != kFrames) {
    z13::log_error("expected tick {} after {} frames, got {}", kFrames, kFrames, tick);
    return EXIT_FAILURE;
  }
  z13::log_info("{} plugins ran {} frames in one world", modules->size(), kFrames);
  return EXIT_SUCCESS;
}
