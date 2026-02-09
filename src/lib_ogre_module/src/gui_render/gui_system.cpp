#include "gui_system.h"

#include <flecs.h>

#include <lib_core/components.h>
#include <z13_module/components/z13.h>
#include <z13_module/components/gameplay.h>
#include <z13_module/components/input.h>
#include <ogre_module/ogre_components.h>
#include "windows/window_component.h"
#include "windows/window_factory.h"
#include "windows/window_base.h"

#include <lib_core/log.h>

#include "../ogre_tools/ogre_import/OgreImGuiOverlay.h"

namespace z13::ogre::gui {

struct PreRenderGui { };
struct RenderGui { };
struct PostRenderGui { };

void BeginGui() {
  Ogre::z13::ImGuiOverlay::NewFrame();
}

void EndGui() {
  ImGui::EndFrame();
}

void GuiSystem::Register(flecs::world& world) {
  world.component<PreRenderGui>().add(flecs::Phase).depends_on<PreRender>();
  world.component<RenderGui>().add(flecs::Phase).depends_on<PreRenderGui>();
  world.component<PostRenderGui>().add(flecs::Phase).depends_on<RenderGui>();
  world.component<PostRender>().add(flecs::Phase).depends_on<PostRenderGui>();
  
  // world.get_alive(flecs::OnValidate).add(flecs::Phase).depends_on(update_phase);
  world.component<FinalizeRender>().add(flecs::Phase).depends_on<PostRenderGui>();

  world.component<z13::ogre::gui::WindowComponent>().add(flecs::Singleton);
  world.add<z13::ogre::gui::WindowComponent>();
  // world.component<PauseMenu>().add(flecs::Singleton);
  // world.add<PauseMenu>();

  world.system("BeginGuiSystem")
    .kind<PreRenderGui>()
    .each(BeginGui);

  world.system<z13::ogre::gui::WindowComponent>("GuiSystem::DrawModalWindow")
    .kind<RenderGui>()
    .each([](z13::ogre::gui::WindowComponent& window_component) {
      if (!window_component.modal_window_stack.empty()) {
        window_component.modal_window_stack.top()->Draw();
      }
    });

  world.observer<gameplay::Pause, z13::ogre::gui::WindowComponent>()
    .event(flecs::OnAdd)
    .each([world](const auto&, z13::ogre::gui::WindowComponent& window_component) {
      window_component.modal_window_stack = {};
      window_component.modal_window_stack.emplace(z13::ogre::gui::WindowFactory::CreateGameplayMainMenu(world));
    });

  world.observer<z13::gameplay::WindowBackEvent, z13::ogre::gui::WindowComponent>()
    .event<z13::input::SystemInputEvent>()
    .each([world](const auto&, z13::ogre::gui::WindowComponent& window_component) {
      if (!window_component.modal_window_stack.empty()) {
        window_component.modal_window_stack.top()->OnBackEvent();
      }
    });

  world.system("EndGuiSystem")
    .kind<PostRenderGui>()
    .each(EndGui);
}

}  // namespace z13::ogre::gui
