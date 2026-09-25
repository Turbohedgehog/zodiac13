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

#include "quick_save_input_system.h"

#include <expected>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>

#include <flecs.h>

#include <actions_generated.h>

#include <lib_core/components.h>
#include <lib_core/log.h>
#include <lib_core/world_state.h>
#include <lib_core/world_state_requests.h>

#include <z13/components/gameplay.h>
#include <z13/components/input.h>
#include <z13_module/input/input_config_loader.h>

namespace z13::state {

namespace {

constexpr std::string_view kActionsEnumName = "z13.fbs.actions.Action";

struct QuickSaveActionIds {
  using Singleton = void;
  using IdType = z13::input::ActionInfo::IdType;
  std::optional<IdType> save;
  std::optional<IdType> load;
};

std::expected<void, std::string> WriteFile(const std::filesystem::path& path, std::string_view text) {
  std::error_code error;
  std::filesystem::create_directories(path.parent_path(), error);
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  if (!file.is_open() || !file.write(text.data(), static_cast<std::streamsize>(text.size()))) {
    return std::unexpected("cannot write " + path.string());
  }
  return {};
}

std::expected<std::string, std::string> ReadFile(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file.is_open()) {
    return std::unexpected("cannot open " + path.string());
  }
  return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

void QuickSave(flecs::world world, const std::filesystem::path& path) {
  z13::flecs_tools::RequestSaveWorldState(
      world, [path](std::expected<std::string, std::string> json) {
        if (!json) {
          log_error("Quick save failed: {}", json.error());
          return;
        }
        if (const auto written = WriteFile(path, *json); !written) {
          log_error("Quick save failed: {}", written.error());
          return;
        }
        log_info("Quick save written to {}", path.string());
      });
}

void QuickLoad(flecs::world world, const std::filesystem::path& path) {
  auto json = ReadFile(path);
  if (!json) {
    log_warn("Quick load skipped: {}", json.error());
    return;
  }

  z13::flecs_tools::RequestLoadWorldState(
      world, std::move(*json), [path](const std::expected<void, std::string>& result) {
        if (result) {
          log_info("Quick load restored from {}", path.string());
        } else {
          log_error("Quick load failed: {}", result.error());
        }
      });
}

void OnConfigUpdated(
    QuickSaveActionIds& ids, z13::input::OnConfigUpdatedEvent, const z13::input::ActionMap& action_map) {
  const auto find_action_id = [&action_map](z13::fbs::actions::Action action) {
    auto id = z13::gameplay::input::InputConfigLoader::FindActionId(
        action_map.action_map, kActionsEnumName, action);
    if (!id) {
      log_error("QuickSave: cannot find action id {} in '{}'", static_cast<int>(action), kActionsEnumName);
    }
    return id;
  };

  ids.save = find_action_id(z13::fbs::actions::Action::SAVE_SCENE);
  ids.load = find_action_id(z13::fbs::actions::Action::LOAD_SCENE);
}

void ProcessActions(
    flecs::entity player,
    const z13::input::ActionListener& action_listener,
    const QuickSaveActionIds& ids,
    const z13::gameplay::QuickSaveSettings& settings) {
  const auto switched_on = [&action_listener](const std::optional<QuickSaveActionIds::IdType>& id) {
    const auto value = id ? action_listener.Value(*id) : std::nullopt;
    return value && value->IsSwitchedOn();
  };

  if (switched_on(ids.save)) {
    QuickSave(player.world(), settings.path);
  }
  if (switched_on(ids.load)) {
    QuickLoad(player.world(), settings.path);
  }
}

void RegisterComponents(flecs::world world) {
  z13::flecs_tools::RegisterComponent<QuickSaveActionIds>(world);
}

void RegisterSystems(flecs::world world) {
  // Set here, not next to `.add(flecs::Singleton)` (see PhysicsSystem::RegisterSystems).
  world.set<QuickSaveActionIds>({});

  world.observer<QuickSaveActionIds, z13::input::OnConfigUpdatedEvent, z13::input::ActionMap>(
           "QuickSave::OnConfigUpdated")
      .event<z13::input::SystemInputEventType>()
      .each(OnConfigUpdated);

  world.system<const z13::input::ActionListener, const QuickSaveActionIds, const z13::gameplay::QuickSaveSettings>(
           "QuickSave::ProcessActions")
      .kind<z13::input::ApplyActionFramePhase>()
      .without<z13::gameplay::Pause>()
      .each(ProcessActions);
}

}  // namespace

void QuickSaveInputSystem::Register(flecs::world& world) {
  world.observer<RegisterComponentsEvent>("QuickSave::RegisterComponents")
      .event(flecs::OnAdd)
      .yield_existing()
      .each([world = world](const auto&) { RegisterComponents(world); });

  world.observer<InitSystemsEvent>("QuickSave::RegisterSystems")
      .event(flecs::OnAdd)
      .yield_existing()
      .each([world = world](const auto&) { RegisterSystems(world); });
}

}  // namespace z13::state
