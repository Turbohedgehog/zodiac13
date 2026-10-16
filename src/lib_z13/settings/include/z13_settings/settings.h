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

#include <flatbuffers/flatbuffers.h>
#include <flecs.h>

#include <lib_core/settings/config.h>
#include <lib_core/settings/core_settings.h>
#include <lib_core/utils/status.h>
#include <settings_generated.h>

#include "building_tuning.h"
#include "net_tuning.h"
#include "physics_tuning.h"
#include "settings_fwd.h"

namespace z13 {

// Fields and defaults: schemas/fbs/settings.fbs (the game's settings.json). The nested
// tables are pointers in the object API: build with MakeSettings() so they are always set.

// A table's object API values with the schema's defaults, strings included: flatc's own
// constructor fills in only the scalars' (a string's default reaches only the table's
// accessor), so they come from an empty table unpacked.
template <typename NativeTable>
NativeTable SchemaDefaults() {
  using Table = typename NativeTable::TableType;
  flatbuffers::FlatBufferBuilder builder;
  builder.Finish(flatbuffers::Offset<Table>(builder.EndTable(builder.StartTable())));
  NativeTable values;
  flatbuffers::GetRoot<Table>(builder.GetBufferPointer())->UnPackTo(&values);
  return values;
}

struct ConnectTimeout : ConnectTimeoutConfig {
  using Singleton = void;

  ConnectTimeout() = default;
  explicit ConnectTimeout(const ConnectTimeoutConfig& values) : ConnectTimeoutConfig(values) {}
};

struct SessionTimeout : SessionTimeoutConfig {
  using Singleton = void;

  SessionTimeout() = default;
  explicit SessionTimeout(const SessionTimeoutConfig& values) : SessionTimeoutConfig(values) {}
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

struct TouchLayout : fbs::settings::TouchLayoutT {
  using Singleton = void;

  TouchLayout() = default;
  explicit TouchLayout(const fbs::settings::TouchLayoutT& values) : fbs::settings::TouchLayoutT(values) {}
};

struct LanDiscoverySettings : fbs::settings::LanDiscoveryT {
  using Singleton = void;

  LanDiscoverySettings() : LanDiscoverySettings(SchemaDefaults<fbs::settings::LanDiscoveryT>()) {}
  explicit LanDiscoverySettings(const fbs::settings::LanDiscoveryT& values) : fbs::settings::LanDiscoveryT(values) {}
};

// The part of Settings a client takes from the server it joins: peers must tick alike.
struct SessionSettings {
  double fps {};
  fbs::net::NetTuningT net;
  fbs::physics::PhysicsTuningT physics;
  fbs::building::BuildingTuningT building;

  bool operator==(const SessionSettings&) const = default;
};

// This build's platform, as the schema's default_<platform> attributes name it.
#if defined(__ANDROID__)
inline constexpr std::string_view kCurrentPlatform = "android";
#elif defined(_WIN32)
inline constexpr std::string_view kCurrentPlatform = "windows";
#elif defined(__APPLE__)
inline constexpr std::string_view kCurrentPlatform = "macos";
#else
inline constexpr std::string_view kCurrentPlatform = "linux";
#endif

// The schema's defaults with `platform`'s own applied (default_<platform> attributes).
std::expected<Settings, std::string> PlatformSettings(std::string_view platform);

// PlatformSettings for this build: what a new settings.json starts from.
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

// Sets the world's NetTuning, PhysicsTuning, BuildingTuning, ConnectTimeout, SessionTimeout, VisualSmoothing,
// RenderTuning, TouchLayout and LanDiscoverySettings singletons; the core part goes through Config::SetCoreSettings before the world is created.
void InstallSettings(flecs::world world, const Settings& settings);

}  // namespace z13
