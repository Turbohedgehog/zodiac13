#include "gameplay_main_menu.h"

#include <imgui.h>

#include <z13_module/components/gameplay.h>
#include <ogre_module/ogre_components.h>

#include "window_component.h"
#include "window_factory.h"

namespace z13::ogre::gui {

GameplayMainMenuWindow::GameplayMainMenuWindow(flecs::world world)
  : WindowBase(world, "Main menu") {}


void GameplayMainMenuWindow::DrawImpl() {
  auto world = GetWorld();
  if (ImGui::Button("Back", ImVec2(120, 0))) {
    OnBackEvent();
  }

  if (ImGui::Button("Settings...", ImVec2(120, 0))) {
    auto& window_component = world.ensure<z13::ogre::gui::WindowComponent>();
    window_component.modal_window_stack.emplace(WindowFactory::CreateInputSettingsMenu(world));
  }

  if (ImGui::Button("Exit", ImVec2(120, 0))) {
    world.add<OgreWindowClosed>();
  }
}

void GameplayMainMenuWindow::OnBackEvent() {
  GetWorld().remove_all<gameplay::Pause>();
  WindowBase::OnBackEvent();
}

}  // namespace z13::ogre::gui
