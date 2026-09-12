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

#include "gui_keybindings.h"

#include <algorithm>
#include <unordered_map>

#include <flatbuffers/reflection.h>

#include <input_config_generated.h>

namespace z13::raylib::gui {

namespace {

constexpr auto kUnknown = z13::fbs::input::Keycode::KEY_UNKNOWN;
constexpr const char* kUnboundText = "...";

// display_text attribute of every Keycode enum value, via the fbs schema.
const std::unordered_map<z13::fbs::input::Keycode, std::string>& KeycodeTextMap() {
  static const std::unordered_map<z13::fbs::input::Keycode, std::string> map = [] {
    std::unordered_map<z13::fbs::input::Keycode, std::string> result;
    const auto* schema = reflection::GetSchema(z13::fbs::input::InputConfigBinarySchema::data());
    if (schema == nullptr) {
      return result;
    }
    const auto* keycode_enum = schema->enums()->LookupByKey("z13.fbs.input.Keycode");
    if (keycode_enum == nullptr) {
      return result;
    }
    for (const auto* value : *keycode_enum->values()) {
      const auto* attributes = value->attributes();
      if (attributes == nullptr) {
        continue;
      }
      if (const auto* display_text = attributes->LookupByKey("display_text")) {
        result[static_cast<z13::fbs::input::Keycode>(value->value())] =
            display_text->value()->str();
      }
    }
    return result;
  }();
  return map;
}

}  // namespace

KeyBindingModel BuildKeyBindingModel(flecs::world world) {
  KeyBindingModel model;

  const auto& action_map = world.ensure<z13::input::ActionMap>().action_map;
  const auto& config = world.ensure<z13::input::InputConfig>();

  std::unordered_map<std::string, int> group_index;
  std::unordered_map<z13::input::ActionInfo::IdType, std::pair<int, int>> action_slot;

  const auto& actions_by_id = action_map.get<z13::input::ActionMap::IdTag>();
  for (const auto& action_info : actions_by_id) {
    if (action_info.display_text.empty()) {
      continue;
    }
    const std::string group_name(action_info.group_name);
    auto [group_it, inserted] = group_index.try_emplace(group_name, static_cast<int>(model.groups.size()));
    if (inserted) {
      model.groups.push_back(KeyBindingGroup{.name = group_name});
    }
    KeyBindingGroup& group = model.groups[group_it->second];

    KeyBindingAction action{};
    action.display_text = std::string(action_info.display_text);
    action.action_id = action_info.id;
    action.keycodes.fill(kUnknown);
    action_slot[action_info.id] = {group_it->second, static_cast<int>(group.actions.size())};
    group.actions.push_back(std::move(action));
  }

  const auto& bindings_by_action = config.keycode_binding.get<z13::input::InputConfig::ActionIdTag>();
  for (const auto& binding : bindings_by_action) {
    const auto slot_it = action_slot.find(binding.action_id);
    if (slot_it == action_slot.end()) {
      continue;
    }
    KeyBindingAction& action = model.groups[slot_it->second.first].actions[slot_it->second.second];
    for (auto& keycode : action.keycodes) {
      if (keycode == kUnknown) {
        keycode = binding.keycode;
        break;
      }
    }
  }

  return model;
}

void RebindSlot(KeyBindingModel& model, KeyBindingSlot slot, z13::fbs::input::Keycode key) {
  if (slot.group < 0 || slot.group >= static_cast<int>(model.groups.size())) {
    return;
  }
  KeyBindingGroup& group = model.groups[slot.group];
  if (slot.action < 0 || slot.action >= static_cast<int>(group.actions.size())) {
    return;
  }

  // A (group, keycode) pair must be unique -> clear this key wherever it sits in
  // the group before assigning it.
  for (KeyBindingAction& action : group.actions) {
    for (auto& keycode : action.keycodes) {
      if (keycode == key) {
        keycode = kUnknown;
      }
    }
  }
  group.actions[slot.action].keycodes[slot.slot] = key;
}

void ApplyKeyBindingModel(flecs::world world, const KeyBindingModel& model) {
  auto& config = world.ensure<z13::input::InputConfig>();
  const auto& actions_by_id = world.ensure<z13::input::ActionMap>().action_map.get<
      z13::input::ActionMap::IdTag>();

  config.keycode_binding.clear();
  for (const KeyBindingGroup& group : model.groups) {
    for (const KeyBindingAction& action : group.actions) {
      const auto action_it = actions_by_id.find(action.action_id);
      if (action_it == actions_by_id.end()) {
        continue;
      }
      for (const auto keycode : action.keycodes) {
        if (keycode == kUnknown) {
          continue;
        }
        config.keycode_binding.emplace(z13::input::KeyCodeAction{
            .keycode = keycode,
            .action_group = action_it->group_name,  // string_view into the fbs schema
            .action_id = action.action_id,
        });
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

const char* KeyBindingSlotText(const KeyBindingModel& model, int group, int action, int slot) {
  if (group < 0 || group >= static_cast<int>(model.groups.size())) {
    return kUnboundText;
  }
  const KeyBindingGroup& binding_group = model.groups[group];
  if (action < 0 || action >= static_cast<int>(binding_group.actions.size())) {
    return kUnboundText;
  }
  const z13::fbs::input::Keycode keycode = binding_group.actions[action].keycodes[slot];
  if (keycode == kUnknown) {
    return kUnboundText;
  }
  const auto& text_map = KeycodeTextMap();
  const auto it = text_map.find(keycode);
  return it != text_map.end() ? it->second.c_str() : kUnboundText;
}

}  // namespace z13::raylib::gui
