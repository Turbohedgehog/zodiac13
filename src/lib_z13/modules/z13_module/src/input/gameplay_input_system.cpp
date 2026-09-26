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

#include <cmath>
#include <limits>

#include <Eigen/Dense>
#include <flecs.h>

#include <lib_core/components.h>
#include <lib_core/log.h>
#include <lib_core/math.h>
#include <lib_core/flecs_utils.h>
#include <lib_core/rollback.h>
#include <lib_core/world_state.h>

#include <z13/components/status.h>
#include <z13/components/gameplay.h>
#include <z13/components/input.h>
#include <z13/components/player_action.h>
#include <z13_module/tools/z13_environment.h>
#include <z13_module/input/input_config_loader.h>
#include <z13_module/gameplay/camera_look.h>

#include <input_config_generated.h>

#include <actions_generated.h>

namespace z13::gameplay::input {

namespace {

struct MoveActionIds {
  using Singleton = void;
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
  using Singleton = void;
  flecs::query<z13::input::CurrentActionListenerTag, z13::input::ActionListener> listener_query;
};

constexpr int kKeycodeMin = static_cast<int>(z13::fbs::input::Keycode::MIN);

size_t KeyCodeToArrayIndex(z13::fbs::input::Keycode keyboard_code) {
  return static_cast<size_t>(static_cast<int>(keyboard_code) - kKeycodeMin);
}

z13::fbs::input::Keycode ArrayIndexToKeyCode(size_t idx) {
  return static_cast<z13::fbs::input::Keycode>(static_cast<int>(idx) + kKeycodeMin);
}

void OnMousePos(
    const z13::input::MousePos& mouse_pos,
    const InputListenerQueryComponent& listener_query_component,
    const z13::input::InputConfig& input_config) {
  // listener_query_component.listener_query.each([&mouse_pos](
  //     flecs::entity e,
  //     const z13::input::InputListener&,
  //     geometry::Transform& transform) {
  //       // log_info("~~~ {} OnMousePos = {}, {}", e.name().c_str(), mouse_pos.x, mouse_pos.y);
  // });
}

void ApplyMoveActionListener(
    flecs::entity e,
    const z13::input::ActionListener& action_listener,
    const MoveActionIds& move_action_ids,
    Eigen::Matrix4f& transform) {
  auto delta_time = e.world().delta_time();
  const auto& action_values = action_listener.action_values;

  // Seed LookAngles from the transform once, on the entity's first frame here, so a
  // non-identity spawn rotation isn't discarded by ApplyCameraMove's accumulation.
  if (!e.has<LookAngles>()) {
    const Eigen::Vector3f forward = transform.block<3, 3>(0, 0).col(0);
    LookAngles initial;
    initial.pitch_deg = z13::math::ToDegrees(-std::asin(std::clamp(forward.z(), -1.f, 1.f)));
    initial.yaw_deg = z13::math::ToDegrees(std::atan2(forward.y(), forward.x()));
    e.set(initial);
  }
  auto& look = e.ensure<LookAngles>();

  const auto value_of = [&action_listener](z13::input::ActionInfo::IdType action_id) {
    const std::optional<z13::input::ActionValueHolder> value = action_listener.Value(action_id);
    return value ? value->current_value : 0.f;
  };

  z13::gameplay::CameraMoveAxes axes {
      .forward = value_of(move_action_ids.move_forward_id),
      .backward = value_of(move_action_ids.move_backward_id),
      .right = value_of(move_action_ids.move_right_id),
      .left = value_of(move_action_ids.move_left_id),
      .up = value_of(move_action_ids.move_up_id),
      .down = value_of(move_action_ids.move_down_id),
      .yaw_delta_deg = value_of(move_action_ids.horizontal_look_id),
      .pitch_delta_deg = value_of(move_action_ids.vertical_look_id),
  };

  z13::gameplay::ApplyCameraMove(axes, delta_time, look, transform);
  e.set(transform);
}

void OnMouseMove(
    flecs::iter it,
    size_t,
    const z13::input::MouseMoveEvent& mouse_move,
    const z13::input::InputConfig& input_config,
    z13::input::InputState& input_state) {
  auto delta_time = it.world().delta_time();
  auto factor = delta_time * input_config.mouse_sensitivity;
  auto delta_h_deg = -static_cast<float>(mouse_move.delta.x) * factor;
  auto delta_v_deg = -static_cast<float>(mouse_move.delta.y) * factor;
  if (input_config.invert_x) {
    delta_h_deg = -delta_h_deg;
  }

  if (input_config.invert_y) {
    delta_v_deg = -delta_v_deg;
  }

  input_state.mouse_yaw_delta_deg += delta_h_deg;
  input_state.mouse_pitch_delta_deg += delta_v_deg;
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
//   // log_info("==== {} OnMousePosEvent = {}, {} -> {}", mp.idx, e.name().c_str(), mp.x, mp.y, e.world().count<input::MousePos>());
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
      log_error(
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
    const z13::input::InputConfigPersistenceSettings& persistence,
    status::OnStartupGameEvent) {
  action_map.action_map.clear();

  {
    WorldNoDeferGuard no_defer(it.world());
    it.world().event<z13::input::AppendInputSchema>()
      .id<z13::input::AppendInputSchema>()
      .entity(it.world().entity().add<z13::input::AppendInputSchema>())
      .emit();
  }

  log_info("~~~~ OnInputSystemStartupGameEvent");

  if (!persistence.load_config_from_file ||
      !InputConfigLoader::LoadConfig(input_config, action_map)) {
    InputConfigLoader::SetDefaults(input_config, action_map);
    if (persistence.load_config_from_file) {
      InputConfigLoader::SaveConfig(input_config, action_map);
    }
  }

  CallConfigUpdatedEvent(it.world());
}

// ActionListener isn't state, so a restored player never gets one -- backfill it for
// every Player, local or not.
void EnsurePlayerActionListener(flecs::entity e, const z13::gameplay::Player&) {
  e.set(z13::input::ActionListener{.action_group_priority = {std::string(z13::input::kControlActionGroup)}});
}

// Locality (live input routing) only, not ActionListener: re-derived every frame so it
// self-heals the stale tags a snapshot leaves behind.
void SyncLocalPlayerListener(
    flecs::entity e, const z13::gameplay::Player& player, const z13::gameplay::LocalPlayer& local_player) {
  const bool should_be_local = local_player.id == player.id;
  if (e.has<z13::input::CurrentActionListenerTag>() == should_be_local) {
    return;
  }

  if (should_be_local) {
    e.add<z13::input::InputListener>();
    e.add<z13::input::CurrentActionListenerTag>();
  } else {
    e.remove<z13::input::InputListener>();
    e.remove<z13::input::CurrentActionListenerTag>();
  }
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
    z13::input::InputState& input_state,
    const z13::input::InputConfig& input_config,
    const MoveActionIds& move_action_ids,
    z13::input::ActionListener& action_listener) {
  // Fold in and reset the mouse-look delta here, since this phase reliably runs after Clear.
  action_listener.action_values[move_action_ids.horizontal_look_id] += input_state.mouse_yaw_delta_deg;
  action_listener.action_values[move_action_ids.vertical_look_id] += input_state.mouse_pitch_delta_deg;
  input_state.mouse_yaw_delta_deg = 0.f;
  input_state.mouse_pitch_delta_deg = 0.f;

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
        // log_info("~~~~ 1 action_listener.action_values[{}] = {}", ag_it->action_id, action_listener.action_values[ag_it->action_id].current_value);
      }
    } else {
      for (
          auto action_group_it = action_listener.action_group_priority.rbegin();
          action_group_it != action_listener.action_group_priority.rend() && !action_group_it->empty();
          ++action_group_it) {
        auto agkc_it = action_group_key_codes.find(std::make_tuple(*action_group_it, key_code));
        if (agkc_it != action_group_key_codes.end()) {
          // log_info("=== agkc_it = {}", agkc_it->display_text);
          action_listener.action_values[agkc_it->action_id] += key_value;
          // log_info("~~~~ 2 action_listener.action_values[{}] = {}", agkc_it->action_id, action_listener.action_values[agkc_it->action_id].current_value);
          break;
        }
      }
    }
  }
}

void RegisterPhases(flecs::world world) {
  world.component<z13::input::ScheduledCommandsPhase>().add(flecs::Phase).depends_on(flecs::PreFrame);
  world.component<z13::input::ClearActionFramePhase>().add(flecs::Phase).depends_on<z13::input::ScheduledCommandsPhase>();
  world.get_alive(flecs::OnLoad).add(flecs::Phase).depends_on<z13::input::ClearActionFramePhase>();

  world.component<z13::input::CalculateActionFramePhase>().add(flecs::Phase).depends_on(flecs::OnUpdate);
  world.component<z13::input::RecordActionFramePhase>().add(flecs::Phase).depends_on<z13::input::CalculateActionFramePhase>();
  world.component<z13::input::RemoteActionFramePhase>().add(flecs::Phase).depends_on<z13::input::RecordActionFramePhase>();
  world.component<z13::input::ApplyActionFramePhase>().add(flecs::Phase).depends_on<z13::input::RemoteActionFramePhase>();
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
      z13::input::InputConfig,
      z13::input::InputState>("gameplay_input_system::OnMouseMoveObserver")
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

  world.observer<z13::input::InputConfig, z13::input::ActionMap,
      z13::input::InputConfigPersistenceSettings, z13::status::OnStartupGameEvent>(
      "gameplay_input_system::OnStartupGameEvent")
      .event(flecs::OnAdd)
      .yield_existing()
      .each(OnInputSystemStartupGameEvent);

  world.system<const z13::gameplay::Player>("gameplay_input_system::EnsurePlayerActionListener")
      .kind<z13::input::ClearActionFramePhase>()
      .without<z13::input::ActionListener>()
      // Visible to ClearActionListenerCurrentState etc. later this same phase/frame.
      .write<z13::input::ActionListener>()
      .each(EnsurePlayerActionListener);

  world.system<const z13::gameplay::Player, const z13::gameplay::LocalPlayer>(
      "gameplay_input_system::SyncLocalPlayerListener")
      .kind<z13::input::ClearActionFramePhase>()
      // A cold add must be visible to CalculateInputValues later this same phase/frame.
      .write<z13::input::InputListener>()
      .write<z13::input::CurrentActionListenerTag>()
      .each(SyncLocalPlayerListener);

  world.system<z13::input::ActionListener, z13::input::ActionMap>("gameplay_input_system::ClearActionListenerCurrentState")
      .kind<z13::input::ClearActionFramePhase>()
      .each(ClearActionListenerCurrentState);

  world.system<z13::input::InputState, z13::input::InputConfig, const MoveActionIds, z13::input::ActionListener>(
      "gameplay_input_system::CalculateInputValues")
      .kind<z13::input::CalculateActionFramePhase>()
      .without<z13::gameplay::Pause>()
      // Replay::InjectRecordedActionValues (same phase) drives action_values instead -- see replay.cpp.
      .without<z13::flecs_tools::ReplayInProgress>()
      // InputState is one shared singleton (one local keyboard/mouse); a second player
      // entity's ActionListener must not mirror it too.
      .with<z13::input::CurrentActionListenerTag>()
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
      flecs::world w = world;
      z13::flecs_tools::RegisterComponents<
          InputListenerQueryComponent, z13::input::InputState, MoveActionIds>(w);
      w.component<z13::gameplay::LookAngles>();
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

  log_info("=== GameplayInputSystem::Register {}", z13::tools::environment::GetGameInputConfigJsonPath().string());
}

} // namespace z13::gameplay::input
