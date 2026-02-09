#include "options_input_keyboard_bindings.h"

#include <algorithm>

#include <imgui.h>

#include <lib_core/log.h>

namespace z13::ogre::gui {

class ButtonBase{};

class KeyboardButton : public ButtonBase {};

class MouseButton : public ButtonBase {};

auto ExtractKeycodeToText() {
  KeycodeToTextType res;

  const auto* keyboard_code_descriptor = z13::proto::input::Keyboard_Code_descriptor();
  for (
    auto i = z13::proto::input::Keyboard_Code_Code_MIN;
    i <= z13::proto::input::Keyboard_Code_Code_MAX;
    i = static_cast<z13::proto::input::Keyboard_Code>(static_cast<uint32_t>(i) + 1)) {
    auto idx = static_cast<int>(i);
    const auto* value_descriptor = keyboard_code_descriptor->value(i);
    const auto& value_options = value_descriptor->options();
    std::string display_text;
    if (value_options.HasExtension(z13::proto::input::details)) {
      display_text = value_options.GetExtension(z13::proto::input::details).display_text();
    }

    res[i] = std::make_tuple(
      z13::proto::input::Keyboard_Code_Name(i),
      value_options.GetExtension(z13::proto::input::details).display_text()
    );
  }

  return res;
}

const KeycodeToTextType KeyBindingWindow::kKeycodeToText = ExtractKeycodeToText();

KeyBindingWindow::KeyBindingWindow(flecs::world world)
  : InputSettingsWindowBase(world, "Keyboard bindings") {
  FillActionToNameList();
  FillActionBindings();
}

void KeyBindingWindow::DrawImpl() {
  static const std::string kNoneKey = "..."; 
  // Todo: ОПТИМИЗИРОВАТЬ!!!!
  for (const auto& [action, action_str] : action_to_name_) {
    ImGui::Text(action_str.c_str());

    auto it = current_bindings_.find(action);
    if (it == current_bindings_.end()) {
      continue;
    }

    ImGui::PushID(action_str.c_str());

    const auto& action_bindings = it->second;
    for (size_t i = 0; i < action_bindings.size(); ++i) {
      std::reference_wrapper<const std::string> key_binding = kNoneKey;
      auto keycode = action_bindings[i];
      auto binding_it = KeyBindingWindow::kKeycodeToText.find(keycode);
      if (binding_it != KeyBindingWindow::kKeycodeToText.end()) {
        const auto& key_info = binding_it->second;
        key_binding = std::get<1>(key_info);
        if (key_binding.get().empty()) {
          key_binding = std::get<0>(key_info);
        }
      }
      ImGui::PushID(static_cast<int>(i));
      ImGui::SameLine();
      if (ImGui::Button(key_binding.get().c_str())) {
        LOG_INFO("Keyboard binding is not ready yet");
      }
      ImGui::PopID();
    }

    ImGui::PopID();
  }

  if (ImGui::Button("Back", ImVec2(120, 0))) {
    OnBackEvent();
  }
}

void KeyBindingWindow::OnBackEvent() {
  InputSettingsWindowBase::OnBackEvent();
}

void KeyBindingWindow::FillActionToNameList() {
  action_to_name_.clear();
  action_to_name_.reserve(z13::proto::input::Action_ActionType_ActionType_ARRAYSIZE);
  const auto* action_type_descriptor = z13::proto::input::Action_ActionType_descriptor();
  for (
      auto i = z13::proto::input::Action_ActionType_ActionType_MIN;
      i <= z13::proto::input::Action_ActionType_ActionType_MAX;
      i = static_cast<z13::proto::input::Action::ActionType>(static_cast<uint32_t>(i) + 1)) {
    const auto* value_descriptor = action_type_descriptor->value(i);
    const auto& value_options = value_descriptor->options();
    std::string display_text;
    if (value_options.HasExtension(z13::proto::input::details)) {
      display_text = value_options.GetExtension(z13::proto::input::details).display_text();
    }
    // action_to_name_.push_back({i, z13::proto::input::Action_ActionType_Name(i)});
    action_to_name_.push_back({i, display_text});
  }
}

void KeyBindingWindow::FillActionBindings() {
  key_bindings_.clear();
  current_bindings_.clear();
  if (KeyBindingWindow::kMaxKeysPerAction == 0) {
    return;
  }

  for (
      auto a = z13::proto::input::Action_ActionType_ActionType_MIN;
      a <= z13::proto::input::Action_ActionType_ActionType_MAX;
      a = static_cast<z13::proto::input::Action::ActionType>(static_cast<uint32_t>(a) + 1)) {
    current_bindings_.insert({a, {z13::proto::input::Keyboard::Code::Keyboard_Code_KEY_UNKNOWN}});
  }

  const auto& input_config = GetInputConfig();
  for (const auto& action_binding : input_config.action_bindings) {
    auto it = current_bindings_.find(action_binding.action);
    if (it == current_bindings_.end()) {
      it = current_bindings_.insert({action_binding.action, {z13::proto::input::Keyboard::Code::Keyboard_Code_KEY_UNKNOWN}}).first;
    }

    auto& key_array = it->second;
    for (const auto& key : action_binding.keys) {
      key_bindings_[key] = action_binding.action;
      if (*key_array.begin() != z13::proto::input::Keyboard::Code::Keyboard_Code_KEY_UNKNOWN) {
        std::shift_right(key_array.begin(), key_array.end(), 1);  
      }

      *key_array.begin() = key;
    }
  }
}

}  // namespace z13::ogre::gui
