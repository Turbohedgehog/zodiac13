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

#include <expected>
#include <string>

#include <flecs.h>

#include <lib_core/settings/config.h>
#include <lib_core/settings/core_settings.h>
#include <lib_core/utils/status.h>
#include <settings_generated.h>

#include "building_tuning.h"
#include "net_tuning.h"
#include "physics_tuning.h"

namespace z13 {

// Fields and defaults: schemas/fbs/settings.fbs (the game's settings.json). The nested
// tables are pointers in the object API: build with MakeSettings() so they are always set.
using ConnectTimeoutConfig = fbs::settings::ConnectTimeoutT;
using Settings = fbs::settings::SettingsT;

struct ConnectTimeout : ConnectTimeoutConfig {
  using Singleton = void;

  ConnectTimeout() = default;
  explicit ConnectTimeout(const ConnectTimeoutConfig& values) : ConnectTimeoutConfig(values) {}
};

struct VisualSmoothing : fbs::settings::VisualSmoothingT {
  using Singleton = void;

  VisualSmoothing() = default;
  explicit VisualSmoothing(const fbs::settings::VisualSmoothingT& values) : fbs::settings::VisualSmoothingT(values) {}
};

struct RenderTuning : fbs::settings::RenderTuningT {
  using Singleton = void;

  RenderTuning() = default;
  explicit RenderTuning(const fbs::settings::RenderTuningT& values) : fbs::settings::RenderTuningT(values) {}
};

// The part of Settings a client takes from the server it joins: peers must tick alike.
struct SessionSettings {
  double fps {};
  fbs::net::NetTuningT net;
  fbs::physics::PhysicsTuningT physics;
  fbs::building::BuildingTuningT building;

  bool operator==(const SessionSettings&) const = default;
};

Settings MakeSettings();

void EnsureNestedSettings(Settings& settings);

SessionSettings SessionOf(const Settings& settings);

Settings WithSession(Settings settings, const SessionSettings& session);

// The ranges the schema declares (min/max), plus what a range cannot say: fields that must
// agree with each other (e.g. the retained history must cover the late-command window).
Status ValidateSettings(const Settings& settings);

// Call before the command line is parsed.
Status AddSettingsOptions(Config& config);

std::expected<Settings, std::string> ApplyCliOverrides(const Config& config, const Settings& base);

// Sets the world's NetTuning, PhysicsTuning, BuildingTuning, ConnectTimeout, VisualSmoothing and
// RenderTuning singletons; the core part goes through Config::SetCoreSettings before the world is created.
void InstallSettings(flecs::world world, const Settings& settings);

}  // namespace z13
