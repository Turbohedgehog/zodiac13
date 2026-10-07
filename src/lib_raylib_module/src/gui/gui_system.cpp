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

#include <chrono>
#include <string_view>

#include <flecs.h>

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl3.h>
#include <rlgl.h>

#include <lib_core/state/world_state.h>
#include <lib_core/utils/flecs_utils.h>
#include <lib_core/utils/log.h>
#include <lib_core/world/components.h>
#include <lib_core/world/lifecycle.h>

#include <z13/components/gameplay.h>
#include <z13/components/input.h>

#include <raylib_module/raylib_components.h>

#include "gui_windows.h"
#include "main_menu_window.h"
#include "palette_window.h"
#include "pause_menu_window.h"
#include "platform/sdl_platform.h"
#include "render/render_stats.h"
#include "stats_overlay.h"

namespace z13::raylib {

namespace {

constexpr std::string_view kGlslVersion = "#version 330";

// Singleton: whether the ImGui context/backends are up. Was a file-scope static;
// moved into the world so nothing here is process-global state.
struct GuiState {
  using Singleton = void;
  bool imgui_ready {};
};

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
  z13::flecs_tools::RegisterComponents<gui::WindowStack, GuiState, gui::StatsOverlay>(world);
}

// Runs once RaylibData is set, i.e. after SdlPlatform::Init brought up the window
// / GL context (same trigger EnvironmentRenderSystem uses to load GPU resources).
void InitImGui(const SdlPlatform& platform, GuiState& state) {
  if (state.imgui_ready || !platform.IsReady()) {
    return;
  }
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGui::GetIO().IniFilename = nullptr;
  ImGui::StyleColorsDark();
  ImGui_ImplSDL3_InitForOpenGL(platform.Window(), platform.GlContext());
  ImGui_ImplOpenGL3_Init(kGlslVersion.data());
  state.imgui_ready = true;
  log_info("[gui] Dear ImGui {} initialised (SDL3 + OpenGL3)", IMGUI_VERSION);
}

void ForwardImGuiEvents(const SdlPlatform& platform) {
  for (const SDL_Event& event : platform.FrameEvents()) {
    ImGui_ImplSDL3_ProcessEvent(&event);
  }
}

void BeginImGuiFrame() {
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
  // Set before InitImGui exists: set() on a new component fires OnAdd first and then
  // assigns the value, which would wipe the imgui_ready the observer just wrote.
  world.set<gui::WindowStack>({});
  world.set<GuiState>({});
  world.set<gui::StatsOverlay>({});

  // Bring up ImGui once the SDL window / GL context exists (RaylibData is set at
  // the tail of RaylibSystem::CreateDefaults).
  world.observer<const RaylibData, const SdlPlatformData, GuiState>("GuiSystem::InitImGui")
      .event(flecs::OnAdd)
      .yield_existing()
      .each([](const RaylibData&, const SdlPlatformData& platform_data, GuiState& state) {
        InitImGui(*platform_data.platform, state);
      });

  // Every tick, drawn or not: ImGui queues the events until its next NewFrame. Runs in
  // ConsumeEvents, not ReadEvents, to guarantee it's after PumpEvents.
  world.system<const RaylibData, const SdlPlatformData, const GuiState>("GuiSystem::ForwardEvents")
      .kind<ConsumeEvents>()
      .each([](const RaylibData&, const SdlPlatformData& platform_data, const GuiState& state) {
        if (state.imgui_ready) {
          ForwardImGuiEvents(*platform_data.platform);
        }
      });

  world.system<const RaylibData, const GuiState>("GuiSystem::BeginFrame")
      .kind<PreRender>()
      .tick_source<RenderGate>()
      .each([](const RaylibData&, const GuiState& state) {
        if (state.imgui_ready) {
          BeginImGuiFrame();
        }
      });

  // PostRender: build the window stack on top of the 3D scene, then render the
  // ImGui draw data. Render must be called every frame to match NewFrame.
  world.system<gui::WindowStack, const GuiState, gui::StatsOverlay, const RenderStats*>("GuiSystem::Draw")
      .kind<PostRender>()
      .tick_source<RenderGate>()
      .each([world](gui::WindowStack& stack, const GuiState& state, gui::StatsOverlay& stats,
                    const RenderStats* render) {
        if (!state.imgui_ready) {
          return;
        }
        const bool paused = world.has<gameplay::Pause>();
        if (paused && stack.windows.empty()) {
          stack.windows.push_back(world.has<gameplay::Gameplay>() ? gui::MakeGameplayPauseMenu(world)
                                                                  : gui::MakeMainMenu(world));
        } else if (!paused && !stack.windows.empty()) {
          stack.windows.clear();
        }
        if (!stack.windows.empty()) {
          ApplyStackRequest(world, stack, stack.windows.back()->Draw());
        } else if (world.has<gameplay::FreeCursor>()) {
          gui::MakePaletteWindow(world)->Draw();
        }
        stats.CountFrame(std::chrono::steady_clock::now());
        if (ImGui::IsKeyPressed(gui::kStatsOverlayKey, false)) {
          stats.Toggle();
        }
        if (stats.Visible()) {
          stats.Draw(render);
        }
        EndImGuiFrame();
      });

  // Esc while paused -> WindowBackEvent; other keys/mouse -> WindowKeyDownEvent
  // (both emitted by z13_module's gameplay_input_system).
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
  OnRegisterComponents(world, RegisterComponents);

  OnInitSystems(world, RegisterSystems);
}

void GuiSystem::ShutdownImGui(flecs::world world) {
  GuiState& state = world.get_mut<GuiState>();
  if (!state.imgui_ready) {
    return;
  }
  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplSDL3_Shutdown();
  ImGui::DestroyContext();
  state.imgui_ready = false;
}

}  // namespace z13::raylib
