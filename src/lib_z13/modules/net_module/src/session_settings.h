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
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <flecs.h>

#include <z13/components/utilities.h>
#include <z13_settings/settings.h>

#include <net_module/protocol.h>

namespace z13::net {

// A client's own settings, kept while it runs on its server's.
struct AdoptedSettings {
  using Singleton = void;
  NetTuning own_net;
  z13::PhysicsTuning own_physics;
  z13::BuildingTuning own_building;
};

// Validated with this client's own retention, which the server's windows must fit.
std::expected<z13::SessionSettings, std::string> DecodeSessionSettings(
    flecs::world world, const fbs::net::WelcomeT& welcome);
void AdoptSessionSettings(flecs::world world, const z13::SessionSettings& session);
void RestoreOwnSettings(flecs::world world);

// Peers must place blocks from the same palette; neither having one also agrees.
std::optional<uint64_t> PaletteHash(flecs::world world);

// The utility modules this world runs, by name; none if no module declared any.
std::vector<z13::utility::UtilityModule> UtilityModulesOf(flecs::world world);
std::vector<std::unique_ptr<fbs::net::ModuleDescT>> ToWire(std::span<const z13::utility::UtilityModule> modules);
// Why a client with `client` can't join a server with `server` (both by name); nothing if the sets are the same.
std::optional<std::string> UtilityModulesMismatch(
    std::span<const z13::utility::UtilityModule> server, const std::vector<std::unique_ptr<fbs::net::ModuleDescT>>& client);

}  // namespace z13::net
