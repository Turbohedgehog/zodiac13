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

#include <lib_core/config.h>
#include <lib_core/core_settings.h>
#include <settings_generated.h>

#include "net_tuning.h"

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

// The part of Settings a client takes from the server it joins: peers must tick alike.
struct SessionSettings {
  double fps {};
  fbs::net::NetTuningT net;

  bool operator==(const SessionSettings&) const = default;
};

Settings MakeSettings();

void EnsureNestedSettings(Settings& settings);

SessionSettings SessionOf(const Settings& settings);

Settings WithSession(Settings settings, const SessionSettings& session);

// The ranges the schema declares (min/max), plus what a range cannot say: fields that must
// agree with each other (e.g. the retained history must cover the late-command window).
std::expected<void, std::string> ValidateSettings(const Settings& settings);

// Call before the command line is parsed.
std::expected<void, std::string> AddSettingsOptions(Config& config);

std::expected<Settings, std::string> ApplyCliOverrides(const Config& config, const Settings& base);

// Sets the world's NetTuning and ConnectTimeout singletons; the core part goes through
// Config::SetCoreSettings before the world is created.
void InstallSettings(flecs::world world, const Settings& settings);

}  // namespace z13
