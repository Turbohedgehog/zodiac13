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

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>
#include <span>
#include <map>

#include <boost/multi_index_container.hpp>
#include <boost/multi_index/composite_key.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/identity.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/container/flat_map.hpp>

#include <input_config_generated.h>

namespace z13::input {

struct ClearActionFramePhase {};
struct CalculateActionFramePhase {};
struct ApplyActionFramePhase {};

// PreFrame -> ScheduledCommandsPhase -> ClearActionFramePhase: net_module commits due
// ScheduledCommands into PlayerActionLog before anything reads the log this tick.
struct ScheduledCommandsPhase {};

// Calculate -> RecordActionFramePhase -> RemoteActionFramePhase -> Apply: Record captures
// this tick's fresh local value before Remote can overwrite ActionListener with a
// delayed/log-driven one.
struct RecordActionFramePhase {};
struct RemoteActionFramePhase {};

namespace bmi = boost::multi_index;

struct SystemInputEventType {};

struct MousePos {
  int x;
  int y;

  static constinit MousePos kZero;
};

inline constinit MousePos MousePos::kZero = {0, 0};

struct MouseMoveEvent {
  MousePos delta;
};

struct MouseButtonEvent {
  MousePos pos;
  z13::fbs::input::Keycode button;
  uint8_t clicks;
};

struct MouseButtonDownEvent : public MouseButtonEvent {
};

struct MouseButtonUpEvent : MouseButtonEvent {
};

struct Keycode {
  z13::fbs::input::Keycode code;
  int32_t raw_code;
  int8_t mod;
  uint8_t repeat;
};

struct KeyboardEvent {
  Keycode keycode;
};

struct KeyboardDownEvent : KeyboardEvent {
};

struct KeyboardUpEvent : KeyboardEvent {
};

struct SaveConfigEvent {};
struct LoadConfigEvent {};
struct SetDefaultConfigEvent {};
struct AppendInputSchema {};
struct OnConfigUpdatedEvent {};

struct ActionBinding {
  z13::fbs::actions::Action action;
  std::vector<z13::fbs::input::Keycode> keys;
};

struct FlatbufferBinarySchema {
  std::span<const uint8_t> binary_schema;
};

struct ActionInfo {
  using IdType = size_t;
  using EnumValueType = int64_t;

  const std::string_view enum_name;
  const std::string_view value_name;
  const std::string_view group_name;
  const std::string_view display_text;
  const std::vector<z13::fbs::input::Keycode> default_keycodes;
  const EnumValueType enum_value {};
  const IdType id {};
};

struct ActionMap {
  using Singleton = void;
  struct ActionNameTag;
  struct EnumNameTag;
  struct EnumActionNameTag;
  struct GroupNameTag;
  struct IdTag;
  struct EnumValueTag;
  struct EnumNameEnumValueTag;

  using ActionMapContainer = bmi::multi_index_container<
    ActionInfo,
    bmi::indexed_by<
      bmi::sequenced<>,
      bmi::ordered_unique<
        bmi::tag<EnumActionNameTag>,
        bmi::composite_key<
          ActionInfo,
          bmi::member<ActionInfo, decltype(ActionInfo::enum_name), &ActionInfo::enum_name>,
          bmi::member<ActionInfo, decltype(ActionInfo::value_name), &ActionInfo::value_name>
        >
      >,
      bmi::ordered_non_unique<
        bmi::tag<ActionNameTag>,
        bmi::member<ActionInfo, decltype(ActionInfo::value_name), &ActionInfo::value_name>
      >,
      bmi::ordered_non_unique<
        bmi::tag<EnumNameTag>,
        bmi::member<ActionInfo, decltype(ActionInfo::enum_name), &ActionInfo::enum_name>
      >,
      bmi::ordered_non_unique<
        bmi::tag<GroupNameTag>,
        bmi::member<ActionInfo, decltype(ActionInfo::group_name), &ActionInfo::group_name>
      >,
      bmi::ordered_unique<
        bmi::tag<IdTag>,
        bmi::member<ActionInfo, decltype(ActionInfo::id), &ActionInfo::id>
      >,
      bmi::ordered_non_unique<
        bmi::tag<EnumValueTag>,
        bmi::member<ActionInfo, decltype(ActionInfo::enum_value), &ActionInfo::enum_value>
      >,
      bmi::ordered_unique<
        bmi::tag<EnumNameEnumValueTag>,
        bmi::composite_key<
          ActionInfo,
          bmi::member<ActionInfo, decltype(ActionInfo::enum_name), &ActionInfo::enum_name>,
          bmi::member<ActionInfo, decltype(ActionInfo::enum_value), &ActionInfo::enum_value>
        >
      >
    >
  >;

  ActionMapContainer action_map;
};

struct KeyCodeAction {
  const z13::fbs::input::Keycode keycode = z13::fbs::input::Keycode::KEY_UNKNOWN;
  const std::string_view action_group;
  const ActionInfo::IdType action_id {};
};

struct InputConfig {
  using Singleton = void;
  struct KeycodeIdTag;
  struct ActionGroupKeycodeIdTag;
  struct ActionIdTag;

  using KeyBindingType = bmi::multi_index_container<
    KeyCodeAction,
    bmi::indexed_by<
      // bmi::sequenced<>,
      bmi::ordered_non_unique<
        bmi::tag<KeycodeIdTag>,
        bmi::member<KeyCodeAction, decltype(KeyCodeAction::keycode), &KeyCodeAction::keycode>
      >,
      bmi::ordered_unique<
        bmi::tag<ActionGroupKeycodeIdTag>,
        bmi::composite_key<
          KeyCodeAction,
          bmi::member<KeyCodeAction, decltype(KeyCodeAction::action_group), &KeyCodeAction::action_group>,
          bmi::member<KeyCodeAction, decltype(KeyCodeAction::keycode), &KeyCodeAction::keycode>
        >
      >,
      bmi::ordered_non_unique<
        bmi::tag<ActionIdTag>,
        bmi::member<KeyCodeAction, decltype(KeyCodeAction::action_id), &KeyCodeAction::action_id>
      >
    >
  >;
  KeyBindingType keycode_binding;

  std::vector<ActionBinding> action_bindings;
  float mouse_sensitivity = 5.f;
  bool invert_x {};
  bool invert_y {};
};

// Lets a caller (e.g. a headless test) fully isolate a world from the
// developer's real on-disk input config (both loading and saving it).
struct InputConfigPersistenceSettings {
  using Singleton = void;
  bool load_config_from_file {true};
};

struct InputState {
  using Singleton = void;
  std::array<float, static_cast<size_t>(z13::fbs::input::Keycode::MAX) + 1> input_state = {};

  // This frame's accumulated mouse-look delta; folded into action_values and
  // reset every frame in CalculateInputValues.
  float mouse_yaw_delta_deg {};
  float mouse_pitch_delta_deg {};
};

struct InputListener {};

struct CurrentActionListenerTag {};

struct ActionValueHolder {
  static constexpr float kInputValueEps = 0.001f;
  static constexpr float kSwithValue = 1.f;

  bool IsSwitchedOn() const {
    return current_value - prev_value >= kSwithValue - kInputValueEps;
  }

  bool IsSwitchedOff() const {
    return current_value - prev_value <= kInputValueEps - kSwithValue;
  }

  void IterateToNextState() {
    prev_value = current_value;
    current_value = {};
  }

  ActionValueHolder& operator+= (float value) {
    current_value += value;
    return *this;
  }

  ActionValueHolder& operator-= (float value) {
    current_value -= value;
    return *this;
  }

  float& operator* () {
    return current_value;
  }

  const float& operator* () const {
    return current_value;
  }

  bool HasBeenChanged() const {
    return std::abs(current_value - prev_value) >= kInputValueEps;
  }

  float current_value {};
  float prev_value {};
};

struct ActionListener {
  // action_values is filled by the clear phase at the end of the frame, so a listener
  // added earlier in one has nothing in it yet -- readers must tolerate that.
  std::optional<ActionValueHolder> Value(ActionInfo::IdType action_id) const {
    const auto value = action_values.find(action_id);
    return value == action_values.end() ? std::nullopt : std::optional(value->second);
  }

  std::vector<std::string> action_group_priority;
  boost::container::flat_map<ActionInfo::IdType, ActionValueHolder> action_values;
};

// Default action group every local player listens to (movement, look).
// todo: убрать константу и брать из z13.fbs.actions.Action.action_group
constexpr std::string_view kControlActionGroup = "Control";

struct WindowBackEvent {};

struct WindowKeyDownEvent {
  z13::fbs::input::Keycode key_code = z13::fbs::input::Keycode::KEY_UNKNOWN;
};

}  // namespace z13::input
