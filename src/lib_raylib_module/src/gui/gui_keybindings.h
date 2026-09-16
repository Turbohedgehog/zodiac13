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

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <flecs.h>

#include <z13/components/input.h>

namespace z13::raylib::gui {

inline constexpr int kKeyBindingSlots = 2;

struct KeyBindingAction {
  std::string display_text;
  z13::input::ActionInfo::IdType action_id {};
  std::array<z13::fbs::input::Keycode, kKeyBindingSlots> keycodes{};
};

struct KeyBindingGroup {
  std::string name;
  std::vector<KeyBindingAction> actions;
};

struct KeyBindingModel {
  std::vector<KeyBindingGroup> groups;
};

struct KeyBindingSlot {
  int group {};
  int action {};
  int slot {};
};

// Snapshots ActionMap + InputConfig.keycode_binding into an editable model.
KeyBindingModel BuildKeyBindingModel(flecs::world world);

// Sets model.groups[slot].keycodes[slot], first clearing that key elsewhere in
// the same group (a group's (group,keycode) pair must stay unique).
void RebindSlot(KeyBindingModel& model, KeyBindingSlot slot, z13::fbs::input::Keycode key);

// Rewrites InputConfig.keycode_binding from the model and fires Save + Updated.
void ApplyKeyBindingModel(flecs::world world, const KeyBindingModel& model);

// Human-readable label for a slot's key ("..." when unbound). Points at static
// storage (the keycode text table or a literal), valid for the program's lifetime.
std::string_view KeyBindingSlotText(const KeyBindingModel& model, int group, int action, int slot);

}  // namespace z13::raylib::gui
