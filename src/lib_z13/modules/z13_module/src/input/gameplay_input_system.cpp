#include "gameplay_input_system.h"

#include <limits>

#include <Eigen/Dense>
#include <flecs.h>
#include <lib_core/log.h>
#include <lib_core/math.h>

#include <z13_module/components/status.h>
#include <z13_module/components/gameplay.h>
#include <z13_module/components/input.h>
#include <z13_module/components/geometry.h>
#include <z13_module/tools/z13_environment.h>
#include <z13_module/input/input_config_loader.h>

#include <input_config.pb.h>

#include "../private_components/input_system_components.h"

#include "input_converter.h"

namespace z13::gameplay::input {

struct ClearActionFramePhase {};
struct CalculateActionFramePhase {};
struct ApplyActionFramePhase {};

struct InputListenerQueryComponent {
  flecs::query<z13::input::CurrentActionListenerTag, z13::input::ActionListener> listener_query;
};

size_t ActionToArrayIndex(z13::proto::input::Action_ActionType action_type) {
  auto min = static_cast<int>(z13::proto::input::Action_ActionType_ActionType_MIN);
  auto val = static_cast<int>(action_type);
  auto cur = val - min;
  return static_cast<size_t>(cur);
}

// todo: remove code duplicate
size_t KeyCodeToArrayIndex(z13::proto::input::Keyboard_Code keyboard_code) {
  auto min = static_cast<int>(z13::proto::input::Keyboard_Code_Code_MIN);
  auto val = static_cast<int>(keyboard_code);
  auto cur = val - min;
  return static_cast<size_t>(cur);
}

z13::proto::input::Keyboard::Code ArrayIndexToKeyCode(size_t idx) {
  auto index = static_cast<int>(idx);
  auto min = static_cast<int>(z13::proto::input::Keyboard_Code_Code_MIN);
  auto code_idx = index + min;
  return static_cast<z13::proto::input::Keyboard::Code>(code_idx);
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

void ApplyActionListener(
    flecs::entity e,
    const z13::input::ActionListener& action_listener,
    geometry::Transform& transform) {
  auto delta_time = e.world().delta_time();
  const auto& action_values = action_listener.action_values;
  auto current_rotation = transform.rotation;
  Eigen::Quaternionf current_eigen_rotation = {
    current_rotation.w,
    current_rotation.x,
    current_rotation.y,
    current_rotation.z
  };

  auto rotation_matrix = current_eigen_rotation.toRotationMatrix();
  auto current_euler_angles = rotation_matrix.eulerAngles(1, 0, 2);
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
  auto v_delta_deg = action_values[ActionToArrayIndex(z13::proto::input::Action::VERTICAL_LOOK)];
  auto h_delta_deg = action_values[ActionToArrayIndex(z13::proto::input::Action::HORIZONTAL_LOOK)];

  h_rotation_deg += h_delta_deg;
  v_rotation_deg += v_delta_deg;
  h_rotation_deg = std::clamp(h_rotation_deg, -180.f, 180.f);
  v_rotation_deg = std::clamp(v_rotation_deg, -89.f, 89.f); // 89 degs - euler bug workaround

  auto rotation = Eigen::Quaternionf::Identity()
      * Eigen::AngleAxisf(z13::math::ToRadians(h_rotation_deg), Eigen::Vector3f::UnitY())
      * Eigen::AngleAxisf(z13::math::ToRadians(v_rotation_deg), Eigen::Vector3f::UnitX())
      * Eigen::AngleAxisf(roll, Eigen::Vector3f::UnitZ())
  ;

  transform.rotation = {
    .x = rotation.x(),
    .y = rotation.y(),
    .z = rotation.z(),
    .w = rotation.w(),
  };

  auto new_rotation_matrix = rotation.toRotationMatrix();
  auto forward_axis = new_rotation_matrix.col(0);
  auto side_axis = new_rotation_matrix.col(1);
  // auto forward_axis = new_rotation_matrix.row(0);
  // auto side_axis = new_rotation_matrix.row(1);

  auto forward_v = forward_axis * action_values[ActionToArrayIndex(z13::proto::input::Action::MOVE_FORWARD)];
  auto backward_v = forward_axis * action_values[ActionToArrayIndex(z13::proto::input::Action::MOVE_BACKWARD)];
  auto x_delta = (forward_v - backward_v) * delta_time;

  // LOG_INFO("===== {}", action_values[ActionToArrayIndex(z13::proto::input::Action::MOVE_FORWARD)]);
  // LOG_INFO("==== x_delta = {}, {}, {}", x_delta.x(), x_delta.y(), x_delta.z());

  auto right_v = side_axis * action_values[ActionToArrayIndex(z13::proto::input::Action::MOVE_RIGHT)];
  auto left_v = side_axis * action_values[ActionToArrayIndex(z13::proto::input::Action::MOVE_LEFT)];
  auto y_delta = (right_v - left_v) * delta_time;

  auto position = Eigen::Vector3f {
    transform.position.x,
    transform.position.y,
    transform.position.z,
  };

  position += x_delta + y_delta;
  // if (x_delta.squaredNorm() >= 0.000001f || y_delta.squaredNorm() >= 0.000001f) {
  //   LOG_INFO("==== position = {}, {}, {}", position.x(), position.y(), position.z());
  //   LOG_INFO("==== forward_axis = {}, {}, {}", forward_axis.x(), forward_axis.y(), forward_axis.z());
  //   LOG_INFO("==== side_axis = {}, {}, {}", side_axis.x(), side_axis.y(), side_axis.z());
  // }
  transform.position = {
    position.x(),
    position.y(),
    position.z(),
  };
}

void OnMouseMove(
    const z13::input::MouseMoveEvent& mouse_move,
    const InputListenerQueryComponent& listener_query_component,
    const z13::input::InputConfig& input_config) {
  listener_query_component.listener_query.each([&mouse_move, &input_config](
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
      action_values[ActionToArrayIndex(z13::proto::input::Action::VERTICAL_LOOK)] += delta_v_deg;
      action_values[ActionToArrayIndex(z13::proto::input::Action::HORIZONTAL_LOOK)] += delta_h_deg;
    });
}

void OnMouseDown(
    const z13::input::MouseButtonDownEvent& mouse_down,
    const InputListenerQueryComponent& input_listener_query,
    const z13::input::InputConfig& input_config,
    z13::input::InputState& input_state) {
  input_state.input_state[KeyCodeToArrayIndex(mouse_down.button)] = 1.f;
}

void OnMouseUp(
    const z13::input::MouseButtonUpEvent& mouse_up,
    const InputListenerQueryComponent& input_listener_query,
    const z13::input::InputConfig& input_config,
    z13::input::InputState& input_state) {
  input_state.input_state[KeyCodeToArrayIndex(mouse_up.button)] = 0.f;
}

void OnKeyboardDown(
    flecs::world world,
    const z13::input::KeyboardDownEvent& key_down,
    const InputListenerQueryComponent& input_listener_query,
    const z13::input::InputConfig& input_config,
    z13::input::InputState& input_state) {
  if (key_down.key.code == z13::proto::input::Keyboard::KEY_ESCAPE) {
    if (world.has<z13::gameplay::Pause>()) {
      world.event<z13::input::SystemInputEvent>()
        .id<WindowBackEvent>()
        .entity(world.entity().add<WindowBackEvent>())
        .enqueue();
    } else {
      world.add<z13::gameplay::Pause>();
    }    
  }
  input_state.input_state[KeyCodeToArrayIndex(key_down.key.code)] = 1.f;
  // LOG_INFO("==== OnKeyboardDown = {}", Keyboard_Code_Name(key_down.key.code));
}

void OnKeyboardUp(
    const z13::input::KeyboardUpEvent& key_up,
    const InputListenerQueryComponent& input_listener_query,
    const z13::input::InputConfig& input_config,
    z13::input::InputState& input_state) {
  input_state.input_state[KeyCodeToArrayIndex(key_up.key.code)] = 0.f;
}

// void OnMousePosEvent(flecs::entity e, z13::input::MousePos& mp) {
//   // LOG_INFO("==== {} OnMousePosEvent = {}, {} -> {}", mp.idx, e.name().c_str(), mp.x, mp.y, e.world().count<input::MousePos>());
// }

void OnInputSystemStartupGameEvent(flecs::entity e, status::OnStartupGameEvent) {
  LoadingInputSystemState loading_input_state = {
    .input_state = std::make_shared<LoadingInputSystemStateRaw>(),
  };
  loading_input_state.input_state->load_config_thread = std::thread(
    [](std::atomic<std::weak_ptr<LoadingInputSystemStateRaw>> load_system_state) {
      z13::input::InputConfig input_config;
      if (!InputConfigLoader::LoadConfig(input_config)) {
        InputConfigLoader::SetDefaults(input_config);
        InputConfigLoader::SaveConfig(input_config);
      }

      auto load_system_state_wptr = load_system_state.load();
      if (load_system_state_wptr.expired()) {
        return;
      }

      auto load_system_state_ptr = load_system_state_wptr.lock();
      load_system_state_ptr->input_config = std::move(input_config);
      load_system_state_ptr->is_complete.store(true, std::memory_order_release);
  }, std::weak_ptr<LoadingInputSystemStateRaw>(loading_input_state.input_state));

  e.world().entity()
    .set(loading_input_state)
    .set(status::Loading {});
}

void LoadingInput(flecs::entity e, LoadingInputSystemState& loading_input_system_state, status::Loading& loading) {
  if (loading_input_system_state.input_state->is_complete.load(std::memory_order_acquire)) {
    loading_input_system_state.input_state->load_config_thread.join();
    e.world().set(std::move(loading_input_system_state.input_state->input_config));
    e.destruct();
  }
}

void ClearActionListener(flecs::entity, z13::input::ActionListener& action_listener) {
  for (auto& value : action_listener.action_values) {
    value = 0.f;
  }
}

void CalculateInputValues(
    const z13::input::InputState& input_state,
    const z13::input::InputConfig& input_config,
    z13::input::ActionListener& action_listener) {
  for (size_t i = 0; i < input_state.input_state.size(); ++i) {
    auto value = input_state.input_state[i];
    if (value < std::numeric_limits<float>::epsilon()) {
      continue;
    }

    auto key_code = ArrayIndexToKeyCode(i);
    auto key_code_to_action = input_config.code_to_action.find(key_code);
    if (key_code_to_action == input_config.code_to_action.end()) {
      continue;
    }

    auto action_idx = ActionToArrayIndex(key_code_to_action->second);
    action_listener.action_values[action_idx] += value;
  }
}

void RegisterPhases(flecs::world& world) {
  world.component<ClearActionFramePhase>().add(flecs::Phase).depends_on(flecs::PreFrame);
  world.get_alive(flecs::OnLoad).add(flecs::Phase).depends_on<ClearActionFramePhase>();

  world.component<CalculateActionFramePhase>().add(flecs::Phase).depends_on(flecs::OnUpdate);
  world.component<ApplyActionFramePhase>().add(flecs::Phase).depends_on<CalculateActionFramePhase>();
  world.get_alive(flecs::PostUpdate).add(flecs::Phase).depends_on<ApplyActionFramePhase>();
}

void GameplayInputSystem::Register(flecs::world& world) {
  LOG_INFO("=== GameplayInputSystem::Register {}", z13::tools::environment::GetGameInputConfigJsonPath().string());
  RegisterPhases(world);
  world.component<InputListenerQueryComponent>().add(flecs::Singleton);
  world.component<z13::input::InputState>().add(flecs::Singleton);
  world.add<z13::input::InputState>();

  world.observer<
      z13::input::MousePos,
      InputListenerQueryComponent,
      z13::input::InputConfig>("gameplay_input_system::OnMousePosObserver")
    .event<z13::input::SystemInputEvent>()
    .with<z13::gameplay::Pause>().not_()
    .each(OnMousePos);

  world.observer<
      z13::input::MouseMoveEvent,
      InputListenerQueryComponent,
      z13::input::InputConfig>("gameplay_input_system::OnMouseMoveObserver")
    .event<z13::input::SystemInputEvent>()
    // .with<z13::gameplay::Pause>().not_()
    .each(OnMouseMove);

  world.observer<
      z13::input::MouseButtonDownEvent,
      InputListenerQueryComponent,
      z13::input::InputConfig,
      z13::input::InputState>("gameplay_input_system::OnMouseDownObserver")
    .event<z13::input::SystemInputEvent>()
    // .with<z13::gameplay::Pause>().not_()
    .each(OnMouseDown);

  world.observer<
      z13::input::MouseButtonUpEvent,
      InputListenerQueryComponent,
      z13::input::InputConfig,
      z13::input::InputState>("gameplay_input_system::OnMouseUpObserver")
    .event<z13::input::SystemInputEvent>()
    // .with<z13::gameplay::Pause>().not_()
    .each(OnMouseUp);

  world.observer<
      z13::input::KeyboardDownEvent,
      InputListenerQueryComponent,
      z13::input::InputConfig,
      z13::input::InputState>("gameplay_input_system::OnKeyboardDownObserver")
    .event<z13::input::SystemInputEvent>()
    // .with<z13::gameplay::Pause>().not_()
    .each(
      [world](const auto& key_down, const auto& input_listener_query, const auto& input_config, z13::input::InputState& input_state) {
        OnKeyboardDown(world, key_down, input_listener_query, input_config, input_state);
      });

  world.observer<
      z13::input::KeyboardUpEvent,
      InputListenerQueryComponent,
      z13::input::InputConfig,
      z13::input::InputState>("gameplay_input_system::OnKeyboardUpObserver")
    .event<z13::input::SystemInputEvent>()
    // .with<z13::gameplay::Pause>().not_()
    .each(OnKeyboardUp);

  world.observer<z13::status::OnStartupGameEvent>("gameplay_input_system::OnStartupGameEvent")
    .event(flecs::OnAdd)
    .yield_existing()
    .each(OnInputSystemStartupGameEvent);

  InputListenerQueryComponent input_listener_query_component = {
    .listener_query = world
      .query_builder<z13::input::CurrentActionListenerTag, z13::input::ActionListener>("InputListenerQuery")
      .build(),
  };
  world.set(input_listener_query_component);

  world.system<LoadingInputSystemState, status::Loading>("gameplay_input_system::LoadingInput")
    .each(LoadingInput);

  world.system<z13::input::ActionListener>("gameplay_input_system::ClearActionListener")
    .kind<ClearActionFramePhase>()
    .each(ClearActionListener);

  world.system<z13::input::InputState, z13::input::InputConfig, z13::input::ActionListener>("gameplay_input_system::CalculateInputValues")
    .kind<CalculateActionFramePhase>()
    .with<z13::gameplay::Pause>().not_()
    .each(CalculateInputValues);

  world.system<z13::input::ActionListener, geometry::Transform>("gameplay_input_system::ApplyActionListener")
    .kind<ApplyActionFramePhase>()
    .with<z13::gameplay::Pause>().not_()
    .each(ApplyActionListener);
}

}  // namespace z13::gameplay::input
