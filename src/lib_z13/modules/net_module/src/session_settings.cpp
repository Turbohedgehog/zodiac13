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

#include <format>
#include <utility>

#include <lib_core/utils/flecs_utils.h>
#include <z13_primitives/palette.h>

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

}  // namespace z13::net
