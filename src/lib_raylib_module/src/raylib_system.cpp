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
#include <cstdint>
#include <memory>
#include <string_view>

#include <flecs.h>

#include <lib_core/state/rollback.h>
#include <lib_core/state/world_state.h>
#include <lib_core/time/simulation_clock.h>
#include <lib_core/utils/flecs_utils.h>
#include <lib_core/utils/log.h>
#include <lib_core/world/components.h>
#include <lib_core/world/core.h>
#include <lib_core/world/lifecycle.h>

#include <raylib_module/raylib_components.h>

#include "gui/gui_system.h"
#include "platform/sdl_platform.h"
#include "render/block_meshes.h"
#include "render/render_components.h"

namespace z13::raylib {

struct RenderThrottle {
  using Singleton = void;
  uint64_t skipped_frames {};
};

namespace {

constexpr std::string_view kWindowTitle = "Zodiac 13";

// Floor on the frame rate while catching up: at least every (kMaxSkippedFrames + 1)-th frame draws.
constexpr uint64_t kMaxSkippedFrames {3};

template <class... Phases>
void SetPhasesEnabled(flecs::world& world, bool enabled) {
  const auto set_enabled = [enabled](flecs::entity phase) {
    if (enabled) {
      phase.enable();
    } else {
      phase.disable();
    }
  };
  (set_enabled(world.entity<Phases>()), ...);
}

void ThrottleRender(flecs::world& world, z13::flecs_tools::FrameKind kind) {
  using z13::flecs_tools::FrameKind;
  RenderThrottle& throttle = world.get_mut<RenderThrottle>();
  const bool draw = kind == FrameKind::kLive ||
                    (kind == FrameKind::kCatchUp && throttle.skipped_frames >= kMaxSkippedFrames);
  // Replays re-draw the past: skipped without counting toward the floor.
  if (kind != FrameKind::kReplay) {
    throttle.skipped_frames = draw ? 0 : throttle.skipped_frames + 1;
  }
  if (world.entity<PreRender>().enabled() != draw) {
    SetPhasesEnabled<PreRender, Render, PostRender, FinalizeRender>(world, draw);
  }
}

void RegisterPipelines(flecs::world world) {
  using z13::flecs_tools::PresentationPhase;
  world.component<ReadEvents>().add(flecs::Phase).add<PresentationPhase>().depends_on(flecs::PreFrame);

  // Real phase barrier for ReadEvents' consumers, since same-phase order isn't guaranteed.
  world.component<ConsumeEvents>().add(flecs::Phase).add<PresentationPhase>().depends_on<ReadEvents>();
  world.get_alive(flecs::PreUpdate).add(flecs::Phase).depends_on<ConsumeEvents>();

  world.component<RenderGatePhase>().add(flecs::Phase).add<PresentationPhase>().depends_on(flecs::OnStore);
  world.component<PreRender>().add(flecs::Phase).add<PresentationPhase>().depends_on<RenderGatePhase>();
  world.component<Render>().add(flecs::Phase).add<PresentationPhase>().depends_on<PreRender>();
  world.component<PostRender>().add(flecs::Phase).add<PresentationPhase>().depends_on<Render>();
  world.component<FinalizeRender>().add(flecs::Phase).add<PresentationPhase>().depends_on<PostRender>();
  world.get_alive(flecs::PostFrame).add(flecs::Phase).depends_on<FinalizeRender>();
}

void RegisterComponents(flecs::world world) {
  z13::flecs_tools::RegisterComponents<RaylibData, WindowSize, SdlPlatformData, RenderThrottle>(world);
  world.entity<RenderGate>().set<flecs::TickSource>({.tick = true, .time_elapsed = 0.f});
}

void ShutdownCore(flecs::world world) { world.get<CoreComponent>().core->get().Shutdown(); }

void CreateDefaults(flecs::world world) {
  const Eigen::Vector2i kWindowSize {960, 600};
  auto platform = std::make_shared<SdlPlatform>();
  world.set(SdlPlatformData{.platform = platform});
  if (!platform->Init(kWindowSize.x(), kWindowSize.y(), kWindowTitle)) {
    log_critical("[raylib] SDL platform init failed");
    platform->Shutdown();
    ShutdownCore(world);
    return;
  }
  world.set(RaylibData{.initialized = true});
  world.set(WindowSize{.size = platform->Size()});
}

void Shutdown(flecs::entity e, RaylibWindowClosed, RaylibData&, SdlPlatformData& platform_data) {
  // Frees GPU resources promptly rather than waiting for world teardown; not
  // strictly required since their dtors no-op once IsGlContextAlive() is false.
  e.world().remove<RenderModel>();
  e.world().remove<Lighting>();
  e.world().remove<Skybox>();
  e.world().remove<BlockMeshes>();
  GuiSystem::ShutdownImGui(e.world());  // needs the GL context, so before SdlPlatform.
  platform_data.platform->Shutdown();
  e.world().remove<RaylibData>();
  ShutdownCore(e.world());
}

void RegisterSystems(flecs::world world) {
  world.set<RenderThrottle>({});
  z13::flecs_tools::OnFrameStart(world, ThrottleRender);

  // A rollback requested this frame replays it, and the replay's last frame is drawn instead.
  world.system("RaylibSystem::UpdateRenderGate")
      .kind<RenderGatePhase>()
      .immediate()
      .run([](flecs::iter& it) {
        flecs::world world = it.world();
        world.entity<RenderGate>().get_mut<flecs::TickSource>().tick =
            !world.get<z13::flecs_tools::RollbackRequest>().to_tick.has_value();
      });

  world.system<const RaylibData, WindowSize, SdlPlatformData>("RaylibSystem::FrameBegin")
      .kind<PreRender>()
      .tick_source<RenderGate>()
      .each([](const RaylibData&, WindowSize& size, SdlPlatformData& platform_data) {
        SdlPlatform& platform = *platform_data.platform;
        size.size = platform.Size();
        static constexpr std::array<unsigned char, 3> kClearColor = {28, 28, 38};
        platform.BeginFrame(kClearColor[0], kClearColor[1], kClearColor[2]);
      });

  world.system<const RaylibData, SdlPlatformData>("RaylibSystem::FrameEnd")
      .kind<FinalizeRender>()
      .tick_source<RenderGate>()
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
  OnRegisterComponents(world, RegisterComponents);

  OnInitPhases(world, RegisterPipelines);

  OnInitSystems(world, RegisterSystems);

  OnInitWorldData(world, CreateDefaults);
}

}  // namespace z13::raylib
