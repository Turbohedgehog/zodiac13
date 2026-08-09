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

#include "gameplay_input_system.h"

#include <limits>

#include <Eigen/Dense>
#include <flecs.h>

#include <lib_core/components.h>
#include <lib_core/log.h>
#include <lib_core/math.h>
#include <lib_core/flecs_utils.h>

#include <z13/components/status.h>
#include <z13/components/gameplay.h>
#include <z13/components/input.h>
#include <z13_module/tools/z13_environment.h>
#include <z13_module/input/input_config_loader.h>

#include <input_config_generated.h>

#include <actions_generated.h>


#include "../private_components/input_system_components.h"

namespace z13::gameplay::input {

namespace {

struct MoveActionIds {
  z13::input::ActionInfo::IdType move_forward_id = 0;
  z13::input::ActionInfo::IdType move_backward_id = 0;
  z13::input::ActionInfo::IdType move_right_id = 0;
  z13::input::ActionInfo::IdType move_left_id = 0;
  z13::input::ActionInfo::IdType move_up_id = 0;
  z13::input::ActionInfo::IdType move_down_id = 0;
  z13::input::ActionInfo::IdType vertical_look_id = 0;
  z13::input::ActionInfo::IdType horizontal_look_id = 0;
};

struct InputListenerQueryComponent {
  flecs::query<z13::input::CurrentActionListenerTag, z13::input::ActionListener> listener_query;
};


// todo: remove code duplicate
size_t KeyCodeToArrayIndex(z13::fbs::input::Keycode keyboard_code) {
  auto min = static_cast<int>(z13::fbs::input::Keycode::MIN);
  auto val = static_cast<int>(keyboard_code);
  auto cur = val - min;

  return static_cast<size_t>(cur);
}

z13::fbs::input::Keycode ArrayIndexToKeyCode(size_t idx) {
  auto index = static_cast<int>(idx);
  auto min = static_cast<int>(z13::fbs::input::Keycode::MIN);
  auto code_idx = index + min;

  return static_cast<z13::fbs::input::Keycode>(code_idx);
}

void OnMousePos(
    const z13::input::MousePos& mouse_pos,
    const InputListenerQueryComponent& listener_query_component,
    const z13::input::InputConfig& input_config) {
  // listener_query_component.listener_query.each([&mouse_pos](
  //     flecs::entity e,
  //     const z13::input::InputListener&,
  //     geometry::Transform& transform) {
  //       // LOG_INFO("~~~ {} OnMousePos = {}, {}", e.name().c_str(), mouse_pos.x, mouse_pos.y);
  // });
}

void ApplyMoveActionListener(
    flecs::entity e,
    const z13::input::ActionListener& action_listener,
    const MoveActionIds& move_action_ids,
    Eigen::Matrix4f& transform) {
  auto delta_time = e.world().delta_time();
  const auto& action_values = action_listener.action_values;
  auto current_euler_angles = transform.block<3,3>(0,0).eulerAngles(2, 1, 0);
  auto v_rotation_rad = current_euler_angles[1];
  auto h_rotation_rad = current_euler_angles[0];
  auto roll = current_euler_angles[2];

  if (std::fabs(v_rotation_rad) >= z13::math::kHalfPi) {
    v_rotation_rad = std::copysign(1.0, v_rotation_rad) * (z13::math::kPi - std::fabs(v_rotation_rad));
    h_rotation_rad -= z13::math::kPi;
    roll -= z13::math::kPi;
  }

  auto v_rotation_deg = z13::math::ToDegrees(v_rotation_rad);
  auto h_rotation_deg = z13::math::ToDegrees(h_rotation_rad);
  auto v_delta_deg = action_values.at(move_action_ids.vertical_look_id);
  auto h_delta_deg = action_values.at(move_action_ids.horizontal_look_id);

  h_rotation_deg += *h_delta_deg;
  v_rotation_deg -= *v_delta_deg;
  h_rotation_deg = std::clamp(h_rotation_deg, -180.f, 180.f);
  v_rotation_deg = std::clamp(v_rotation_deg, -89.f, 89.f); // 89 degs - euler bug workaround

  auto rotation =
      Eigen::Quaternionf::Identity() *
      Eigen::AngleAxisf(z13::math::ToRadians(h_rotation_deg), Eigen::Vector3f::UnitZ()) *
      Eigen::AngleAxisf(z13::math::ToRadians(v_rotation_deg), Eigen::Vector3f::UnitY()) *
      Eigen::AngleAxisf(roll, Eigen::Vector3f::UnitX());

  auto new_rotation_matrix = rotation.toRotationMatrix();
  transform.block<3,3>(0,0) = new_rotation_matrix;

  auto forward_axis = new_rotation_matrix.col(0);
  auto side_axis = new_rotation_matrix.col(1);
  auto up_axis = new_rotation_matrix.col(2);

  static constexpr float kCamVel = 30.f;

  auto forward_v = forward_axis * *action_values.at(move_action_ids.move_forward_id);
  auto backward_v = forward_axis * *action_values.at(move_action_ids.move_backward_id);
  auto x_delta = (forward_v - backward_v) * delta_time * kCamVel;

  auto right_v = side_axis * *action_values.at(move_action_ids.move_right_id);
  auto left_v = side_axis * *action_values.at(move_action_ids.move_left_id);
  auto y_delta = (left_v - right_v) * delta_time * kCamVel;

  auto up_v = up_axis * *action_values.at(move_action_ids.move_up_id);
  auto down_v = up_axis * *action_values.at(move_action_ids.move_down_id);
  auto z_delta = (up_v - down_v) * delta_time * kCamVel;

  auto position = transform.col(3).head<3>();

  position += x_delta + y_delta + z_delta;

  // if (x_delta.squaredNorm() >= 0.000001f || y_delta.squaredNorm() >= 0.000001f) {
  //   LOG_INFO("==== position = {}, {}, {}", position.x(), position.y(), position.z());
  //   LOG_INFO("==== forward_axis = {}, {}, {}", forward_axis.x(), forward_axis.y(), forward_axis.z());
  //   LOG_INFO("==== side_axis = {}, {}, {}", side_axis.x(), side_axis.y(), side_axis.z());
  // }

  transform.block<3,1>(0,3) = position;
  e.set(transform);
}

void OnMouseMove(
    const z13::input::MouseMoveEvent& mouse_move,
    const InputListenerQueryComponent& listener_query_component,
    const z13::input::InputConfig& input_config,
    const MoveActionIds& move_action_ids) {
  listener_query_component.listener_query.each(
      [&mouse_move, &input_config, &move_action_ids](
          flecs::entity e,
          z13::input::CurrentActionListenerTag,
          z13::input::ActionListener& action_listener) {
        auto delta_time = e.world().delta_time();
        auto factor = delta_time * input_config.mouse_sensitivity;
        auto delta_h_deg = -static_cast<float>(mouse_move.delta.x) * factor;
        auto delta_v_deg = -static_cast<float>(mouse_move.delta.y) * factor;
        if (input_config.invert_x) {
          delta_h_deg = -delta_h_deg;
        }

        if (input_config.invert_y) {
          delta_v_deg = -delta_v_deg;
        }

        auto& action_values = action_listener.action_values;
        action_values[move_action_ids.vertical_look_id] += delta_v_deg;
        action_values[move_action_ids.horizontal_look_id] += delta_h_deg;
      });
}

void OnMouseDown(
    flecs::iter it,
    size_t,
    const z13::input::MouseButtonDownEvent& mouse_down,
    const InputListenerQueryComponent& input_listener_query,
    const z13::input::InputConfig& input_config,
    z13::input::InputState& input_state) {
  input_state.input_state[KeyCodeToArrayIndex(mouse_down.button)] = 1.f;
  
  auto world = it.world();
  if (world.has<z13::gameplay::Pause>()) {
    world.event<z13::input::SystemInputEventType>()
        .id<z13::input::WindowKeyDownEvent>()
        .entity(world.entity().set<z13::input::WindowKeyDownEvent>({.key_code = mouse_down.button,}))
        .enqueue();
  }
}

void OnMouseUp(
    const z13::input::MouseButtonUpEvent& mouse_up,
    const InputListenerQueryComponent& input_listener_query,
    const z13::input::InputConfig& input_config,
    z13::input::InputState& input_state) {
  input_state.input_state[KeyCodeToArrayIndex(mouse_up.button)] = 0.f;
}

void OnKeyboardDown(
    flecs::iter it,
    size_t,
    const z13::input::KeyboardDownEvent& key_down,
    const InputListenerQueryComponent& input_listener_query,
    const z13::input::InputConfig& input_config,
    z13::input::InputState& input_state) {
  auto world = it.world();
  if (key_down.keycode.code == z13::fbs::input::Keycode::KEY_ESCAPE) {
    if (world.has<z13::gameplay::Pause>()) {
      world.event<z13::input::SystemInputEventType>()
          .id<z13::input::WindowBackEvent>()
          .entity(world.entity().add<z13::input::WindowBackEvent>())
          .enqueue();
    } else {
      world.add<z13::gameplay::Pause>();
    }
  } else if (world.has<z13::gameplay::Pause>()) {
    world.event<z13::input::SystemInputEventType>()
        .id<z13::input::WindowKeyDownEvent>()
        .entity(world.entity().set<z13::input::WindowKeyDownEvent>({.key_code = key_down.keycode.code,}))
        .enqueue();
  }
  input_state.input_state[KeyCodeToArrayIndex(key_down.keycode.code)] = 1.f;
}

void OnKeyboardUp(
    const z13::input::KeyboardUpEvent& key_up,
    const InputListenerQueryComponent& input_listener_query,
    const z13::input::InputConfig& input_config,
    z13::input::InputState& input_state) {
  input_state.input_state[KeyCodeToArrayIndex(key_up.keycode.code)] = 0.f;
}

// void OnMousePosEvent(flecs::entity e, z13::input::MousePos& mp) {
//   // LOG_INFO("==== {} OnMousePosEvent = {}, {} -> {}", mp.idx, e.name().c_str(), mp.x, mp.y, e.world().count<input::MousePos>());
// }

void OnSaveConfig(
    z13::input::InputConfig& input_config,
    const z13::input::ActionMap& action_map,
    z13::input::SaveConfigEvent) {
  InputConfigLoader::SaveConfig(input_config, action_map);
}

void CallConfigUpdatedEvent(flecs::world w) {
  w.event<z13::input::SystemInputEventType>()
      .id<z13::input::OnConfigUpdatedEvent>()
      .entity(w.entity().add<z13::input::OnConfigUpdatedEvent>())
      .enqueue();
}

void OnLoadConfig(
    flecs::entity e,
    z13::input::InputConfig& input_config,
    const z13::input::ActionMap& action_map,
    z13::input::LoadConfigEvent) {
  InputConfigLoader::LoadConfig(input_config, action_map);
  CallConfigUpdatedEvent(e.world());
}

void OnSetDefaultConfig(
    flecs::entity e,
    z13::input::InputConfig& input_config,
    const z13::input::ActionMap& action_map,
    z13::input::SetDefaultConfigEvent) {
  InputConfigLoader::SetDefaults(input_config, action_map);
  CallConfigUpdatedEvent(e.world());
}

void AppendFlatbufActionsFromBinarySchema(
    const z13::input::FlatbufferBinarySchema& binary_schema,
    z13::input::ActionMap& action_map) {
  InputConfigLoader::AppendFlatbufActionsFromBinarySchema(binary_schema, action_map);
}

void OnConfigUpdated(flecs::entity e, z13::input::OnConfigUpdatedEvent, const z13::input::ActionMap& action_map) {
  using EnumValueType = z13::input::ActionInfo::EnumValueType;
  auto& move_action_ids = e.world().ensure<MoveActionIds>();
  move_action_ids = MoveActionIds();
  const auto& enum_value = action_map.action_map.get<z13::input::ActionMap::EnumNameEnumValueTag>();

  auto apply_action_id = [&](const auto action_value, auto& action_id_holder) {
    auto action_id = InputConfigLoader::FindActionId(action_map.action_map, "z13.fbs.actions.Action", action_value);
    if (action_id) {
      action_id_holder = *action_id;
    } else {
      LOG_ERROR(
        "OnConfigUpdated: Cannot find action id '{}' for enum 'z13.fbs.actions.Action'",
        static_cast<EnumValueType>(action_value)
      );
    }
  };

  apply_action_id(z13::fbs::actions::Action::MOVE_FORWARD, move_action_ids.move_forward_id);
  apply_action_id(z13::fbs::actions::Action::MOVE_BACKWARD, move_action_ids.move_backward_id);
  apply_action_id(z13::fbs::actions::Action::MOVE_LEFT, move_action_ids.move_left_id);
  apply_action_id(z13::fbs::actions::Action::MOVE_RIGHT, move_action_ids.move_right_id);
  apply_action_id(z13::fbs::actions::Action::JUMP, move_action_ids.move_up_id);
  apply_action_id(z13::fbs::actions::Action::CROUCH, move_action_ids.move_down_id);
  apply_action_id(z13::fbs::actions::Action::VERTICAL_LOOK, move_action_ids.vertical_look_id);
  apply_action_id(z13::fbs::actions::Action::HORIZONTAL_LOOK, move_action_ids.horizontal_look_id);
}

void OnAppendInputSchema(
    flecs::iter it,
    size_t,
    const z13::input::AppendInputSchema,
    z13::input::ActionMap& action_map) {
  z13::input::FlatbufferBinarySchema ev {
      .binary_schema = std::span {
        z13::fbs::actions::ActionsTableBinarySchema::data(),
        z13::fbs::actions::ActionsTableBinarySchema::size()
      },
  };
  InputConfigLoader::AppendFlatbufActionsFromBinarySchema(ev, action_map);
}

void OnInputSystemStartupGameEvent(
    flecs::iter it,
    size_t,
    z13::input::InputConfig& input_config,
    z13::input::ActionMap& action_map,
    status::OnStartupGameEvent) {
  action_map.action_map.clear();

  {
    WorldNoDeferGuard no_defer(it.world());
    it.world().event<z13::input::AppendInputSchema>()
      .id<z13::input::AppendInputSchema>()
      .entity(it.world().entity().add<z13::input::AppendInputSchema>())
      .emit();
  }

  LOG_INFO("~~~~ OnInputSystemStartupGameEvent");

  if (!InputConfigLoader::LoadConfig(input_config, action_map)) {
    InputConfigLoader::SetDefaults(input_config, action_map);
    InputConfigLoader::SaveConfig(input_config, action_map);
  }

  CallConfigUpdatedEvent(it.world());
}

void ClearActionListenerCurrentState(
    z13::input::ActionListener& action_listener,
    const z13::input::ActionMap& action_map) {
  const auto& action_id_map = action_map.action_map.get<z13::input::ActionMap::IdTag>();
  for (const auto& action_info : action_id_map) {
    action_listener.action_values[action_info.id].IterateToNextState();
  }
}

void CalculateInputValues(
    const z13::input::InputState& input_state,
    const z13::input::InputConfig& input_config,
    z13::input::ActionListener& action_listener) {
  const auto& action_group_key_codes = input_config.keycode_binding.get<z13::input::InputConfig::ActionGroupKeycodeIdTag>();
  const auto& key_codes = input_config.keycode_binding.get<z13::input::InputConfig::KeycodeIdTag>();
  for (size_t key_idx = 0; key_idx < input_state.input_state.size(); ++key_idx) {
    auto key_code = static_cast<z13::fbs::input::Keycode>(key_idx);
    auto key_value = input_state.input_state[key_idx];
    if (std::abs(key_value) <= std::numeric_limits<float>::epsilon()) {
      continue;
    }

    if (action_listener.action_group_priority.empty()) {
      if (auto ag_it = key_codes.find(key_code); ag_it != key_codes.end()) {
        action_listener.action_values[ag_it->action_id] += key_value;
        // LOG_INFO("~~~~ 1 action_listener.action_values[{}] = {}", ag_it->action_id, action_listener.action_values[ag_it->action_id].current_value);
      }
    } else {
      for (
          auto action_group_it = action_listener.action_group_priority.rbegin();
          action_group_it != action_listener.action_group_priority.rend() && !action_group_it->empty();
          ++action_group_it) {
        auto agkc_it = action_group_key_codes.find(std::make_tuple(*action_group_it, key_code));
        if (agkc_it != action_group_key_codes.end()) {
          // LOG_INFO("=== agkc_it = {}", agkc_it->display_text);
          action_listener.action_values[agkc_it->action_id] += key_value;
          // LOG_INFO("~~~~ 2 action_listener.action_values[{}] = {}", agkc_it->action_id, action_listener.action_values[agkc_it->action_id].current_value);
          break;
        }
      }
    }
  }
}

void RegisterPhases(flecs::world world) {
  world.component<z13::input::ClearActionFramePhase>().add(flecs::Phase).depends_on(flecs::PreFrame);
  world.get_alive(flecs::OnLoad).add(flecs::Phase).depends_on<z13::input::ClearActionFramePhase>();

  world.component<z13::input::CalculateActionFramePhase>().add(flecs::Phase).depends_on(flecs::OnUpdate);
  world.component<z13::input::ApplyActionFramePhase>().add(flecs::Phase).depends_on<z13::input::CalculateActionFramePhase>();
  world.get_alive(flecs::PostUpdate).add(flecs::Phase).depends_on<z13::input::ApplyActionFramePhase>();
}

void RegisterSystems(flecs::world world) {
  world.observer<
      z13::input::MousePos,
      InputListenerQueryComponent,
      z13::input::InputConfig>("gameplay_input_system::OnMousePosObserver")
      .event<z13::input::SystemInputEventType>()
      .without<z13::gameplay::Pause>()
      .each(OnMousePos);

  world.observer<
      z13::input::MouseMoveEvent,
      InputListenerQueryComponent,
      z13::input::InputConfig,
      MoveActionIds>("gameplay_input_system::OnMouseMoveObserver")
      .event<z13::input::SystemInputEventType>()
      // .with<z13::gameplay::Pause>().not_()
      .each(OnMouseMove);

  world.observer<
      z13::input::MouseButtonDownEvent,
      InputListenerQueryComponent,
      z13::input::InputConfig,
      z13::input::InputState>("gameplay_input_system::OnMouseDownObserver")
      .event<z13::input::SystemInputEventType>()
      // .with<z13::gameplay::Pause>().not_()
      .each(OnMouseDown);

  world.observer<
      z13::input::MouseButtonUpEvent,
      InputListenerQueryComponent,
      z13::input::InputConfig,
      z13::input::InputState>("gameplay_input_system::OnMouseUpObserver")
      .event<z13::input::SystemInputEventType>()
      // .with<z13::gameplay::Pause>().not_()
      .each(OnMouseUp);

  world.observer<
      z13::input::KeyboardDownEvent,
      InputListenerQueryComponent,
      z13::input::InputConfig,
      z13::input::InputState>("gameplay_input_system::OnKeyboardDownObserver")
      .event<z13::input::SystemInputEventType>()
      // .with<z13::gameplay::Pause>().not_()
      .each(OnKeyboardDown);

  world.observer<
      z13::input::KeyboardUpEvent,
      InputListenerQueryComponent,
      z13::input::InputConfig,
      z13::input::InputState>("gameplay_input_system::OnKeyboardUpObserver")
      .event<z13::input::SystemInputEventType>()
      // .with<z13::gameplay::Pause>().not_()
      .each(OnKeyboardUp);

  world.observer<z13::input::InputConfig, z13::input::ActionMap, z13::status::OnStartupGameEvent>("gameplay_input_system::OnStartupGameEvent")
      .event(flecs::OnAdd)
      .yield_existing()
      .each(OnInputSystemStartupGameEvent);

  world.system<z13::input::ActionListener, z13::input::ActionMap>("gameplay_input_system::ClearActionListenerCurrentState")
      .kind<z13::input::ClearActionFramePhase>()
      .each(ClearActionListenerCurrentState);

  world.system<z13::input::InputState, z13::input::InputConfig, z13::input::ActionListener>("gameplay_input_system::CalculateInputValues")
      .kind<z13::input::CalculateActionFramePhase>()
      .without<z13::gameplay::Pause>()
      .each(CalculateInputValues);

  world.system<z13::input::ActionListener, MoveActionIds, Eigen::Matrix4f>("gameplay_input_system::ApplyMoveActionListener")
      .kind<z13::input::ApplyActionFramePhase>()
      .without<z13::gameplay::Pause>()
      .each(ApplyMoveActionListener);

  world.observer<z13::input::InputConfig, const z13::input::ActionMap, z13::input::SaveConfigEvent>("gameplay_input_system::OnSaveConfigObserver")
      .event<z13::input::SystemInputEventType>()
      .each(OnSaveConfig);

  world.observer<z13::input::InputConfig, const z13::input::ActionMap, z13::input::LoadConfigEvent>("gameplay_input_system::LoadConfigObserver")
      .event<z13::input::SystemInputEventType>()
      .each(OnLoadConfig);

  world.observer<z13::input::InputConfig, const z13::input::ActionMap, z13::input::SetDefaultConfigEvent>("gameplay_input_system::OnSetDefaultConfigObserver")
      .event<z13::input::SystemInputEventType>()
      .each(OnSetDefaultConfig);

  world.observer<const z13::input::FlatbufferBinarySchema, z13::input::ActionMap>("gameplay_input_system::AppendFlatbufActionsFromBinarySchema")
      .event<z13::input::SystemInputEventType>()
      .each(AppendFlatbufActionsFromBinarySchema);

  world.observer<z13::input::OnConfigUpdatedEvent, z13::input::ActionMap>()
      .event<z13::input::SystemInputEventType>()
      .each(OnConfigUpdated);

  world.observer<z13::input::AppendInputSchema, z13::input::ActionMap>()
      .event<z13::input::AppendInputSchema>()
      .each(OnAppendInputSchema);
}

}  // namespace

void GameplayInputSystem::Register(flecs::world& world) {
  world.observer<RegisterComponentsEvent>()
    .event(flecs::OnAdd)
    .yield_existing()
    .each([world = world](const auto&) {
      world.component<InputListenerQueryComponent>().add(flecs::Singleton);
      world.component<z13::input::InputState>().add(flecs::Singleton);
      world.component<z13::gameplay::input::MoveActionIds>().add(flecs::Singleton);
    });

  world.observer<InitSystemsEvent>()
    .event(flecs::OnAdd)
    .yield_existing()
    .each([world = world](const auto&) {
      RegisterSystems(world);
    });

  world.observer<InitPhasesEvent>()
    .event(flecs::OnAdd)
    .yield_existing()
    .each([world = world](const auto&) {
      RegisterPhases(world);
    });

  world.observer<InitWorldDataEvent>()
    .event(flecs::OnAdd)
    .yield_existing()
    .each([world = world](const auto&) {
      world.add<z13::input::InputState>();
      world.add<z13::gameplay::input::MoveActionIds>();

      InputListenerQueryComponent input_listener_query_component = {
          .listener_query = world
              .query_builder<z13::input::CurrentActionListenerTag, z13::input::ActionListener>("InputListenerQuery")
              .build(),
      };
      world.set(input_listener_query_component);
    });

  LOG_INFO("=== GameplayInputSystem::Register {}", z13::tools::environment::GetGameInputConfigJsonPath().string());
}

} // namespace z13::gameplay::input
