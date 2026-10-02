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
// a few frames: fails where each plugin gets its own copy of flecs (a static flecs build),
// or keeps its own spdlog logger registry instead of the process's one.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <boost/dll/runtime_symbol_info.hpp>
#include <spdlog/sinks/ringbuffer_sink.h>
#include <spdlog/spdlog.h>

#include <lib_core/core.h>
#include <lib_core/log.h>
#include <lib_core/simulation_clock.h>
#include <lib_test_dll/test_dll_messages.h>
#include <z13_launcher/module_list.h>

namespace {

// Relative to bin/tests/, where this runs; module paths in the config are relative to bin/.
const std::filesystem::path kBinDir = "..";
const std::filesystem::path kServerConfigPath = kBinDir / "config" / "z13_config_server.yaml";

constexpr uint64_t kFrames {10};
constexpr float kDeltaTime {1.f / 60.f};
constexpr size_t kCapturedMessages {256};

}  // namespace

int main() {
  // Debug here; a plugin with its own spdlog copy would still be at the default info level.
  auto captured = std::make_shared<spdlog::sinks::ringbuffer_sink_mt>(kCapturedMessages);
  spdlog::default_logger()->sinks().push_back(captured);
  spdlog::set_level(spdlog::level::debug);

  std::string program {"z13_plugin_smoke"};
  std::string skip_main_menu {"--skip-main-menu"};
  std::vector<char*> argv {program.data(), skip_main_menu.data()};
  z13::Core core(static_cast<int>(argv.size()), argv.data());

  const std::filesystem::path exe_dir = boost::dll::program_location().parent_path().string();
  const auto modules = z13::ReadModuleList(exe_dir / kServerConfigPath);
  if (!modules) {
    z13::log_error("{}", modules.error());
    return EXIT_FAILURE;
  }
  if (std::ranges::any_of(*modules, [&core](const std::string& module_path) {
        return !core.RegisterModuleFactory(kBinDir / module_path);
      })) {
    return EXIT_FAILURE;
  }

  flecs::world& world = core.CreateWorld().get();
  for (uint64_t frame = 0; frame < kFrames; ++frame) {
    core.Update(kDeltaTime);
  }

  if (std::ranges::none_of(captured->last_formatted(), [](const std::string& line) {
        return line.find(z13::dll::kRegisterModulesMessage) != std::string::npos;
      })) {
    z13::log_error("test_dll's debug message didn't reach the core's logger");
    return EXIT_FAILURE;
  }

  const uint64_t tick = world.get<z13::flecs_tools::SimulationClock>().tick;
  if (tick != kFrames) {
    z13::log_error("expected tick {} after {} frames, got {}", kFrames, kFrames, tick);
    return EXIT_FAILURE;
  }
  z13::log_info("{} plugins ran {} frames in one world", modules->size(), kFrames);
  return EXIT_SUCCESS;
}
