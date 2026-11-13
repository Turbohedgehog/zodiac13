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

#include <lib_core/settings/core_settings.h>
#include <lib_core/settings/schema_attributes.h>
#include <lib_core/state/world_state.h>
#include <lib_core/utils/status.h>
#include <z13/components/station/block.h>

namespace z13 {

namespace {

// A client's interest is a cube of (2 * rings + 1)³ sectors: this keeps it from growing into thousands.
constexpr int kMostInterestSectors = 4;

constexpr std::string_view kCorePathPrefix = "core.";

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
  if (!settings.power) {
    settings.power = NewDefaults<fbs::power::PowerTuningT>();
  }
  if (!settings.door) {
    settings.door = NewDefaults<fbs::door::DoorTuningT>();
  }
  if (!settings.atmos) {
    settings.atmos = NewDefaults<fbs::atmos::AtmosTuningT>();
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
  if (!settings.mcp_server) {
    settings.mcp_server = NewDefaults<fbs::settings::McpServerT>();
  }
}

Settings WithSession(Settings settings, const SessionSettings& session) {
  EnsureNestedSettings(settings);
  settings.core->fps = session.fps;
  settings.net = std::make_unique<fbs::net::NetTuningT>(session.net);
  settings.physics = std::make_unique<fbs::physics::PhysicsTuningT>(session.physics);
  settings.building = std::make_unique<fbs::building::BuildingTuningT>(session.building);
  settings.power = std::make_unique<fbs::power::PowerTuningT>(session.power);
  settings.door = std::make_unique<fbs::door::DoorTuningT>(session.door);
  settings.atmos = std::make_unique<fbs::atmos::AtmosTuningT>(session.atmos);
  return settings;
}

namespace {

bool HasAllNestedTables(const Settings& settings) {
  return settings.core && settings.connect_timeout && settings.session_timeout && settings.net && settings.physics &&
         settings.building && settings.power && settings.door && settings.atmos && settings.visual_smoothing && settings.render &&
         settings.touch && settings.lan_discovery && settings.mcp_server;
}

}  // namespace

Status ValidateSettings(const Settings& settings) {
  if (!HasAllNestedTables(settings)) {
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
  if (settings.atmos->overlay_empty_percent > settings.atmos->overlay_thin_percent) {
    return Invalid("atmos.overlay_empty_percent", "must not exceed atmos.overlay_thin_percent");
  }
  if (settings.render->sun_direction_x == 0.f && settings.render->sun_direction_y == 0.f &&
      settings.render->sun_direction_z == 0.f) {
    return Invalid("render.sun_direction_x", "the sun's direction must not be the zero vector");
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

  if (settings.net->look_hint_near_radius_m > settings.net->look_hint_radius_m) {
    return Invalid("net.look_hint_near_radius_m", "must not exceed net.look_hint_radius_m");
  }

  if (settings.net->ghost_near_radius_m > settings.net->ghost_radius_m) {
    return Invalid("net.ghost_near_radius_m", "must not exceed net.ghost_radius_m");
  }

  if (settings.net->simulated_radius_m > settings.net->ghost_radius_m) {
    return Invalid("net.simulated_radius_m", "must not exceed net.ghost_radius_m");
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
  flecs_tools::RegisterComponents<NetTuning, PhysicsTuning, BuildingTuning, PowerTuning, DoorTuning, AtmosTuning, ConnectTimeout, SessionTimeout, VisualSmoothing,
                                  RenderTuning, TouchLayout, LanDiscoverySettings, McpServerSettings>(world);
  if (settings.net) {
    world.set(NetTuning(*settings.net));
  }
  if (settings.physics) {
    world.set(PhysicsTuning(*settings.physics));
  }
  if (settings.building) {
    world.set(BuildingTuning(*settings.building));
  }
  if (settings.power) {
    world.set(PowerTuning(*settings.power));
  }
  if (settings.door) {
    world.set(DoorTuning(*settings.door));
  }
  if (settings.atmos) {
    world.set(AtmosTuning(*settings.atmos));
  }
  if (settings.connect_timeout) {
    world.set(ConnectTimeout(*settings.connect_timeout));
  }
  if (settings.session_timeout) {
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
  if (settings.mcp_server) {
    world.set(McpServerSettings(*settings.mcp_server));
  }
}

namespace {

template <typename Singleton, typename Table>
void TakeFrom(flecs::world world, std::unique_ptr<Table>& table) {
  if (const auto* value = world.try_get<Singleton>()) {
    table = std::make_unique<Table>(*value);
  }
}

void CollectSettings(const reflection::Schema& schema, const reflection::Object& object,
                     const flatbuffers::Table& table, const std::string& prefix, std::string_view wanted,
                     std::vector<SettingEntry>& out) {
  for (const reflection::Field* field : *object.fields()) {
    const std::string path = prefix + field->name()->str();
    const reflection::BaseType kind = field->type()->base_type();
    if (kind == reflection::Obj) {
      if (const flatbuffers::Table* nested = flatbuffers::GetFieldT(table, *field)) {
        CollectSettings(schema, *schema.objects()->Get(field->type()->index()), *nested, path + ".", wanted, out);
      }
    } else if (kind != reflection::Vector && kind != reflection::Union && path.starts_with(wanted)) {
      out.push_back({.path = path, .value = flatbuffers::GetAnyFieldS(table, *field, &schema)});
    }
  }
}

}  // namespace

Settings SettingsOf(flecs::world world) {
  Settings settings;
  EnsureNestedSettings(settings);
  if (const auto* core = world.try_get<ActiveCoreSettings>()) {
    settings.core = std::make_unique<CoreSettings>(*core);
  }
  TakeFrom<ConnectTimeout>(world, settings.connect_timeout);
  TakeFrom<SessionTimeout>(world, settings.session_timeout);
  TakeFrom<NetTuning>(world, settings.net);
  TakeFrom<PhysicsTuning>(world, settings.physics);
  TakeFrom<BuildingTuning>(world, settings.building);
  TakeFrom<PowerTuning>(world, settings.power);
  TakeFrom<DoorTuning>(world, settings.door);
  TakeFrom<AtmosTuning>(world, settings.atmos);
  TakeFrom<VisualSmoothing>(world, settings.visual_smoothing);
  TakeFrom<RenderTuning>(world, settings.render);
  TakeFrom<TouchLayout>(world, settings.touch);
  TakeFrom<LanDiscoverySettings>(world, settings.lan_discovery);
  TakeFrom<McpServerSettings>(world, settings.mcp_server);
  return settings;
}

std::expected<schema::ScalarRange, std::string> RangeOfSetting(std::string_view path) {
  return schema::RangeOfField(SettingsSchema(), *SettingsSchema().root_table(), path);
}

std::vector<SettingEntry> ListSettings(const Settings& settings, std::string_view prefix) {
  const flatbuffers::FlatBufferBuilder packed = PackSettings(settings);
  const reflection::Schema& schema = SettingsSchema();
  std::vector<SettingEntry> entries;
  CollectSettings(schema, *schema.root_table(), *flatbuffers::GetRoot<flatbuffers::Table>(packed.GetBufferPointer()),
                  {}, prefix, entries);
  return entries;
}

Status ChangeSetting(flecs::world world, std::string_view path, std::string_view text) {
  if (path.starts_with(kCorePathPrefix)) {
    return std::unexpected("core settings are read before the world exists: restart with a new value");
  }
  Settings before = SettingsOf(world);
  flatbuffers::FlatBufferBuilder packed = PackSettings(before);
  auto* table = flatbuffers::GetMutableRoot<flatbuffers::Table>(packed.GetBufferPointer());
  if (const auto set = schema::SetFieldFromText(SettingsSchema(), *SettingsSchema().root_table(), *table, path, text);
      !set) {
    return std::unexpected(set.error());
  }
  Settings after;
  fbs::settings::GetSettings(packed.GetBufferPointer())->UnPackTo(&after);
  EnsureNestedSettings(after);
  if (const auto valid = ValidateSettings(after); !valid) {
    return valid;
  }
  Settings changed;
  const auto keep_changed = [](auto& now, const auto& old, auto& out) {
    if (!(*now == *old)) {
      out = std::move(now);
    }
  };
  keep_changed(after.connect_timeout, before.connect_timeout, changed.connect_timeout);
  keep_changed(after.session_timeout, before.session_timeout, changed.session_timeout);
  keep_changed(after.net, before.net, changed.net);
  keep_changed(after.physics, before.physics, changed.physics);
  keep_changed(after.building, before.building, changed.building);
  keep_changed(after.power, before.power, changed.power);
  keep_changed(after.door, before.door, changed.door);
  keep_changed(after.atmos, before.atmos, changed.atmos);
  keep_changed(after.visual_smoothing, before.visual_smoothing, changed.visual_smoothing);
  keep_changed(after.render, before.render, changed.render);
  keep_changed(after.touch, before.touch, changed.touch);
  keep_changed(after.lan_discovery, before.lan_discovery, changed.lan_discovery);
  keep_changed(after.mcp_server, before.mcp_server, changed.mcp_server);
  InstallSettings(world, changed);
  return {};
}

}  // namespace z13
