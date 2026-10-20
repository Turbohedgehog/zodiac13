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

#include <algorithm>
#include <chrono>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <Eigen/Geometry>
#include <flecs.h>

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl3.h>
#include <raylib.h>
#include <rlgl.h>

#include <lib_core/state/world_state.h>
#include <lib_core/utils/enum_cycle.h>
#include <lib_core/utils/flecs_utils.h>
#include <lib_core/utils/log.h>
#include <lib_core/world/components.h>
#include <lib_core/world/lifecycle.h>

#include <z13/components/gameplay.h>
#include <z13/components/input_events.h>
#include <z13/components/net.h>
#include <z13/components/rooms.h>

#include <raylib_module/raylib_components.h>

#include "build_info.h"
#include "gui_widgets.h"
#include "gui_windows.h"
#include "hotbar_hud.h"
#include "loading_screen.h"
#include "main_menu_window.h"
#include "pending_transition.h"
#include "palette_window.h"
#include "pause_menu_window.h"
#include "platform/gl_profile.h"
#include "render/debug_view.h"
#include "render/render_components.h"
#include "platform/sdl_platform.h"
#include "render/render_stats.h"
#include "room_label.h"
#include "stats_overlay.h"
#include "touch_hud.h"

namespace z13::raylib {

namespace {

// Singleton: whether the ImGui context/backends are up. Was a file-scope static;
// moved into the world so nothing here is process-global state.
struct GuiState {
  using Singleton = void;
  bool imgui_ready {};
  // The display scale the style was last sized for.
  std::optional<float> display_scale;
};

constexpr std::string_view kJoinLoadingLabel = "Loading the station...";

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
  z13::flecs_tools::RegisterComponents<gui::WindowStack, GuiState, gui::StatsOverlay, gui::PendingTransition>(world);
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
  ImGui_ImplSDL3_InitForOpenGL(platform.Window(), platform.GlContext());
  ImGui_ImplOpenGL3_Init(kGlslVersion.data());
  state.imgui_ready = true;
  log_info("[gui] Dear ImGui {} initialised (SDL3 + OpenGL3)", IMGUI_VERSION);
}

// ImGui scales neither by itself: the style's sizes and the font follow the monitor the
// window is on, rebuilt from the default style when the window moves to another scale.
void ApplyDisplayScale(const SdlPlatform& platform, GuiState& state) {
  const float scale = platform.DisplayScale();
  if (state.display_scale == scale) {
    return;
  }
  ImGuiStyle style;
  ImGui::StyleColorsDark(&style);
  style.ScaleAllSizes(scale);
  style.FontScaleDpi = scale;
  ImGui::GetStyle() = style;
  state.display_scale = scale;
  log_info("[gui] display scale {}", scale);
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

  world.system<const RaylibData, const SdlPlatformData, GuiState>("GuiSystem::BeginFrame")
      .kind<PreRender>()
      .tick_source<RenderGate>()
      .each([](const RaylibData&, const SdlPlatformData& platform_data, GuiState& state) {
        if (state.imgui_ready) {
          ApplyDisplayScale(*platform_data.platform, state);
          BeginImGuiFrame();
        }
      });

  // PostRender: build the window stack on top of the 3D scene, then render the
  // ImGui draw data. Render must be called every frame to match NewFrame.
  world.system<gui::WindowStack, const GuiState, gui::StatsOverlay, const RenderStats*, z13::station::RoomOverlay*,
               z13::input::TouchControls*, const z13::TouchLayout*, const z13::net::ConnectionStatus*,
               gui::PendingTransition*>("GuiSystem::Draw")
      .kind<PostRender>()
      .tick_source<RenderGate>()
      .each([](flecs::iter& it, size_t, gui::WindowStack& stack, const GuiState& state, gui::StatsOverlay& stats,
               const RenderStats* render, z13::station::RoomOverlay* rooms, z13::input::TouchControls* touch,
               const z13::TouchLayout* touch_layout, const z13::net::ConnectionStatus* connection,
               gui::PendingTransition* pending) {
        flecs::world world = it.world();
        if (!state.imgui_ready) {
          return;
        }
        const bool paused = world.has<gameplay::Pause>();
        const bool in_game = world.has<gameplay::Gameplay>();
        // A game joined while undrawn frames load it, then paused by a lost focus, still has
        // the main menu's windows: rebuilt for the game.
        if (paused && (stack.windows.empty() || stack.in_game != in_game)) {
          stack = {.windows = {in_game ? gui::MakeGameplayPauseMenu(world) : gui::MakeMainMenu(world)},
                   .in_game = in_game};
        } else if (!paused && !stack.windows.empty()) {
          stack = {};
        }
        std::vector<Eigen::AlignedBox2f> brush_areas;
        if (!stack.windows.empty()) {
          ApplyStackRequest(world, stack, stack.windows.back()->Draw());
        } else if (world.has<gameplay::FreeCursor>()) {
          gui::MakePaletteWindow(world)->Draw();
        } else if (touch_layout != nullptr) {
          if (touch != nullptr) {
            gui::DrawTouchHud(*touch, *touch_layout, gui::LocalToolAction(world));
          }
          gui::DrawHotbar(world, *touch_layout);
          brush_areas = gui::DrawBrushRow(world, *touch_layout);
        }
        stats.CountFrame(std::chrono::steady_clock::now());
        if (ImGui::IsKeyPressed(gui::kStatsOverlayKey, false)) {
          stats.Toggle();
        }
        const bool cursor_free = paused || world.has<gameplay::FreeCursor>();
        const bool stats_full = stats.Visible() || (touch != nullptr && touch->stats_shown);
        const gui::OverlayPlacement stats_drawn =
            stats.Draw(render, gui::FirstOverlayTop(),
                       cursor_free ? gui::OverlayInput::kSelectable : gui::OverlayInput::kPassThrough, stats_full);
        float overlay_top = stats_drawn.next_top;
        if (touch != nullptr) {
          const ImVec2 display = ImGui::GetIO().DisplaySize;
          const Eigen::Vector2f screen(display.x, display.y);
          const auto fraction = [&screen](const Eigen::AlignedBox2f& area) {
            return Eigen::AlignedBox2f(area.min().cwiseQuotient(screen), area.max().cwiseQuotient(screen));
          };
          touch->stats_area = fraction(stats_drawn.area);
          touch->brush_areas.clear();
          std::ranges::transform(brush_areas, std::back_inserter(touch->brush_areas), fraction);
        }
        if (!ImGui::GetIO().WantTextInput && world.has<Lighting>()) {
          if (ImGui::IsKeyPressed(gui::kDebugViewPreviousKey, false)) {
            StepDebugView(world.get<Lighting>(), -1);
          }
          if (ImGui::IsKeyPressed(gui::kDebugViewNextKey, false)) {
            StepDebugView(world.get<Lighting>(), 1);
          }
        }
        if (rooms != nullptr) {
          if (ImGui::IsKeyPressed(gui::kRoomOverlayKey, false) && !ImGui::GetIO().WantTextInput) {
            rooms->mode = z13::NextEnumerator(rooms->mode);
          }
          if (rooms->mode != z13::station::RoomOverlayMode::kOff) {
            gui::DrawRoomLabel(rooms->label, overlay_top);
          }
        }
        if (pending != nullptr) {
          gui::DrawLoadingScreen(pending->label);
          ++pending->frames_shown;
        } else if (in_game && connection != nullptr && connection->state == z13::net::ConnectionState::kConnecting) {
          // A joined client stays kConnecting while it restores the snapshot.
          gui::DrawLoadingScreen(std::string(kJoinLoadingLabel));
        }
        gui::DrawBuildInfo();
        EndImGuiFrame();
      });

  // The action blocks its frame, so it waits for one that showed the loading screen.
  world.system<gui::PendingTransition>("GuiSystem::RunPendingTransition")
      .kind(flecs::PreFrame)
      .immediate()
      .write<gameplay::Gameplay>()
      .write<gameplay::Pause>()
      .each([](flecs::iter& it, size_t, gui::PendingTransition& pending) {
        if (pending.frames_shown == 0) {
          return;
        }
        flecs::world world = it.world();
        const auto action = std::move(pending.action);
        world.remove<gui::PendingTransition>();
        action(world);
      });

  // Esc while paused -> WindowBackEvent; other keys/mouse -> WindowKeyDownEvent
  // (both emitted by z13_module's gameplay_input_system).
  world.observer<const z13::input::WindowBackEvent, gui::WindowStack>("GuiSystem::OnWindowBack")
      .event<z13::input::SystemInputEventType>()
      .each([](flecs::iter& it, size_t, const z13::input::WindowBackEvent&, gui::WindowStack& stack) {
        if (!stack.windows.empty()) {
          ApplyStackRequest(it.world(), stack, stack.windows.back()->OnBack());
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
  state = {};
}

}  // namespace z13::raylib
