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

#include "raylib_system.h"

#include <flecs.h>

#include <lib_core/components.h>
#include <lib_core/core.h>
#include <lib_core/log.h>

#include <raylib_module/raylib_components.h>

#include "gui/gui_system.h"
#include "platform/sdl_platform.h"
#include "render/render_components.h"

namespace z13::raylib {

namespace {

constexpr int kWindowWidth = 960;
constexpr int kWindowHeight = 600;
constexpr const char* kWindowTitle = "Zodiac 13";
constexpr unsigned char kClearColor[3] = {28, 28, 38};

void RegisterPipelines(flecs::world world) {
  world.component<ReadEvents>().add(flecs::Phase).depends_on(flecs::PreFrame);
  world.get_alive(flecs::PreUpdate).add(flecs::Phase).depends_on<ReadEvents>();

  world.component<PreRender>().add(flecs::Phase).depends_on(flecs::OnStore);
  world.component<Render>().add(flecs::Phase).depends_on<PreRender>();
  world.component<PostRender>().add(flecs::Phase).depends_on<Render>();
  world.component<FinalizeRender>().add(flecs::Phase).depends_on<PostRender>();
  world.get_alive(flecs::PostFrame).add(flecs::Phase).depends_on<FinalizeRender>();
}

void RegisterComponents(flecs::world world) {
  world.component<RaylibData>().add(flecs::Singleton);
  world.component<WindowSize>().add(flecs::Singleton);
}

void ShutdownCore(flecs::world world) { world.get<CoreComponent>().core->get().Shutdown(); }

void CreateDefaults(flecs::world world) {
  if (!SdlPlatform::Init(kWindowWidth, kWindowHeight, kWindowTitle)) {
    LOG_CRITICAL("[raylib] SDL platform init failed");
    SdlPlatform::Shutdown();
    ShutdownCore(world);
    return;
  }
  world.set(RaylibData{.initialized = true});
  world.set(WindowSize{kWindowWidth, kWindowHeight});
}

void Shutdown(flecs::entity e, RaylibWindowClosed, RaylibData&) {
  // Free GPU resources while the GL context is still alive.
  e.world().remove<RenderModel>();
  e.world().remove<Lighting>();
  e.world().remove<Skybox>();
  // Per-entity, unlike the singletons above (e.g. an in-progress building brush).
  e.world().remove_all<BuildingBlock>();
  GuiSystem::ShutdownImGui();  // needs the GL context, so before SdlPlatform.
  SdlPlatform::Shutdown();
  e.world().remove<RaylibData>();
  ShutdownCore(e.world());
}

void RegisterSystems(flecs::world world) {
  // Drain SDL events first so the UI and input layers consume the same batch.
  world.system<const RaylibData>("RaylibSystem::PumpEvents")
      .kind<ReadEvents>()
      .each([](const RaylibData&) { SdlPlatform::PumpEvents(); });

  world.system<const RaylibData, WindowSize>("RaylibSystem::FrameBegin")
      .kind<PreRender>()
      .each([](const RaylibData&, WindowSize& size) {
        size.width = SdlPlatform::Width();
        size.height = SdlPlatform::Height();
        SdlPlatform::BeginFrame(kClearColor[0], kClearColor[1], kClearColor[2]);
      });

  world.system<const RaylibData>("RaylibSystem::FrameEnd")
      .kind<FinalizeRender>()
      .each([world](const RaylibData&) {
        SdlPlatform::EndFrame();
        if (SdlPlatform::QuitRequested()) {
          world.add<RaylibWindowClosed>();
        }
      });

  world.observer<RaylibWindowClosed, RaylibData>("RaylibSystem::ShutdownObserver")
      .event(flecs::OnAdd)
      .each(Shutdown);
}

}  // namespace

void RaylibSystem::Register(flecs::world& world) {
  world.observer<RegisterComponentsEvent>()
      .event(flecs::OnAdd)
      .yield_existing()
      .each([world = world](const auto&) { RegisterComponents(world); });

  world.observer<InitPhasesEvent>()
      .event(flecs::OnAdd)
      .yield_existing()
      .each([world = world](const auto&) { RegisterPipelines(world); });

  world.observer<InitSystemsEvent>()
      .event(flecs::OnAdd)
      .yield_existing()
      .each([world = world](const auto&) { RegisterSystems(world); });

  world.observer<InitWorldDataEvent>()
      .event(flecs::OnAdd)
      .yield_existing()
      .each([world = world](const auto&) { CreateDefaults(world); });
}

}  // namespace z13::raylib
