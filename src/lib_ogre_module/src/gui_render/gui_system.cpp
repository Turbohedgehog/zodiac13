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

#include "gui_system.h"

#include <flecs.h>

#include <lib_core/components.h>
#include <z13/components/z13.h>
#include <z13/components/gameplay.h>
#include <z13/components/input.h>
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

void BeginGui(const z13::ogre::OgreData&) {
  Ogre::z13::ImGuiOverlay::NewFrame();
}

void EndGui(const z13::ogre::OgreData&) {
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

  world.system<z13::ogre::OgreData>("BeginGuiSystem")
    .kind<PreRenderGui>()
    .each(BeginGui);

  world.system<z13::ogre::gui::WindowComponent, z13::ogre::OgreData>("GuiSystem::DrawModalWindow")
    .kind<RenderGui>()
    .each([](z13::ogre::gui::WindowComponent& window_component, const z13::ogre::OgreData&) {
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

  world.observer<z13::input::WindowBackEvent, z13::ogre::gui::WindowComponent>()
    .event<z13::input::SystemInputEventType>()
    .each([world](const auto&, z13::ogre::gui::WindowComponent& window_component) {
      if (!window_component.modal_window_stack.empty()) {
        window_component.modal_window_stack.top()->OnBackEvent();
      }
    });

  world.observer<z13::input::WindowKeyDownEvent, z13::ogre::gui::WindowComponent>()
    .event<z13::input::SystemInputEventType>()
    .each([world](const auto& key_down, z13::ogre::gui::WindowComponent& window_component) {
      if (!window_component.modal_window_stack.empty()) {
        window_component.modal_window_stack.top()->OnKeyDownEvent(key_down);
      }
    });

  world.system<z13::ogre::OgreData>("EndGuiSystem")
    .kind<PostRenderGui>()
    .each(EndGui);
}

}  // namespace z13::ogre::gui
