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
#include <string>
#include <vector>

#include <flecs.h>

#include <lib_core/state/world_state.h>
#include <lib_core/utils/asset_source.h>
#include <lib_core/utils/log.h>
#include <lib_core/world/lifecycle.h>

#include <z13/components/utilities.h>

#include <primitives/palette.h>
#include <primitives/station_assets.h>


namespace z13::building {

namespace {

std::expected<std::vector<z13::building::primitives::PaletteFragment>, std::string> LoadFragments(
    const z13::AssetSource& assets, const z13::utility::UtilityModules& utilities) {
  std::vector<z13::building::primitives::PaletteFragment> fragments;
  for (const z13::utility::UtilityModule& module : utilities.Sorted()) {
    if (!module.palette_fragment) {
      continue;
    }
    const std::filesystem::path file = z13::building::primitives::PaletteFragmentFile(module.name);
    auto json = assets.ReadAsset(file);
    if (!json) {
      return std::unexpected(std::format("{} ({})", json.error(), file.string()));
    }
    fragments.push_back({.file = file.string(), .json = std::move(*json)});
  }
  return fragments;
}

std::expected<z13::building::primitives::Palette, std::string> LoadPalette(
    const z13::AssetSource& assets, const z13::utility::UtilityModules& utilities) {
  const std::filesystem::path file = z13::building::primitives::PaletteFile();
  return assets.ReadAsset(file)
      .and_then([&](const std::string& contents) {
        return LoadFragments(assets, utilities).and_then([&](const auto& fragments) {
          return z13::building::primitives::ParsePalette(contents, fragments);
        });
      })
      .transform_error([&file](const std::string& error) { return std::format("{} ({})", error, file.string()); });
}

void RegisterComponents(flecs::world world) {
  z13::flecs_tools::RegisterComponents<z13::building::primitives::BlockPalette, z13::utility::UtilityModules>(world);
}

// In the phases stage, after the components are registered, so the network modules can add themselves.
void InstallUtilityModules(flecs::world world) {
  world.set(z13::utility::UtilityModules {});
}

// In the systems stage, not next to the registration (see PhysicsSystem::RegisterSystems).
void InstallPalette(flecs::world world) {
  // Read once, outside any system.
  auto palette = LoadPalette(world.get<z13::Assets>().source, world.get<z13::utility::UtilityModules>());
  if (!palette) {
    log_error("station: no block palette: {}", palette.error());
    return;
  }
  world.set(z13::building::primitives::BlockPalette {.palette = std::move(*palette)});
}

}  // namespace

void PaletteSystem::Register(flecs::world& world) {
  OnRegisterComponents(world, RegisterComponents);

  OnInitPhases(world, InstallUtilityModules);

  OnInitSystems(world, InstallPalette);
}

}  // namespace z13::building
