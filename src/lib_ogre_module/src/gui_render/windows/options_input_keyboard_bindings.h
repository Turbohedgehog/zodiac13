#pragma once

#include "window_settings_base.h"

#include <input_config.pb.h>

// #include <vector>
// #include <tuple>
// #include <memory>

#include <array>
#include <map>

// #include <boost/bimap.hpp>
// #include <boost/bimap/set_of.hpp>
// #include <boost/bimap/multiset_of.hpp>

namespace z13::ogre::gui {

class ButtonBase;
using ButtonPtr = std::shared_ptr<ButtonBase>;
using KeycodeToTextType = std::map<z13::proto::input::Keyboard::Code, std::tuple<std::string, std::string>>;
// using BindingMap = boost::bimap

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
  std::map<z13::proto::input::Keyboard::Code, z13::proto::input::Action::ActionType> key_bindings_;
  std::map<z13::proto::input::Action::ActionType, std::array<z13::proto::input::Keyboard::Code, kMaxKeysPerAction>> current_bindings_;
  std::vector<std::tuple<z13::proto::input::Action::ActionType, std::string>> action_to_name_;
};

}  // namespace z13::ogre::gui
