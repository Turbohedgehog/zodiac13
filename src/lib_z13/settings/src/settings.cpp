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

#include <cstdint>
#include <format>
#include <limits>
#include <memory>
#include <string_view>

#include <flatbuffers/flatbuffers.h>
#include <flatbuffers/reflection.h>

#include <lib_core/settings/schema_attributes.h>
#include <lib_core/state/world_state.h>
#include <lib_core/utils/status.h>
#include <z13/components/block.h>

namespace z13 {

namespace {

// A client's interest is a cube of (2 * rings + 1)³ sectors: this keeps it from growing into thousands.
constexpr int kMostInterestSectors = 4;

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

template <typename NativeTable>
std::unique_ptr<NativeTable> NewDefaults() {
  return std::make_unique<NativeTable>(SchemaDefaults<NativeTable>());
}

}  // namespace

std::expected<Settings, std::string> PlatformSettings(std::string_view platform) {
  Settings defaults;
  EnsureNestedSettings(defaults);
  flatbuffers::FlatBufferBuilder builder = PackSettings(defaults);
  auto& table = *flatbuffers::GetMutableRoot<flatbuffers::Table>(builder.GetBufferPointer());
  if (const auto applied =
          schema::ApplyPlatformDefaults(SettingsSchema(), *SettingsSchema().root_table(), table, platform);
      !applied) {
    return std::unexpected(applied.error());
  }
  Settings settings;
  fbs::settings::GetSettings(builder.GetBufferPointer())->UnPackTo(&settings);
  EnsureNestedSettings(settings);
  return settings;
}

Settings MakeSettings() {
  // The attributes are the schema's own, and a test applies every platform's.
  if (auto settings = PlatformSettings(kCurrentPlatform)) {
    return std::move(*settings);
  }
  Settings settings;
  EnsureNestedSettings(settings);
  return settings;
}

void EnsureNestedSettings(Settings& settings) {
  if (!settings.core) {
    settings.core = NewDefaults<CoreSettings>();
  }
  if (!settings.connect_timeout) {
    settings.connect_timeout = NewDefaults<ConnectTimeoutConfig>();
  }
  if (!settings.session_timeout) {
    settings.session_timeout = NewDefaults<SessionTimeoutConfig>();
  }
  if (!settings.net) {
    settings.net = NewDefaults<fbs::net::NetTuningT>();
  }
  if (!settings.physics) {
    settings.physics = NewDefaults<fbs::physics::PhysicsTuningT>();
  }
  if (!settings.building) {
    settings.building = NewDefaults<fbs::building::BuildingTuningT>();
  }
  if (!settings.visual_smoothing) {
    settings.visual_smoothing = NewDefaults<fbs::settings::VisualSmoothingT>();
  }
  if (!settings.render) {
    settings.render = NewDefaults<fbs::settings::RenderTuningT>();
  }
  if (!settings.touch) {
    settings.touch = NewDefaults<fbs::settings::TouchLayoutT>();
  }
  if (!settings.lan_discovery) {
    settings.lan_discovery = NewDefaults<fbs::settings::LanDiscoveryT>();
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
  if (!settings.core || !settings.connect_timeout || !settings.session_timeout || !settings.net || !settings.physics || !settings.building ||
      !settings.visual_smoothing || !settings.render || !settings.touch || !settings.lan_discovery) {
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
  const fbs::physics::PhysicsTuningT& physics = *settings.physics;
  if (physics.eye_height <= physics.player_collider_radius) {
    return Invalid("physics.eye_height", "must be above physics.player_collider_radius");
  }
  if (physics.step_height > physics.eye_height - physics.player_collider_radius) {
    return Invalid("physics.step_height", "must not exceed physics.eye_height minus physics.player_collider_radius");
  }
  if (settings.render->light_cluster_far_m <= settings.render->light_cluster_near_m) {
    return Invalid("render.light_cluster_far_m", "must be above render.light_cluster_near_m");
  }
  if (settings.connect_timeout->max_timeout_ms < settings.connect_timeout->min_timeout_ms) {
    return Invalid("connect_timeout.max_timeout_ms", "must not be below connect_timeout.min_timeout_ms");
  }
  if (settings.session_timeout->max_timeout_ms < settings.session_timeout->min_timeout_ms) {
    return Invalid("session_timeout.max_timeout_ms", "must not be below session_timeout.min_timeout_ms");
  }

  // A relayed run of commands spans the whole schedule window in one byte (CommandWire::tick_delta).
  if (settings.net->max_late_ticks + settings.net->max_schedule_ahead_ticks > std::numeric_limits<uint8_t>::max()) {
    return Invalid("net.max_late_ticks", "plus net.max_schedule_ahead_ticks must fit a command's 8-bit tick delta");
  }

  // The server takes commands no further ahead than the schedule window.
  if (settings.net->clock_lead_ticks >= settings.net->max_schedule_ahead_ticks) {
    return Invalid("net.clock_lead_ticks", "must be below net.max_schedule_ahead_ticks");
  }
  if (settings.net->dedicated_clock_lead_ticks >= settings.net->max_schedule_ahead_ticks) {
    return Invalid("net.dedicated_clock_lead_ticks", "must be below net.max_schedule_ahead_ticks");
  }

  if (settings.net->sector_size_cells % settings.building->index_chunk_cells != 0) {
    return Invalid("net.sector_size_cells", "must be a multiple of building.index_chunk_cells");
  }

  if (settings.net->interest_radius_sectors + settings.net->interest_margin_sectors > kMostInterestSectors) {
    return Invalid("net.interest_radius_sectors",
                   std::format("plus net.interest_margin_sectors must not exceed {}", kMostInterestSectors));
  }

  // A player at the edge of its sector must see what its building reaches.
  if (settings.net->interest_radius_sectors * settings.net->sector_size_cells < settings.building->wall_pick_reach_cells) {
    return Invalid("net.interest_radius_sectors", "times net.sector_size_cells must cover building.wall_pick_reach_cells");
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
  flecs_tools::RegisterComponents<NetTuning, PhysicsTuning, BuildingTuning, ConnectTimeout, SessionTimeout, VisualSmoothing,
                                  RenderTuning, TouchLayout, LanDiscoverySettings>(world);
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
    world.set(SessionTimeout(*settings.session_timeout));
  }
  if (settings.visual_smoothing) {
    world.set(VisualSmoothing(*settings.visual_smoothing));
  }
  if (settings.render) {
    world.set(RenderTuning(*settings.render));
  }
  if (settings.touch) {
    world.set(TouchLayout(*settings.touch));
  }
  if (settings.lan_discovery) {
    world.set(LanDiscoverySettings(*settings.lan_discovery));
  }
}

}  // namespace z13
