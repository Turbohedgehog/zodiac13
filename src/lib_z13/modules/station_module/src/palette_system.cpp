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

#include "palette_system.h"

#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <sstream>
#include <string>

#include <boost/dll/runtime_symbol_info.hpp>
#include <flecs.h>

#include <lib_core/state/world_state.h>
#include <lib_core/utils/log.h>
#include <lib_core/world/lifecycle.h>

#include <z13_primitives/palette.h>

namespace z13::station {

namespace {

const std::filesystem::path kPalettePath = std::filesystem::path("assets") / "station" / "palette.json";

// Relative to this plugin (bin/modules/station_module/), which sits two levels below
// assets/ wherever it runs: the game, a dedicated server or the test runner.
std::filesystem::path PaletteFile() {
  const std::filesystem::path plugin_dir = boost::dll::this_line_location().parent_path().string();
  return plugin_dir.parent_path().parent_path() / kPalettePath;
}

std::expected<z13::primitives::Palette, std::string> LoadPalette(const std::filesystem::path& file) {
  std::ifstream stream(file);
  if (!stream.is_open()) {
    return std::unexpected(std::format("cannot open '{}'", file.string()));
  }
  std::ostringstream contents;
  contents << stream.rdbuf();
  return z13::primitives::ParsePalette(contents.str()).transform_error([&file](const std::string& error) {
    return std::format("{} ({})", error, file.string());
  });
}

void RegisterComponents(flecs::world world) {
  z13::flecs_tools::RegisterComponent<z13::primitives::BlockPalette>(world);
}

// In the systems stage, not next to the registration (see PhysicsSystem::RegisterSystems).
void InstallPalette(flecs::world world) {
  auto palette = LoadPalette(PaletteFile());
  if (!palette) {
    log_error("station: no block palette: {}", palette.error());
    return;
  }
  world.set(z13::primitives::BlockPalette {.palette = std::move(*palette)});
}

}  // namespace

void PaletteSystem::Register(flecs::world& world) {
  OnRegisterComponents(world, RegisterComponents);

  OnInitSystems(world, InstallPalette);
}

}  // namespace z13::station
