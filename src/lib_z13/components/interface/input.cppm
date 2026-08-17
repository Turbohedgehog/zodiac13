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

#include <boost/multi_index_container.hpp>
#include <boost/multi_index/composite_key.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/identity.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/container/flat_map.hpp>

#include <input_config_generated.h>

export module z13.input;

export import <array>;
export import <vector>;
export import <span>;
export import <map>;

export namespace z13::input {

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

  // boost::multi_index требует, чтобы элемент был EqualityComparable (копирующий
  // конструктор контейнера использует operator!= итераторов, построенный из operator==).
  // Без него компиляция из другого модуля падает с C2678 на boost/operators.hpp.
  bool operator==(const ActionInfo&) const = default;
};

struct ActionMap {
  struct ActionNameTag;
  struct EnumNameTag;
  struct EnumActionNameTag;
  struct GroupNameTag;
  struct IdTag;
  struct EnumValueTag;
  struct EnumNameEnumValueTag;

  // Конструктор копирования определён неинлайново в implementation unit
  // (input.cpp), чтобы boost::multi_index::copy_construct_from инстанцировался
  // в том же TU, где подключены заголовки boost через global module fragment.
  // Иначе из-за ограничений ADL C++20 modules в MSVC операторы ==/!= для
  // bidir_node_iterator не находятся и возникает ошибка C2678 в boost/operators.hpp.
  ActionMap() = default;
  ActionMap(const ActionMap& other);
  ActionMap(ActionMap&&) noexcept = default;
  // Операторы присваивания оставлены неявно удалёнными: ActionInfo содержит
  // const-члены, поэтому контейнер (и, как следствие, ActionMap) не является
  // copy/move assignable.

  using ActionMapContainer = bmi::multi_index_container<
    ActionInfo,
    bmi::indexed_by<
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
  struct KeycodeIdTag;
  struct ActionGroupKeycodeIdTag;
  struct ActionIdTag;

  // Аналогично ActionMap: конструктор копирования определён неинлайново в
  // input.cpp, чтобы boost::multi_index::copy_construct_from для keycode_binding
  // инстанцировался в TU с видимыми boost-заголовками (обход ограничения ADL
  // в C++20 modules / MSVC, ошибка C2678 в boost/operators.hpp).
  InputConfig() = default;
  InputConfig(const InputConfig& other);
  InputConfig(InputConfig&&) noexcept = default;

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

struct InputState {
  std::array<float, static_cast<size_t>(z13::fbs::input::Keycode::MAX) + 1> input_state = {};
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
  std::vector<std::string> action_group_priority;
  boost::container::flat_map<ActionInfo::IdType, ActionValueHolder> action_values;
};

struct WindowBackEvent {};

struct WindowKeyDownEvent {
  z13::fbs::input::Keycode key_code = z13::fbs::input::Keycode::KEY_UNKNOWN;
};

}  // namespace z13::input
