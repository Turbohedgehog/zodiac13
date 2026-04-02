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
