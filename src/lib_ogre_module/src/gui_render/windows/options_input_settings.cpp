#include "options_input_settings.h"

#include <imgui.h>

#include "window_component.h"
#include "window_factory.h"

namespace z13::ogre::gui {

InputSettingsWindow::InputSettingsWindow(flecs::world world)
  : InputSettingsWindowBase(world, "Settings") {
}

void InputSettingsWindow::DrawImpl() {
  bool is_dirty = false;
  auto& input_config = GetInputConfig();
  is_dirty |= ImGui::Checkbox("Invert X", &input_config.invert_x);
  is_dirty |= ImGui::Checkbox("Invert Y", &input_config.invert_y);
  is_dirty |= ImGui::SliderFloat("Mouse sensitivity", &input_config.mouse_sensitivity, 0.0f, 10.0f, "%.1f");
  if (is_dirty) {
    SetDirty();
  }

  if (ImGui::Button("Keyboard bindings...", ImVec2(200, 0))) {
    auto world = GetWorld();
    auto& window_component = world.ensure<z13::ogre::gui::WindowComponent>();
    window_component.modal_window_stack.emplace(WindowFactory::CreateKeyboardBindingsMenu(world));
  }

  if (ImGui::Button("Back", ImVec2(200, 0))) {
    OnBackEvent();
  }
}

}  // namespace z13::ogre::gui
