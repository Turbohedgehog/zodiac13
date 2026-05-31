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

#include "options_input_keyboard_bindings_2.h"

#include <algorithm>
#include <iterator>

#include <imgui.h>

#include <input_config_generated.h>

#include "flatbuffers/reflection.h"

#include <lib_core/log.h>

namespace z13::ogre::gui {

static constexpr size_t kMaxKeycodesPerAction = 2;
static constexpr auto kUnknownKeycode = z13::fbs::input::Keycode::KEY_UNKNOWN;

void FillUnknownKeycodesToAction(KeyBindingWindow2::ActionBinding& action_binding) {
  for (size_t i = action_binding.keycodes.size(); i < kMaxKeycodesPerAction; ++i) {
    action_binding.keycodes.push_back(kUnknownKeycode);
  }
}

KeyBindingWindow2::ActionBindingMap CollectActions(
    const z13::input::ActionMap& action_map,
    const z13::input::InputConfig& input_config) {
  KeyBindingWindow2::ActionBindingMap group_to_actions;

  std::unordered_map<z13::input::ActionInfo::IdType, KeyBindingWindow2::ActionBinding> action_bindings;
  const auto& action_id_map = action_map.action_map.get<z13::input::ActionMap::IdTag>();
  for (const auto& action_info : action_id_map) {
    if (action_info.display_text.empty()) {
      continue;
    }
    action_bindings.insert(std::make_pair(action_info.id, KeyBindingWindow2::ActionBinding{.action = action_info}));
  }

  const auto& bindings = input_config.keycode_binding.get<z13::input::InputConfig::ActionIdTag>();
  for (const auto& binding : bindings) {
    auto action_it = action_bindings.find(binding.action_id);
    if (action_it == action_bindings.end()) {
      continue;
    }

    action_it->second.keycodes.push_back(binding.keycode);
  }

  for (const auto& action : action_bindings) {
    group_to_actions[action.second.action.group_name].push_back(action.second);
  }

  for (auto& [_, action_infos] : group_to_actions) {
    for (auto& action_info : action_infos) {
      FillUnknownKeycodesToAction(action_info);
    }
  }

  return group_to_actions;  
}

void KeyBindingWindow2::SaveConfig() {
  auto world = GetWorld();

  auto& input_config = world.ensure<z13::input::InputConfig>();
  input_config.keycode_binding.clear();

  for (const auto& [group_name, action_infos] : group_to_actions_) {
    for (const auto& action_info : action_infos) {
      for (auto keycode : action_info.keycodes) {
        if (keycode == kUnknownKeycode) {
          continue;
        }
        input_config.keycode_binding.emplace(
          z13::input::KeyCodeAction {
            .keycode = keycode,
            .action_group = group_name,
            .action_id = action_info.action.id,
          }
        );
      }
    }
  }

  world.modified<z13::input::InputConfig>();

  world.event<z13::input::SystemInputEventType>()
    .id<z13::input::SaveConfigEvent>()
    .entity(world.entity().add<z13::input::SaveConfigEvent>())
    .enqueue();

  world.event<z13::input::SystemInputEventType>()
    .id<z13::input::OnConfigUpdatedEvent>()
    .entity(world.entity().add<z13::input::OnConfigUpdatedEvent>())
    .enqueue();
}

KeyBindingWindow2::KeyCodeToTextMap GenerateKeyCodeToTextMap() {
  KeyBindingWindow2::KeyCodeToTextMap key_code_to_text;

  const auto* input_config_schema = reflection::GetSchema(z13::fbs::input::InputConfigBinarySchema::data());
  const auto* enums = input_config_schema->enums();
  const auto keycode_reflection = enums->LookupByKey("z13.fbs.input.Keycode");
  const auto* keycode_values = keycode_reflection->values();
  for (const auto& value : *keycode_values) {
    const auto* attributes = value->attributes();
    if (!attributes) {
      continue;
    }

    const auto* display_text = attributes->LookupByKey("display_text");
    if (!display_text) {
      continue;
    }

    key_code_to_text[static_cast<z13::fbs::input::Keycode>(value->value())] = display_text->value()->str();
  }

  return key_code_to_text;
}

KeyBindingWindow2::KeyBindingWindow2(flecs::world world)
  : InputSettingsWindowBase(world, "Keyboard bindings") {
  group_to_actions_ = CollectActions(
    world.ensure<z13::input::ActionMap>(),
    world.ensure<z13::input::InputConfig>());
  keycode_to_text_ = GenerateKeyCodeToTextMap();
}

void KeyBindingWindow2::DrawImpl() {
  if (current_binding_) {
    DrawBindingState();
  } else {
    DrawShowState();
  }
}

void KeyBindingWindow2::DrawShowState() {
  if (!ImGui::BeginTabBar("MyTabBar", ImGuiTabBarFlags_None)) {
    return;
  }

  for (const auto& group : group_to_actions_) {
    if (ImGui::BeginTabItem(group.first.data())) {
      for (const auto& action_binding : group.second) {
        ImGui::Text("%s", action_binding.action.display_text.data());
        ImGui::PushID(action_binding.action.display_text.data());
        for (size_t i = 0; i < action_binding.keycodes.size(); ++i) {
          const auto& keycode = action_binding.keycodes[i];
          auto it = keycode_to_text_.find(keycode);
          auto txt = it == keycode_to_text_.end() ? std::string("...") : it->second;

          ImGui::SameLine();
          ImGui::PushID(i);
          if (ImGui::Button(txt.data())) {
            current_binding_ = CurrentKeyBindingInfo {
              .group_name = group.first,
              .action_name = action_binding.action.display_text,
              .action_id = action_binding.action.id,
              .key_code = keycode,
            };
          }
          ImGui::PopID();
        }
        ImGui::PopID();
      }
      ImGui::EndTabItem();
    }
  }

  ImGui::EndTabBar();

  if (is_config_dirty_) {
    ImGui::Separator();

    if (ImGui::Button("Save changes")) {
      is_config_dirty_ = false;
      SaveConfig();
    }
  }
}

void KeyBindingWindow2::DrawBindingState() {
  ImGui::Text("Press a key to bind action '%s'...", current_binding_.value().action_name.data());
  ImGui::Text("Or press ESC to cancel");
  // Gub: Событие нажатия кнопки срабатывает позже события OnKeyDownEvent
  // Из-за этого удаление биндинга не срабатывает вообще и назначается левая кнопка мыши
  // Поэтому кнопка пока что отключена
  #if 0
  if (ImGui::Button("Or press button to delete key binding")) {
    auto binding = current_binding_.value();
    current_binding_.reset();

    auto group_it = group_to_actions_.find(binding.group_name);
    if (group_it == group_to_actions_.end()) {
      return;
    }

    auto& actions = group_it->second;
    for (auto& action : actions) {
      if (binding.action_id == action.action.id) {
        action.keycodes.erase(
          std::remove(action.keycodes.begin(), action.keycodes.end(), binding.key_code),
          action.keycodes.end());
        
        is_config_dirty_ = true;
        FillUnknownKeycodesToAction(action);

        break;
      }
    }
  }
  #endif

  if (current_binding_ && current_binding_->event_key_code) {
    auto binding = current_binding_.value();
    current_binding_.reset();

    auto group_it = group_to_actions_.find(binding.group_name);
    if (group_it == group_to_actions_.end()) {
      auto& actions = group_it->second;
      for (auto& action : actions) {
        action.keycodes.erase(
          std::remove(action.keycodes.begin(), action.keycodes.end(), *current_binding_->event_key_code),
          action.keycodes.end());

        if (binding.action_id == action.action.id) {
          action.keycodes.erase(
            std::remove(action.keycodes.begin(), action.keycodes.end(), kUnknownKeycode),
            action.keycodes.end());
          if (action.keycodes.size() >= kMaxKeycodesPerAction) {
            action.keycodes.erase(action.keycodes.begin(), action.keycodes.begin() + action.keycodes.size() - kMaxKeycodesPerAction + 1);
          }
          action.keycodes.push_back(*current_binding_->event_key_code);
          is_config_dirty_ = true;
        }

        FillUnknownKeycodesToAction(action);
      }
    }
  }
}

void KeyBindingWindow2::OnBackEvent() {
  if (current_binding_) {
    current_binding_.reset();
  } else {
    InputSettingsWindowBase::OnBackEvent();
  }
}

void KeyBindingWindow2::OnKeyDownEvent(const z13::input::WindowKeyDownEvent& event) {
  InputSettingsWindowBase::OnKeyDownEvent(event);

#if 1
  if (current_binding_) {
    current_binding_->event_key_code = event.key_code;
  }
#else
  if (!current_binding_) {
    return;
  }

  LOG_INFO("KeyBindingWindow2::OnKeyDownEvent");

  auto binding = current_binding_.value();
  current_binding_.reset();

  auto group_it = group_to_actions_.find(binding.group_name);
  if (group_it == group_to_actions_.end()) {
    return;
  }

  auto& actions = group_it->second;
  for (auto& action : actions) {
    action.keycodes.erase(
      std::remove(action.keycodes.begin(), action.keycodes.end(), event.key_code),
      action.keycodes.end());

    if (binding.action_id == action.action.id) {
      action.keycodes.erase(
        std::remove(action.keycodes.begin(), action.keycodes.end(), kUnknownKeycode),
        action.keycodes.end());
      if (action.keycodes.size() >= kMaxKeycodesPerAction) {
        action.keycodes.erase(action.keycodes.begin(), action.keycodes.begin() + action.keycodes.size() - kMaxKeycodesPerAction + 1);
      }
      action.keycodes.push_back(event.key_code);
      is_config_dirty_ = true;
    }

    FillUnknownKeycodesToAction(action);
  }
#endif
}

}  // namespace z13::ogre::gui
