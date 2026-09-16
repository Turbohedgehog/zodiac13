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

#include <array>
#include <memory>
#include <string_view>

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

constexpr std::string_view kWindowTitle = "Zodiac 13";

void RegisterPipelines(flecs::world world) {
  world.component<ReadEvents>().add(flecs::Phase).depends_on(flecs::PreFrame);

  // Real phase barrier for ReadEvents' consumers, since same-phase order isn't guaranteed.
  world.component<ConsumeEvents>().add(flecs::Phase).depends_on<ReadEvents>();
  world.get_alive(flecs::PreUpdate).add(flecs::Phase).depends_on<ConsumeEvents>();

  world.component<PreRender>().add(flecs::Phase).depends_on(flecs::OnStore);
  world.component<Render>().add(flecs::Phase).depends_on<PreRender>();
  world.component<PostRender>().add(flecs::Phase).depends_on<Render>();
  world.component<FinalizeRender>().add(flecs::Phase).depends_on<PostRender>();
  world.get_alive(flecs::PostFrame).add(flecs::Phase).depends_on<FinalizeRender>();
}

void RegisterComponents(flecs::world world) {
  world.component<RaylibData>().add(flecs::Singleton);
  world.component<WindowSize>().add(flecs::Singleton);
  world.component<SdlPlatformData>().add(flecs::Singleton);
}

void ShutdownCore(flecs::world world) { world.get<CoreComponent>().core->get().Shutdown(); }

void CreateDefaults(flecs::world world) {
  static const Eigen::Vector2i kWindowSize = {960, 600};
  auto platform = std::make_shared<SdlPlatform>();
  world.set(SdlPlatformData{.platform = platform});
  if (!platform->Init(kWindowSize.x(), kWindowSize.y(), kWindowTitle.data())) {
    LOG_CRITICAL("[raylib] SDL platform init failed");
    platform->Shutdown();
    ShutdownCore(world);
    return;
  }
  world.set(RaylibData{.initialized = true});
  world.set(WindowSize{.size = kWindowSize});
}

void Shutdown(flecs::entity e, RaylibWindowClosed, RaylibData&, SdlPlatformData& platform_data) {
  // Frees GPU resources promptly rather than waiting for world teardown; not
  // strictly required since their dtors no-op once IsGlContextAlive() is false.
  e.world().remove<RenderModel>();
  e.world().remove<Lighting>();
  e.world().remove<Skybox>();
  // Per-entity, unlike the singletons above (e.g. an in-progress building brush).
  e.world().remove_all<BuildingBlock>();
  GuiSystem::ShutdownImGui(e.world());  // needs the GL context, so before SdlPlatform.
  platform_data.platform->Shutdown();
  e.world().remove<RaylibData>();
  ShutdownCore(e.world());
}

void RegisterSystems(flecs::world world) {
  // Pre-add it so CreateDefaults's set() (same InitWorldDataEvent dispatch as
  // InputPublisher's) is a value update, not a deferred add InputPublisher could miss.
  world.ensure<SdlPlatformData>();

  world.system<const RaylibData, WindowSize, SdlPlatformData>("RaylibSystem::FrameBegin")
      .kind<PreRender>()
      .each([](const RaylibData&, WindowSize& size, SdlPlatformData& platform_data) {
        SdlPlatform& platform = *platform_data.platform;
        size.size = platform.Size();
        static constexpr std::array<unsigned char, 3> kClearColor = {28, 28, 38};
        platform.BeginFrame(kClearColor[0], kClearColor[1], kClearColor[2]);
      });

  world.system<const RaylibData, SdlPlatformData>("RaylibSystem::FrameEnd")
      .kind<FinalizeRender>()
      .each([world](const RaylibData&, SdlPlatformData& platform_data) {
        SdlPlatform& platform = *platform_data.platform;
        platform.EndFrame();
        if (platform.QuitRequested()) {
          world.add<RaylibWindowClosed>();
        }
      });

  world.observer<RaylibWindowClosed, RaylibData, SdlPlatformData>("RaylibSystem::ShutdownObserver")
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
