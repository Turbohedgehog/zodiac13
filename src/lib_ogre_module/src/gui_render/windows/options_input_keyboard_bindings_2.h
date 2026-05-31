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

#include <tuple>
#include <optional>

#include "window_settings_base.h"

namespace z13::ogre::gui {

class KeyBindingWindow2 : public InputSettingsWindowBase {
  struct CurrentKeyBindingInfo {
    // CurrentKeyBindingInfo() {
    //   volatile int zzz = 0;
    // }
    ~CurrentKeyBindingInfo() {
      volatile int zzz = 0;
    } 
    std::string_view group_name;
    std::string_view action_name;
    z13::input::ActionInfo::IdType action_id {};
    z13::fbs::input::Keycode key_code {};
    std::optional<z13::fbs::input::Keycode> event_key_code {};
  };

 public:
  struct ActionBinding {
    z13::input::ActionInfo action;
    std::vector<z13::fbs::input::Keycode> keycodes;
  };
  
  using ActionBindingMap = std::map<std::string_view, std::vector<ActionBinding>>;
  using KeyCodeToTextMap = std::unordered_map<z13::fbs::input::Keycode, std::string>;

  KeyBindingWindow2(flecs::world world);

  void OnBackEvent() override;

  void OnKeyDownEvent(const z13::input::WindowKeyDownEvent& event) override;

 protected:
  void DrawImpl() override;
  
 private:
  void DrawShowState();
  void DrawBindingState();
  void SaveConfig();
  
  ActionBindingMap group_to_actions_;
  KeyCodeToTextMap keycode_to_text_;

  std::optional<CurrentKeyBindingInfo> current_binding_;

  bool is_config_dirty_ {};
};

}  // namespace z13::ogre::gui
