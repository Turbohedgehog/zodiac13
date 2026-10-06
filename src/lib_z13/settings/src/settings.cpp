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

#include <z13_settings/settings.h>

#include <format>
#include <memory>
#include <string_view>

#include <flatbuffers/flatbuffers.h>
#include <flatbuffers/reflection.h>

#include <lib_core/settings/schema_attributes.h>
#include <lib_core/state/world_state.h>
#include <lib_core/utils/status.h>
#include <z13/components/station.h>

namespace z13 {

namespace {

std::unexpected<std::string> Invalid(std::string_view field, std::string requirement) {
  return std::unexpected(std::format("settings: {} {}", field, requirement));
}

const reflection::Schema& SettingsSchema() {
  return *reflection::GetSchema(fbs::settings::SettingsBinarySchema::data());
}

// Every scalar stored, so the buffer can be both read for defaults and written to.
flatbuffers::FlatBufferBuilder PackSettings(const Settings& settings) {
  flatbuffers::FlatBufferBuilder builder;
  builder.ForceDefaults(true);
  builder.Finish(fbs::settings::Settings::Pack(builder, &settings));
  return builder;
}

}  // namespace

Settings MakeSettings() {
  Settings settings;
  EnsureNestedSettings(settings);
  return settings;
}

void EnsureNestedSettings(Settings& settings) {
  if (!settings.core) {
    settings.core = std::make_unique<CoreSettings>();
  }
  if (!settings.connect_timeout) {
    settings.connect_timeout = std::make_unique<ConnectTimeoutConfig>();
  }
  if (!settings.net) {
    settings.net = std::make_unique<fbs::net::NetTuningT>();
  }
  if (!settings.physics) {
    settings.physics = std::make_unique<fbs::physics::PhysicsTuningT>();
  }
  if (!settings.building) {
    settings.building = std::make_unique<fbs::building::BuildingTuningT>();
  }
  if (!settings.visual_smoothing) {
    settings.visual_smoothing = std::make_unique<fbs::settings::VisualSmoothingT>();
  }
}

SessionSettings SessionOf(const Settings& settings) {
  return {
      .fps = settings.core ? settings.core->fps : CoreSettings {}.fps,
      .net = settings.net ? *settings.net : fbs::net::NetTuningT {},
      .physics = settings.physics ? *settings.physics : fbs::physics::PhysicsTuningT {},
      .building = settings.building ? *settings.building : fbs::building::BuildingTuningT {},
  };
}

Settings WithSession(Settings settings, const SessionSettings& session) {
  EnsureNestedSettings(settings);
  settings.core->fps = session.fps;
  settings.net = std::make_unique<fbs::net::NetTuningT>(session.net);
  settings.physics = std::make_unique<fbs::physics::PhysicsTuningT>(session.physics);
  settings.building = std::make_unique<fbs::building::BuildingTuningT>(session.building);
  return settings;
}

Status ValidateSettings(const Settings& settings) {
  if (!settings.core || !settings.connect_timeout || !settings.net || !settings.physics || !settings.building ||
      !settings.visual_smoothing) {
    return Invalid("nested tables", "must be set");
  }

  const reflection::Schema& schema = SettingsSchema();
  const flatbuffers::FlatBufferBuilder packed = PackSettings(settings);
  if (const auto in_range = schema::ValidateRanges(
          schema, *schema.root_table(), *flatbuffers::GetRoot<flatbuffers::Table>(packed.GetBufferPointer()));
      !in_range) {
    return std::unexpected(std::format("settings: {}", in_range.error()));
  }

  if (settings.net->clock_jump_threshold_ticks < settings.net->clock_catch_up_threshold_ticks) {
    return Invalid("net.clock_jump_threshold_ticks", "must not be below net.clock_catch_up_threshold_ticks");
  }
  const fbs::building::BuildingTuningT& building = *settings.building;
  if (static_cast<float>(building.spawn_clearance_cells) * station::kCellSize <= building.spawn_height_above_marker) {
    return Invalid("building.spawn_clearance_cells", "must reach above building.spawn_height_above_marker");
  }
  if (settings.connect_timeout->max_timeout_ms < settings.connect_timeout->min_timeout_ms) {
    return Invalid("connect_timeout.max_timeout_ms", "must not be below connect_timeout.min_timeout_ms");
  }

  // A rollback for the oldest accepted command needs a snapshot at least that old.
  const CoreSettings& core = *settings.core;
  const double needed_seconds =
      (static_cast<double>(settings.net->max_late_ticks) / core.fps) + core.snapshot_interval_seconds;
  if (core.snapshot_retention_seconds < needed_seconds) {
    return Invalid(
        "core.snapshot_retention_seconds",
        std::format("must cover net.max_late_ticks plus one snapshot interval ({:.2f}s at {} fps)", needed_seconds,
                    core.fps));
  }
  return {};
}

Status AddSettingsOptions(Config& config) {
  const reflection::Schema& schema = SettingsSchema();
  return config.AddSchemaOptions(schema, *schema.root_table());
}

std::expected<Settings, std::string> ApplyCliOverrides(const Config& config, const Settings& base) {
  flatbuffers::FlatBufferBuilder packed = PackSettings(base);
  const reflection::Schema& schema = SettingsSchema();
  auto* table = flatbuffers::GetMutableRoot<flatbuffers::Table>(packed.GetBufferPointer());
  if (const auto applied = config.ApplySchemaOverrides(*schema.root_table(), *table); !applied) {
    return std::unexpected(applied.error());
  }

  Settings result;
  fbs::settings::GetSettings(packed.GetBufferPointer())->UnPackTo(&result);
  EnsureNestedSettings(result);
  if (const auto valid = ValidateSettings(result); !valid) {
    return std::unexpected(valid.error());
  }
  return result;
}

void InstallSettings(flecs::world world, const Settings& settings) {
  flecs_tools::RegisterComponents<NetTuning, PhysicsTuning, BuildingTuning, ConnectTimeout, VisualSmoothing>(world);
  if (settings.net) {
    world.set(NetTuning(*settings.net));
  }
  if (settings.physics) {
    world.set(PhysicsTuning(*settings.physics));
  }
  if (settings.building) {
    world.set(BuildingTuning(*settings.building));
  }
  if (settings.connect_timeout) {
    world.set(ConnectTimeout(*settings.connect_timeout));
  }
  if (settings.visual_smoothing) {
    world.set(VisualSmoothing(*settings.visual_smoothing));
  }
}

}  // namespace z13
