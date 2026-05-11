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
struct OnConfigUpdatedEvent {};

struct ActionBinding {
  z13::fbs::actions::Action action;
  // std::string action;
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
  const EnumValueType enum_value = 0;
  const IdType id = 0;
};

struct ActionMap {
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
  const std::string_view display_text;
  const ActionInfo::IdType action_id = 0;
};

struct InputConfig {
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
      bmi::ordered_unique<
        bmi::tag<ActionIdTag>,
        bmi::member<KeyCodeAction, decltype(KeyCodeAction::action_id), &KeyCodeAction::action_id>
      >
    >
  >;
  KeyBindingType keycode_binding;

  std::vector<ActionBinding> action_bindings;
  std::map<z13::fbs::input::Keycode, z13::fbs::actions::Action> code_to_action;
  std::map<z13::fbs::input::Keycode, ActionInfo::IdType> code_to_action_id;
  float mouse_sensitivity = 5.f;
  bool invert_x = false;
  bool invert_y = false;
};

struct InputState {
  std::array<float, static_cast<size_t>(z13::fbs::input::Keycode::MAX) + 1> input_state = {0.f};
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

  void Next() {
    prev_value = current_value;
    current_value = 0.f;
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

  float current_value = 0.f;
  float prev_value = 0.f;
};

struct ActionListener {
  std::vector<std::string> action_group_priority;
  boost::container::flat_map<ActionInfo::IdType, ActionValueHolder> action_values;
};

}  // namespace z13::input
