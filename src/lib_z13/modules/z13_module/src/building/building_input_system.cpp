#include "building_input_system.h"

#include <algorithm>

#include <flecs.h>

#include <building_generated.h>

#include <lib_core/log.h>
#include <z13/components/gameplay.h>
#include <z13/components/input.h>
#include <z13/components/building.h>
#include <z13_module/input/input_config_loader_2.h>

namespace z13::building {

// todo: убрать константу и брать из z13.fbs.building.Action.action_group
static const std::string kBuildingActionGroup = "Building";

struct BuildActionIds {
  z13::input::ActionInfo::IdType toggle_building_mode = 0;
  z13::input::ActionInfo::IdType build_block = 0;
  z13::input::ActionInfo::IdType destroy_block = 0;
};

void OnConfigUpdated(flecs::entity e, z13::input::OnConfigUpdatedEvent, const z13::input::ActionMap& action_map) {
  using EnumValueType = z13::input::ActionInfo::EnumValueType;
  auto& build_action_ids = e.world().ensure<BuildActionIds>();
  build_action_ids = BuildActionIds();
  const auto& enum_value = action_map.action_map.get<z13::input::ActionMap::EnumNameEnumValueTag>();

  auto apply_action_id = [&](const auto action_value, auto& action_id_holder) {
    auto action_id = z13::gameplay::input::InputConfigLoader2::FindActionId(action_map.action_map, "z13.fbs.building.Action", action_value);
    if (action_id) {
      action_id_holder = *action_id;
    } else {
      LOG_ERROR(
        "OnConfigUpdated: Cannot find action id '{}' for enum 'z13.fbs.building'",
        static_cast<EnumValueType>(action_value)
      );
    }
  };

  apply_action_id(z13::fbs::building::Action::TOGGLE_BUILDING_MODE, build_action_ids.toggle_building_mode);
  apply_action_id(z13::fbs::building::Action::BUILD_BLOCK, build_action_ids.build_block);
  apply_action_id(z13::fbs::building::Action::DESTROY_BLOK, build_action_ids.destroy_block);

  // LOG_INFO("~~~ build_action_ids.toggle_building_mode = {}", build_action_ids.toggle_building_mode);
  // LOG_INFO("~~~ build_action_ids.build_block = {}", build_action_ids.build_block);
  // LOG_INFO("~~~ build_action_ids.destroy_block = {}", build_action_ids.destroy_block);
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
  const auto& toggle_building_mode = action_listener.action_values.at(build_action_ids.toggle_building_mode);
  ToggleBuildingMode(e, action_listener, toggle_building_mode, building_tool);

  const auto& build_block = action_listener.action_values.at(build_action_ids.build_block);
  if (build_block.IsSwitchedOn()) {
    LOG_INFO("~~~~ Build block");
  }

  const auto& destroy_block = action_listener.action_values.at(build_action_ids.destroy_block);
  if (destroy_block.IsSwitchedOn()) {
    LOG_INFO("~~~~ Destroy block");
  }
}

void BuildingInputSystem::Register(flecs::world& world) {
  world.component<BuildActionIds>().add(flecs::Singleton);

  world.observer<z13::input::OnConfigUpdatedEvent, z13::input::ActionMap>()
      .event<z13::input::SystemInputEventType>()
      .each(OnConfigUpdated);

  world.system<z13::input::ActionListener, BuildActionIds, BuildingTool*>("gameplay_input_system::ApplyBuildActionListener")
      .kind<z13::input::ApplyActionFramePhase>()
      .without<z13::gameplay::Pause>()
      .each(ApplyBuildActionListener);
}

}  // namespace z13::building
