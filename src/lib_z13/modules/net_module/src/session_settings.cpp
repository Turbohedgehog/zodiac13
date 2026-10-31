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

#include "session_settings.h"

#include <algorithm>
#include <format>
#include <memory>
#include <iterator>
#include <numeric>
#include <string>
#include <vector>
#include <utility>

#include <lib_core/utils/flecs_utils.h>
#include <primitives/palette.h>

namespace z13::net {

std::expected<z13::SessionSettings, std::string> DecodeSessionSettings(
    flecs::world world, const fbs::net::WelcomeT& welcome) {
  if (!welcome.tuning || !welcome.physics || !welcome.building) {
    return std::unexpected("server sent no settings");
  }
  const z13::SessionSettings session {
      .fps = welcome.fps, .net = *welcome.tuning, .physics = *welcome.physics, .building = *welcome.building};
  z13::Settings own = z13::MakeSettings();
  *own.core = world.get<z13::ActiveCoreSettings>();
  *own.net = world.get<NetTuning>();
  *own.physics = world.get<z13::PhysicsTuning>();
  *own.building = world.get<z13::BuildingTuning>();
  if (const auto valid = z13::ValidateSettings(z13::WithSession(std::move(own), session)); !valid) {
    return std::unexpected(std::format("unusable server settings ({})", valid.error()));
  }
  return session;
}

void AdoptSessionSettings(flecs::world world, const z13::SessionSettings& session) {
  if (!world.has<AdoptedSettings>()) {
    world.set<AdoptedSettings>({
        .own_net = world.get<NetTuning>(),
        .own_physics = world.get<z13::PhysicsTuning>(),
        .own_building = world.get<z13::BuildingTuning>(),
    });
  }
  z13::OverrideCoreFps(world, session.fps);
  world.set(NetTuning(session.net));
  world.set(z13::PhysicsTuning(session.physics));
  world.set(z13::BuildingTuning(session.building));
}

void RestoreOwnSettings(flecs::world world) {
  if (!world.has<AdoptedSettings>()) {
    return;
  }
  const AdoptedSettings own = world.get<AdoptedSettings>();
  world.remove<AdoptedSettings>();
  world.set(own.own_net);
  world.set(own.own_physics);
  world.set(own.own_building);
  z13::OverrideCoreFps(world, std::nullopt);
}

std::optional<uint64_t> PaletteHash(flecs::world world) {
  if (const auto* palette = world.try_get<z13::building::primitives::BlockPalette>()) {
    return palette->palette.hash;
  }
  return std::nullopt;
}

std::vector<z13::utility::UtilityModule> UtilityModulesOf(flecs::world world) {
  const auto* modules = world.try_get<z13::utility::UtilityModules>();
  return modules ? modules->Sorted() : std::vector<z13::utility::UtilityModule> {};
}

std::vector<std::unique_ptr<fbs::net::ModuleDescT>> ToWire(std::span<const z13::utility::UtilityModule> modules) {
  std::vector<std::unique_ptr<fbs::net::ModuleDescT>> wire;
  for (const z13::utility::UtilityModule& module : modules) {
    auto described = std::make_unique<fbs::net::ModuleDescT>();
    described->name = module.name;
    described->version = module.version;
    wire.push_back(std::move(described));
  }
  return wire;
}

std::optional<std::string> UtilityModulesMismatch(
    std::span<const z13::utility::UtilityModule> server,
    const std::vector<std::unique_ptr<fbs::net::ModuleDescT>>& client) {
  std::vector<std::string> problems;
  for (const z13::utility::UtilityModule& module : server) {
    const auto found = std::ranges::find_if(client, [&](const auto& c) { return c && c->name == module.name; });
    if (found == client.end()) {
      problems.push_back(std::format("the client lacks '{}'", module.name));
    } else if ((*found)->version != module.version) {
      problems.push_back(std::format("'{}' is version {} on the client and {} on the server", module.name,
                                     (*found)->version, module.version));
    }
  }
  for (const auto& module : client) {
    if (module && !std::ranges::contains(server, module->name, &z13::utility::UtilityModule::name)) {
      problems.push_back(std::format("the server lacks '{}'", module->name));
    }
  }
  if (problems.empty()) {
    return std::nullopt;
  }
  // Not views::join_with: the Android NDK's libc++ lacks it.
  const std::string joined = std::accumulate(std::next(problems.begin()), problems.end(), problems.front(),
                                             [](const std::string& all, const std::string& problem) {
                                               return std::format("{}; {}", all, problem);
                                             });
  return std::format("utility modules differ: {}", joined);
}

}  // namespace z13::net
