#include "options_input_keyboard_bindings.h"

#include <algorithm>
#include <iterator>

#include <imgui.h>

#include "flatbuffers/reflection.h"

#include <lib_core/log.h>

namespace z13::ogre::gui {

class ButtonBase{};

class KeyboardButton : public ButtonBase {};

class MouseButton : public ButtonBase {};

auto ExtractKeycodeToText() {
  KeycodeToTextType res;

  const auto* input_config_schema = reflection::GetSchema(z13::fbs::input::InputConfigBinarySchema::data());
  const auto* enums = input_config_schema->enums();
  // к сожалению здесь приходится пользоваться магическими константами, так как я не нашёл способа
  // получить имя enum из сгенерированного кода, а нейронные сети водят меня по кругу, давая неверные результаты
  const auto key_reflection = enums->LookupByKey("z13.fbs.input.Keycode");

  const auto& keys = z13::fbs::input::EnumValuesKeycode();
  for (size_t i = 0; i < std::size(keys); ++i) {
    const auto key = keys[i];
    const auto* val_reflection = key_reflection->values()->LookupByKey(static_cast<int64_t>(key));
    const auto* attributes = val_reflection->attributes();
    std::string display_text_str;
    if (attributes) {
      const auto* display_text = attributes->LookupByKey("display_text");
      if (display_text) {
        display_text_str = display_text->value()->str();
      }
    }

    const static auto* enum_names_key = z13::fbs::input::EnumNamesKeycode();
    res[key] = std::make_tuple(
      enum_names_key[i],
      display_text_str
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
    ImGui::Text("%s", action_str.c_str());

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
  const auto& kActions = z13::fbs::input::EnumValuesAction();
  action_to_name_.reserve(std::size(kActions));
  const auto* input_config_schema = reflection::GetSchema(z13::fbs::input::InputConfigBinarySchema::data());
  const auto* enums = input_config_schema->enums();
  const auto action_reflection = enums->LookupByKey("z13.fbs.input.Keycode");
  const auto* action_values = action_reflection->values();
  for (size_t i = 0; i < std::size(kActions); ++i) {
    const auto action = kActions[i];
    const auto val_reflection = action_values->LookupByKey(static_cast<int64_t>(action));
    const auto* attributes = val_reflection->attributes();
    if (attributes) {
      const auto* display_text = attributes->LookupByKey("display_text");
      if (display_text) {
        std::string display_text_str = display_text->value()->str();
        action_to_name_.push_back(std::make_tuple(action, display_text_str));
      }
    }
  }
}

void KeyBindingWindow::FillActionBindings() {
  key_bindings_.clear();
  current_bindings_.clear();
  if (KeyBindingWindow::kMaxKeysPerAction == 0) {
    return;
  }

  const auto& kActions = z13::fbs::input::EnumValuesAction();
  for (size_t a = 0; a < std::size(kActions); ++a) {
    auto action = kActions[a];
    current_bindings_.insert({action, {z13::fbs::input::Keycode::KEY_UNKNOWN}});
  }

  const auto& input_config = GetInputConfig();
  for (const auto& action_binding : input_config.action_bindings) {
    auto it = current_bindings_.find(action_binding.action);
    if (it == current_bindings_.end()) {
      it = current_bindings_.insert({action_binding.action, {z13::fbs::input::Keycode::KEY_UNKNOWN}}).first;
    }

    auto& key_array = it->second;
    for (const auto& key : action_binding.keys) {
      key_bindings_[key] = action_binding.action;
      if (*key_array.begin() != z13::fbs::input::Keycode::KEY_UNKNOWN) {
        std::shift_right(key_array.begin(), key_array.end(), 1);  
      }

      *key_array.begin() = key;
    }
  }
}

}  // namespace z13::ogre::gui
