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

module;

#include <flecs.h>

#include <building_generated.h>

#include <lib_core/log.h>

module z13.module.building_input;

import <algorithm>;
import <optional>;

import z13.input;
import z13.gameplay.input;

import z13.core;
import z13.building;
import z13.gameplay;

namespace z13::building {

namespace {

// todo: убрать константу и брать из z13.fbs.building.Action.action_group
static const std::string kBuildingActionGroup = "Building";

struct BuildActionIds {
  using IdType = z13::input::ActionInfo::IdType;
  std::optional<IdType> toggle_building_mode;
  std::optional<IdType> build_block;
  std::optional<IdType> destroy_block;
};

void OnAppendInputSchema(
    flecs::iter it,
    size_t,
    const z13::input::AppendInputSchema,
    z13::input::ActionMap& action_map) {
  z13::input::FlatbufferBinarySchema ev {
      .binary_schema = std::span {
        z13::fbs::building::BlockBinarySchema::data(),
        z13::fbs::building::BlockBinarySchema::size()
      },
  };
  z13::gameplay::input::InputConfigLoader::AppendFlatbufActionsFromBinarySchema(ev, action_map);
}

void OnConfigUpdated(flecs::entity e, z13::input::OnConfigUpdatedEvent, const z13::input::ActionMap& action_map) {
  using EnumValueType = z13::input::ActionInfo::EnumValueType;
  auto& build_action_ids = e.world().ensure<BuildActionIds>();
  build_action_ids = BuildActionIds();
  const auto& enum_value = action_map.action_map.get<z13::input::ActionMap::EnumNameEnumValueTag>();

  auto find_action_id = [&](const auto action_value) {
    auto action_id_holder = z13::gameplay::input::InputConfigLoader::FindActionId(
        action_map.action_map,
        "z13.fbs.building.Action",
        action_value);
    if (!action_id_holder) {
      LOG_ERROR(
        "OnConfigUpdated: Cannot find action id '{}' for enum 'z13.fbs.building'",
        static_cast<EnumValueType>(action_value)
      );
    }

    return action_id_holder;
  };

  build_action_ids.toggle_building_mode = find_action_id(z13::fbs::building::Action::TOGGLE_BUILDING_MODE);
  build_action_ids.build_block = find_action_id(z13::fbs::building::Action::BUILD_BLOCK);
  build_action_ids.destroy_block = find_action_id(z13::fbs::building::Action::DESTROY_BLOK);
}

void ToggleBuildingMode(
    flecs::entity e,
    z13::input::ActionListener& action_listener,
    const z13::input::ActionValueHolder& action_value_holder,
    BuildingTool* building_tool) {
  // const auto& toggle_building_mode = action_listener.action_values.at(build_action_ids.toggle_building_mode);
  if (!action_value_holder.IsSwitchedOn()) {
    return;
  }

  // building_tool не работает. Какой-то баг во флексе.
  if (!e.has<BuildingTool>()) {
    // TODO: Эта функция не работает! Разобраться!!!
    LOG_INFO("~~~ ToggleBuildingMode add<BuildingTool>() = {}", building_tool == nullptr);
    e.add<BuildingTool>();
    action_listener.action_group_priority.push_back(kBuildingActionGroup);
  } else {
    LOG_INFO("~~~ ToggleBuildingMode remove<BuildingTool>() = {}", building_tool == nullptr);
    e.remove<BuildingTool>();
    std::erase(action_listener.action_group_priority, kBuildingActionGroup);
  }
}

void ApplyBuildActionListener(
    flecs::entity e,
    z13::input::ActionListener& action_listener,
    const BuildActionIds& build_action_ids,
    BuildingTool* building_tool) {
  if (build_action_ids.toggle_building_mode) {
    const auto& toggle_building_mode = action_listener.action_values.at(*build_action_ids.toggle_building_mode);
    ToggleBuildingMode(e, action_listener, toggle_building_mode, building_tool);
  }

  if (build_action_ids.build_block) {
    const auto& build_block = action_listener.action_values.at(*build_action_ids.build_block);
    if (build_block.IsSwitchedOn()) {
      LOG_INFO("~~~~ Build block");
    }
  }

  if (build_action_ids.destroy_block) {
    const auto& destroy_block = action_listener.action_values.at(*build_action_ids.destroy_block);
    if (destroy_block.IsSwitchedOn()) {
      LOG_INFO("~~~~ Destroy block");
    }
  }
}

void RegisterComponents(flecs::world world) {
  world.component<BuildActionIds>().add(flecs::Singleton);
}

void RegisterSystems(flecs::world world) {
  world.observer<z13::input::OnConfigUpdatedEvent, z13::input::ActionMap>()
      .event<z13::input::SystemInputEventType>()
      .each(OnConfigUpdated);

  world.system<z13::input::ActionListener, BuildActionIds, BuildingTool*>("gameplay_input_system::ApplyBuildActionListener")
      .kind<z13::input::ApplyActionFramePhase>()
      .without<z13::gameplay::Pause>()
      .each(ApplyBuildActionListener);

  world.observer<z13::input::AppendInputSchema, z13::input::ActionMap>()
      .event<z13::input::AppendInputSchema>()
      .each(OnAppendInputSchema);
}

}  // namespace

void BuildingInputSystem::Register(flecs::world& world) {
  world.observer<RegisterComponentsEvent>("BuildingInputSystem::RegisterComponents")
    .event(flecs::OnAdd)
    .yield_existing()
    .each([world = world](const auto&) { RegisterComponents(world); });

  world.observer<InitSystemsEvent>("BuildingInputSystem::RegisterSystems")
    .event(flecs::OnAdd)
    .yield_existing()
    .each([world = world](const auto&) { RegisterSystems(world); });
}

}  // namespace z13::building
