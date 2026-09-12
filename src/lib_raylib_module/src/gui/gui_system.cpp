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

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl3.h>
#include <rlgl.h>

#include <lib_core/components.h>
#include <lib_core/log.h>

#include <z13/components/gameplay.h>
#include <z13/components/input.h>

#include <raylib_module/raylib_components.h>

#include "gui_windows.h"
#include "platform/sdl_platform.h"

namespace z13::raylib {

namespace {

constexpr const char* kGlslVersion = "#version 330";

bool g_imgui_ready = false;

void ApplyStackRequest(flecs::world world, gui::WindowStack& stack,
                       const gui::Window::StackRequest& request) {
  switch (request.op) {
    case gui::Window::StackOp::Pop:
      if (!stack.windows.empty()) {
        stack.windows.pop_back();
      }
      break;
    case gui::Window::StackOp::CloseMenu:
      stack.windows.clear();
      world.remove<gameplay::Pause>();
      break;
    case gui::Window::StackOp::Keep:
      break;
  }
  if (request.push) {
    stack.windows.push_back(request.push);
  }
}

void RegisterComponents(flecs::world world) {
  world.component<gui::WindowStack>().add(flecs::Singleton);
}

void CreateDefaults(flecs::world world) { world.set<gui::WindowStack>({}); }

// Runs once RaylibData is set, i.e. after SdlPlatform::Init brought up the window
// / GL context (same trigger EnvironmentRenderSystem uses to load GPU resources).
void InitImGui() {
  if (g_imgui_ready || !SdlPlatform::IsReady()) {
    return;
  }
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGui::GetIO().IniFilename = nullptr;
  ImGui::StyleColorsDark();
  ImGui_ImplSDL3_InitForOpenGL(static_cast<SDL_Window*>(SdlPlatform::Window()),
                               SdlPlatform::GlContext());
  ImGui_ImplOpenGL3_Init(kGlslVersion);
  g_imgui_ready = true;
  LOG_INFO("[gui] Dear ImGui {} initialised (SDL3 + OpenGL3)", IMGUI_VERSION);
}

void BeginImGuiFrame() {
  int event_count = 0;
  const SDL_Event* events = SdlPlatform::FrameEvents(&event_count);
  for (int i = 0; i < event_count; ++i) {
    ImGui_ImplSDL3_ProcessEvent(&events[i]);
  }
  ImGui_ImplOpenGL3_NewFrame();
  ImGui_ImplSDL3_NewFrame();
  ImGui::NewFrame();
}

void EndImGuiFrame() {
  ImGui::Render();
  rlDrawRenderBatchActive();  // flush any pending rlgl geometry under the UI
  ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void RegisterSystems(flecs::world world) {
  // Bring up ImGui once the SDL window / GL context exists (RaylibData is set at
  // the tail of RaylibSystem::CreateDefaults).
  world.observer<const RaylibData>("GuiSystem::InitImGui")
      .event(flecs::OnAdd)
      .yield_existing()
      .each([](const RaylibData&) { InitImGui(); });

  // ReadEvents: forward the frame's SDL events to ImGui and open a new UI frame.
  // Runs after RaylibSystem::PumpEvents (registered first) fills the batch.
  world.system<const RaylibData>("GuiSystem::BeginFrame")
      .kind<ReadEvents>()
      .each([](const RaylibData&) {
        if (g_imgui_ready) {
          BeginImGuiFrame();
        }
      });

  // PostRender: build the window stack on top of the 3D scene, then render the
  // ImGui draw data. Render must be called every frame to match NewFrame.
  world.system<gui::WindowStack>("GuiSystem::Draw")
      .kind<PostRender>()
      .each([world](gui::WindowStack& stack) {
        if (!g_imgui_ready) {
          return;
        }
        const bool paused = world.has<gameplay::Pause>();
        if (paused && stack.windows.empty()) {
          stack.windows.push_back(gui::MakeMainMenu(world));
        } else if (!paused && !stack.windows.empty()) {
          stack.windows.clear();
        }
        if (!stack.windows.empty()) {
          ApplyStackRequest(world, stack, stack.windows.back()->Draw());
        }
        EndImGuiFrame();
      });

  // Esc while paused -> WindowBackEvent; other keys/mouse -> WindowKeyDownEvent
  // (both emitted by z13_module's gameplay_input_system). WindowStack is a
  // singleton so it is sourced from the singleton, not the event entity.
  world.observer<const z13::input::WindowBackEvent, gui::WindowStack>("GuiSystem::OnWindowBack")
      .event<z13::input::SystemInputEventType>()
      .each([world](const z13::input::WindowBackEvent&, gui::WindowStack& stack) {
        if (!stack.windows.empty()) {
          ApplyStackRequest(world, stack, stack.windows.back()->OnBack());
        }
      });

  world.observer<const z13::input::WindowKeyDownEvent, gui::WindowStack>("GuiSystem::OnWindowKey")
      .event<z13::input::SystemInputEventType>()
      .each([](const z13::input::WindowKeyDownEvent& event, gui::WindowStack& stack) {
        if (!stack.windows.empty()) {
          stack.windows.back()->OnKeyDown(event.key_code);
        }
      });
}

}  // namespace

void GuiSystem::Register(flecs::world& world) {
  world.observer<RegisterComponentsEvent>()
      .event(flecs::OnAdd)
      .yield_existing()
      .each([world = world](const auto&) { RegisterComponents(world); });

  world.observer<InitSystemsEvent>()
      .event(flecs::OnAdd)
      .yield_existing()
      .each([world = world](const auto&) { RegisterSystems(world); });

  world.observer<InitWorldDataEvent>()
      .event(flecs::OnAdd)
      .yield_existing()
      .each([world = world](const auto&) { CreateDefaults(world); });
}

void GuiSystem::ShutdownImGui() {
  if (!g_imgui_ready) {
    return;
  }
  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplSDL3_Shutdown();
  ImGui::DestroyContext();
  g_imgui_ready = false;
}

}  // namespace z13::raylib
