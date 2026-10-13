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

#include <string_view>
#include <vector>

#include <boost/multi_index/composite_key.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index_container.hpp>

#include <input_config_generated.h>
#include <z13/components/action_id.h>

namespace z13::input {

namespace bmi = boost::multi_index;

struct ActionBinding {
  z13::fbs::actions::Action action;
  std::vector<z13::fbs::input::Keycode> keys;
};

struct KeyCodeAction {
  const z13::fbs::input::Keycode keycode = z13::fbs::input::Keycode::KEY_UNKNOWN;
  const std::string_view action_group;
  const ActionId action_id {};
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

}  // namespace z13::input
