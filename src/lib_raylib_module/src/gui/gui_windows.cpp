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

#include "gui_windows.h"

#include <optional>
#include <string>
#include <utility>

#include <imgui.h>

#include <lib_core/components.h>

#include <z13/components/gameplay.h>

#include <raylib_module/raylib_components.h>

#include "gui_keybindings.h"

namespace z13::raylib::gui {

namespace {

constexpr ImVec2 kButtonSize{200.f, 0.f};
constexpr float kBindingLabelWidth = 160.f;
constexpr ImVec2 kBindingSlotSize{110.f, 0.f};

bool IsMouseButtonKeycode(z13::fbs::input::Keycode code) {
  return code >= z13::fbs::input::Keycode::MOUSE_BUTTON_LEFT &&
         code <= z13::fbs::input::Keycode::MOUSE_BUTTON_X2;
}

// ----- Keyboard bindings ---------------------------------------------------

class KeyBindingsWindow : public Window {
 public:
  explicit KeyBindingsWindow(flecs::world world)
      : Window(world, "Keyboard bindings"), model_(BuildKeyBindingModel(world)) {}

  StackRequest OnBack() override {
    if (rebind_.has_value()) {
      rebind_.reset();
      return {};
    }
    if (dirty_) {
      ApplyKeyBindingModel(World(), model_);
      dirty_ = false;
    }
    return {StackOp::Pop, nullptr};
  }

  // Fired at merge time (after DrawBody). Ignore the click that opened the rebind
  // and any mouse button; the first real key wins.
  void OnKeyDown(z13::fbs::input::Keycode key_code) override {
    if (!rebind_.has_value() || arm_countdown_ > 0) {
      return;
    }
    if (key_code == z13::fbs::input::Keycode::KEY_UNKNOWN || IsMouseButtonKeycode(key_code)) {
      return;
    }
    if (key_code != z13::fbs::input::Keycode::KEY_ESCAPE) {
      RebindSlot(model_, *rebind_, key_code);
      dirty_ = true;
    }
    rebind_.reset();
  }

 protected:
  void DrawBody() override {
    if (rebind_.has_value()) {
      if (arm_countdown_ > 0) {
        --arm_countdown_;
      }
      ImGui::TextUnformatted("Press a key to bind, or Esc to cancel");
      return;
    }

    for (int group = 0; group < static_cast<int>(model_.groups.size()); ++group) {
      const KeyBindingGroup& binding_group = model_.groups[group];
      ImGui::SeparatorText(binding_group.name.c_str());
      for (int action = 0; action < static_cast<int>(binding_group.actions.size()); ++action) {
        const KeyBindingAction& binding_action = binding_group.actions[action];
        ImGui::TextUnformatted(binding_action.display_text.c_str());
        ImGui::SameLine(kBindingLabelWidth);
        for (int slot = 0; slot < kKeyBindingSlots; ++slot) {
          if (slot > 0) {
            ImGui::SameLine();
          }
          ImGui::PushID((group * 64 + action) * kKeyBindingSlots + slot);
          if (ImGui::Button(KeyBindingSlotText(model_, group, action, slot), kBindingSlotSize)) {
            rebind_ = KeyBindingSlot{group, action, slot};
            arm_countdown_ = 1;
          }
          ImGui::PopID();
        }
      }
    }

    if (dirty_) {
      ImGui::Separator();
      if (ImGui::Button("Save changes", kButtonSize)) {
        ApplyKeyBindingModel(World(), model_);
        dirty_ = false;
      }
    }
  }

 private:
  KeyBindingModel model_;
  std::optional<KeyBindingSlot> rebind_;
  int arm_countdown_ = 0;
  bool dirty_ = false;
};

// ----- Input settings ----------------------------------------------------

class InputSettingsWindow : public Window {
 public:
  explicit InputSettingsWindow(flecs::world world) : Window(world, "Settings") {}

  StackRequest OnBack() override {
    SaveIfDirty();
    return {StackOp::Pop, nullptr};
  }

 protected:
  void DrawBody() override {
    auto& config = World().ensure<z13::input::InputConfig>();

    if (ImGui::Checkbox("Invert X", &config.invert_x)) dirty_ = true;
    if (ImGui::Checkbox("Invert Y", &config.invert_y)) dirty_ = true;
    if (ImGui::SliderFloat("Sensitivity", &config.mouse_sensitivity, 0.f, 10.f, "%.1f")) {
      dirty_ = true;
    }

    ImGui::Separator();
    if (ImGui::Button("Keyboard bindings...", kButtonSize)) {
      RequestPush(std::make_shared<KeyBindingsWindow>(World()));
    }
    if (ImGui::Button("Back", kButtonSize)) {
      SaveIfDirty();
      RequestPop();
    }
  }

 private:
  void SaveIfDirty() {
    if (!dirty_) return;
    dirty_ = false;
    flecs::world world = World();
    world.modified<z13::input::InputConfig>();
    world.event<z13::input::SystemInputEventType>()
        .id<z13::input::SaveConfigEvent>()
        .entity(world.entity().add<z13::input::SaveConfigEvent>())
        .enqueue();
  }

  bool dirty_ = false;
};

// ----- Main menu -------------------------------------------------------

class MainMenuWindow : public Window {
 public:
  explicit MainMenuWindow(flecs::world world) : Window(world, "Main menu") {}

  StackRequest OnBack() override { return {StackOp::CloseMenu, nullptr}; }

 protected:
  void DrawBody() override {
    if (ImGui::Button("Resume", kButtonSize)) RequestCloseMenu();
    if (ImGui::Button("Settings...", kButtonSize)) {
      RequestPush(std::make_shared<InputSettingsWindow>(World()));
    }
    if (ImGui::Button("Exit", kButtonSize)) {
      World().add<RaylibWindowClosed>();
    }
  }
};

}  // namespace

Window::Window(flecs::world world, std::string title)
    : world_(world), title_(std::move(title)) {}

Window::StackRequest Window::Draw() {
  next_ = {};

  const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
  ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
  const ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
                                 ImGuiWindowFlags_AlwaysAutoResize;
  if (ImGui::Begin(title_.c_str(), nullptr, flags)) {
    DrawBody();
  }
  ImGui::End();
  return next_;
}

Window::StackRequest Window::OnBack() { return {StackOp::Pop, nullptr}; }

WindowPtr MakeMainMenu(flecs::world world) {
  return std::make_shared<MainMenuWindow>(world);
}

WindowPtr MakeInputSettings(flecs::world world) {
  return std::make_shared<InputSettingsWindow>(world);
}

WindowPtr MakeKeyBindings(flecs::world world) {
  return std::make_shared<KeyBindingsWindow>(world);
}

}  // namespace z13::raylib::gui
