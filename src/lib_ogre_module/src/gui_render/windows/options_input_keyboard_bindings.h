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

#include "window_settings_base.h"

#include <input_config_generated.h>

#include <array>
#include <map>

namespace z13::ogre::gui {

class ButtonBase;
using ButtonPtr = std::shared_ptr<ButtonBase>;
using KeycodeToTextType = std::map<z13::fbs::input::Keycode, std::tuple<std::string, std::string>>;

class KeyBindingWindow : public InputSettingsWindowBase {
 public:
  static const KeycodeToTextType kKeycodeToText;
  static constexpr uint32_t kMaxKeysPerAction = 2;

  KeyBindingWindow(flecs::world world);

  void OnBackEvent() override;

 protected:
  void DrawImpl() override;

 private:
  void FillActionBindings();
  void FillActionToNameList();
  std::map<z13::fbs::input::Keycode, z13::fbs::actions::Action> key_bindings_;
  std::map<z13::fbs::actions::Action, std::array<z13::fbs::input::Keycode, kMaxKeysPerAction>> current_bindings_;
  std::vector<std::tuple<z13::fbs::actions::Action, std::string>> action_to_name_;
};

}  // namespace z13::ogre::gui
