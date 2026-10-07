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

#include <optional>

#include <flecs.h>

#include <z13/components/input.h>

#include "gui_keybindings.h"
#include "gui_windows.h"

namespace z13::raylib::gui {

class KeyBindingsWindow : public Window {
 public:
  explicit KeyBindingsWindow(flecs::world world);

  StackRequest OnBack() override;
  // Fired at merge time (after DrawBody). Ignore the click that opened the rebind
  // and any mouse button; the first real key wins.
  void OnKeyDown(z13::fbs::input::Keycode key_code) override;

 protected:
  void DrawBody() override;

 private:
  void DrawGroup(int group, const KeyBindingGroup& binding_group);

  KeyBindingModel model_;
  std::optional<KeyBindingSlot> rebind_;
  int selected_group_ {};
  bool restore_tab_ {};
  // Frames left before OnKeyDown accepts a key for the new binding.
  //
  // WindowKeyDownEvent is published synchronously from InputPublisher::ReadInput
  // (ReadEvents), which runs before GuiSystem::BeginFrame forwards this frame's
  // events to ImGui -- so OnKeyDown sees a frame's key/click before DrawBody()
  // (PostRender) turns it into an ImGui::Button() press and sets rebind_. That
  // ordering already makes the opening click harmless: rebind_ is still empty
  // when OnKeyDown runs for it.
  //
  // The real hazard is the *next* frame: if the slot was opened via keyboard nav
  // (Enter/Space activating the focused button) and the key is still held, SDL
  // key-repeat can deliver another KEY_DOWN for it before the user releases it.
  // By then rebind_ is set, so without this guard that repeat would sail through
  // and rebind the slot to Enter/Space itself. Set to 1 when rebind_ starts,
  // decremented once per DrawBody() call, this swallows exactly that one
  // trailing frame. (Mouse clicks don't need this -- IsMouseButtonKeycode
  // already filters them out regardless of arm_countdown_.)
  int arm_countdown_ {};
  bool dirty_ {};
};

WindowPtr MakeKeyBindings(flecs::world world);

}  // namespace z13::raylib::gui
