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
#include "environment_render_system.h"

#include <algorithm>
#include <chrono>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <Eigen/Dense>
#include <flecs.h>
#include <raylib.h>
#include <raymath.h>
#include <rlgl.h>

#include <lib_core/settings/config.h>
#include <lib_core/state/rollback.h>
#include <lib_core/state/world_state.h>
#include <lib_core/utils/asset_source.h>
#include <lib_core/utils/flecs_utils.h>
#include <lib_core/utils/log.h>
#include <lib_core/utils/drawn_poses.h>
#include <lib_core/utils/system_times.h>
#include <lib_core/world/components.h>
#include <lib_core/world/lifecycle.h>

#include <z13/components/input/action_listener.h>
#include <z13/components/station/building.h>
#include <z13/components/station/door.h>
#include <z13/components/gameplay/gameplay.h>
#include <z13/components/gameplay/ghost.h>
#include <z13/components/station/grid.h>
#include <z13/components/utility/lamps.h>
#include <z13/components/gameplay/player.h>
#include <z13/components/gameplay/player_parts.h>
#include <z13/components/station/rooms.h>
#include <z13/components/station/block.h>
#include <z13/components/station/block_brush.h>
#include <z13/components/station/station_mode.h>
#include <primitives/palette.h>
#include <rooms/view_culling.h>
#include <rooms/room_cache.h>
#include <z13_settings/power_tuning.h>
#include <z13_settings/settings.h>

#include <raylib_module/raylib_components.h>

#include "block_chunks.h"
#include "block_meshes.h"
#include "cell_lighting.h"
#include "chunk_sync.h"
#include "frame_lighting.h"
#include "frame_profile.h"
#include "remote_players.h"
#include "render_components.h"
#include "render_stats.h"
#include "render_tour.h"
#include "render_tour_step.h"
#include "scene_camera.h"
#include "scene_meshes.h"
#include "scene_models.h"
#include "scene_overlay.h"
#include "scene_queries.h"
#include "scene_view.h"
#include "shadow_maps.h"
#include "skybox.h"
#include "station_props.h"

namespace z13::raylib {

using z13::station::rooms::CameraView;
using z13::station::rooms::ViewCulling;

namespace {

constexpr std::chrono::seconds kFrameLogInterval {1};

void RegisterComponents(flecs::world world) {
  z13::flecs_tools::RegisterComponents<RaylibCamera, Skybox, RenderModel, AvatarModel, Lighting, BlockMeshes,
                                         BlockChunks, ShadowMaps, CellLighting, RenderStats, RenderTour, StationProps,
                                         FrameProfile, z13::VisualSmoothing, z13::RenderTuning>(world);
}

void StartProfiledFrame(flecs::world& world, z13::flecs_tools::FrameKind) {
  FrameProfile& profile = world.get_mut<FrameProfile>();
  const RenderStats& stats = world.get<RenderStats>();
  const auto now = FrameProfile::Clock::now();
  profile.systems.Sample(world, profile.system_totals);
  profile.timeline.StartFrame(now, world.has<gameplay::Pause>(), StagesOf(stats.times), profile.system_totals);
  if (profile.log_each_second && (!profile.last_logged || now - *profile.last_logged >= kFrameLogInterval)) {
    profile.last_logged = now;
    log_info("[frame] {}", ProfileLine(profile.timeline.Recorder(), stats));
  }
}

// The overlay outlines the rooms drawn this frame (station_module builds its boxes).
void PublishSeenRooms(const ViewCulling& culling, const z13::station::rooms::RoomCache* rooms, RenderStats& stats,
                      z13::station::RoomOverlay* overlay) {
  stats.rooms_seen = culling.SeenRoomCount();
  if (overlay == nullptr) {
    return;
  }
  overlay->drawn.reset();
  const auto fingerprint = rooms != nullptr ? rooms->CurrentFingerprint() : std::nullopt;
  if (overlay->mode == z13::station::RoomOverlayMode::kSeen && fingerprint) {
    if (auto seen = culling.SeenRooms()) {
      overlay->drawn = z13::station::RoomsDrawn {.fingerprint = *fingerprint, .rooms = std::move(*seen)};
    }
  }
}

template <typename Draw>
double MsOf(Draw draw) {
  const auto start = std::chrono::steady_clock::now();
  draw();
  return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

// The skybox and the spaceship model, drawn before the scene.
void DrawBackground(const flecs::world& world) {
  if (world.has<Skybox>()) {
    if (const Skybox& skybox = world.get<Skybox>(); skybox.res) {
      DrawSkybox(*skybox.res);
    }
  }
  if (world.has<RenderModel>()) {
    if (const RenderModel& render_model = world.get<RenderModel>(); render_model.res) {
      DrawModel(*render_model.res->model, Vector3Zero(), 1.f, WHITE);
    }
  }
}

// Players and opaque chunks first; then glass from the farthest and previews, blended over
// them and without writing depth, so nothing behind them is hidden.
void DrawScene(const flecs::world& world, const BrushQuery& brushes, const RemotePlayerQuery& remote_players,
               const GhostQuery& ghosts, const z13::DrawnPoses& drawn, const SceneView& view,
               const z13::RenderTuning& tuning, RenderStats& stats) {
  stats = {};
  stats.times.players_ms = MsOf([&] { DrawRemotePlayers(world, remote_players, ghosts, drawn, view.culling, tuning.avatar_radius_m); });
  if (!world.has<BlockMeshes>() || !world.has<BlockChunks>() || !world.has<Lighting>()) {
    return;
  }
  const Lighting& lighting = world.get<Lighting>();
  const SingleBlockDrawing drawing {
      .meshes = world.get_mut<BlockMeshes>(), .lighting = lighting, .palette = PaletteOf(world)};
  const BlockChunks& chunks = world.get<BlockChunks>();
  stats.times.opaque_ms = MsOf([&] {
    chunks.DrawOpaque(view.culling, lighting, stats);
    if (const auto* props = world.try_get<StationProps>()) {
      // The blocks leave the checker on, which replaces every texture.
      StopChecker(lighting);
      props->Draw(view.culling);
    }
    if (const auto* scene_meshes = world.try_get<SceneMeshes>()) {
      scene_meshes->Draw(view.culling, lighting);
    }
    rlDrawRenderBatchActive();
  });

  rlDisableDepthMask();
  stats.times.glass_ms = MsOf([&] {
    chunks.DrawTransparent(view.culling, Eigen::Vector3f(view.eye.x, view.eye.y, view.eye.z), lighting, stats);
  });
  stats.times.previews_ms = MsOf([&] {
    brushes.each([&world, &drawing](flecs::entity brush, const z13::building::Brush&, const Eigen::Matrix4f& transform) {
      DrawBrushPreview(world, brush, transform, drawing);
    });
    rlDrawRenderBatchActive();
  });
  rlEnableDepthMask();
  stats.times.overlay_ms = MsOf([&] {
    StopChecker(lighting);
    DrawRoomOverlay(world);
    DrawLayerOverlay(world);
    DrawBrushLines(brushes);
  });
  stats.times.release_ms = MsOf([&] { drawing.meshes.get().ReleaseUnused(); });
}

void RegisterSystems(flecs::world world) {
  // The launcher overwrites this with the loaded settings (z13::InstallSettings).
  world.set<z13::VisualSmoothing>({});
  world.set<z13::RenderTuning>({});
  world.set<RenderStats>({});
  world.set<FrameProfile>({});
  // The frame report names the heaviest systems.
  ecs_measure_system_time(world, true);
  z13::flecs_tools::OnFrameStart(world, StartProfiledFrame);
  // Shared by the closures below; lives as long as the world.
  auto drawing = std::make_shared<SmoothedDrawing>();

  // RaylibData is set right after InitWindow, so a live GL context is guaranteed.
  world.observer<RaylibData, const z13::Assets, const z13::RenderTuning>("EnvironmentRenderSystem::LoadEnvironment")
      .event(flecs::OnAdd)
      // Only RaylibData's arrival loads.
      .term_at(1).filter()
      .yield_existing()
      .each([](flecs::iter& it, size_t, RaylibData&, const z13::Assets& assets, const z13::RenderTuning& tuning) {
        flecs::world world = it.world();
        Lighting lighting = LoadLighting(assets.source);
        world.set<AvatarModel>(LoadAvatar(lighting, tuning));
        const ::Shader shader = lighting.res ? *lighting.res->shader : ::Shader {};
        world.set<BlockMeshes>(BlockMeshes(shader));
        world.set<BlockChunks>(BlockChunks(shader, assets.source));
        const ::Shader shadow_shader = LoadShadowShader(assets.source);
        if (shadow_shader.id == 0) {
          log_error("[raylib] the shadow shader didn't load: nothing casts shadows, the sun is off in a station");
        }
        world.set<ShadowMaps>(ShadowMaps(shadow_shader));
        world.set<CellLighting>({});
        world.set<SceneMeshes>({});
        world.set<Lighting>(std::move(lighting));
        world.set<Skybox>(LoadSkybox(assets.source));
        const auto config = z13::GetCoreConfig(world);
        world.get_mut<FrameProfile>().log_each_second = config && config->get().IsFrameLog();
        if (const auto tour = config ? config->get().GetRenderTourPath() : std::nullopt) {
          world.set(RenderTour(*tour, config->get().GetRenderTourViews()));
          ecs_measure_system_time(world, true);
        }
      });

  // Everything below runs in the Render phase, ahead of Draw (systems in one phase run
  // in registration order), so the scene is synced with this frame's final state.
  world.system<const Lighting, const z13::Assets, const z13::RenderTuning>(
           "EnvironmentRenderSystem::ReconcileSpaceship")
      .kind<Render>()
      .tick_source<RenderGate>()
      .read<gameplay::Gameplay>()
      .read<z13::station::StationMode>()
      .write<RenderModel>()
      .each([](flecs::iter& it, size_t, const Lighting& lighting, const z13::Assets& assets,
               const z13::RenderTuning& tuning) {
        ReconcileSpaceship(it.world(), lighting, assets.source, tuning);
      });

  world.system<const Lighting, const z13::Assets, const z13::RenderTuning>(
           "EnvironmentRenderSystem::ReconcileStationProps")
      .kind<Render>()
      .tick_source<RenderGate>()
      .read<z13::station::StationMode>()
      .read<z13::station::StationSceneChoice>()
      .write<StationProps>()
      .each([](flecs::iter& it, size_t, const Lighting& lighting, const z13::Assets& assets,
               const z13::RenderTuning& tuning) {
        ReconcileStationProps(it.world(), lighting, assets.source, tuning);
      });

  world.system<const RaylibData, const gameplay::Camera>("EnvironmentRenderSystem::EnsureRaylibCamera")
      .kind<Render>()
      .tick_source<RenderGate>()
      .with<z13::input::CurrentActionListenerTag>()
      .without<RaylibCamera>()
      .write<RaylibCamera>()
      .each(EnsureRaylibCamera);

  world.system<RaylibCamera, const gameplay::Camera, const Eigen::Matrix4f>(
           "EnvironmentRenderSystem::SyncRaylibCamera")
      .kind<Render>()
      .tick_source<RenderGate>()
      .with<z13::input::CurrentActionListenerTag>()
      .each(SyncRaylibCamera);

  world.system<const RaylibCamera>("EnvironmentRenderSystem::ReleaseOrphanRaylibCamera")
      .kind<Render>()
      .tick_source<RenderGate>()
      .read<gameplay::Camera>()
      .read<z13::input::CurrentActionListenerTag>()
      .write<RaylibCamera>()
      .each(ReleaseOrphanRaylibCamera);

  const BlockQuery block_query =
      world.query_builder<const z13::station::Block>("EnvironmentRenderSystem::BlockQuery").detect_changes().build();
  const BrushQuery brush_query =
      world.query_builder<const z13::building::Brush, const Eigen::Matrix4f>("EnvironmentRenderSystem::BrushQuery")
          .build();

  RemotePlayerQuery remote_player_query =
      world.query_builder<const gameplay::Player, const Eigen::Matrix4f>("EnvironmentRenderSystem::RemotePlayerQuery")
          .without<z13::input::CurrentActionListenerTag>()
          .build();

  GhostQuery ghost_query =
      world.query_builder<const gameplay::GhostPlayer, const Eigen::Matrix4f>("EnvironmentRenderSystem::GhostQuery")
          .build();

  DrawnQuery drawn_query = world.query_builder<const Eigen::Matrix4f>("EnvironmentRenderSystem::DrawnQuery")
                               .with<gameplay::Player>()
                               .or_()
                               .with<gameplay::GhostPlayer>()
                               .build();

  const FlashlightQuery flashlight_query =
      world.query_builder<const gameplay::Flashlight, const Eigen::Matrix4f>("EnvironmentRenderSystem::FlashlightQuery")
          .with<gameplay::Player>()
          .build();

  world.system<const VisualSmoothing>("EnvironmentRenderSystem::ChaseDrawnPoses")
      .kind<Render>()
      .tick_source<RenderGate>()
      .read<z13::input::CurrentActionListenerTag>()
      .each([drawing, drawn_query](flecs::iter&, size_t, const VisualSmoothing& settings) {
        ChaseDrawnPoses(*drawing, settings, drawn_query);
      });

  world.system<BlockChunks, const z13::RenderTuning, const z13::building::primitives::BlockPalette*>(
           "EnvironmentRenderSystem::SyncBlockChunks")
      .kind<Render>()
      .tick_source<RenderGate>()
      .read<z13::station::Block>()
      .read<StationProps>()
      .each([block_query, synced_replaced = std::make_shared<std::vector<std::string>>()](
                flecs::iter& it, size_t, BlockChunks& chunks, const z13::RenderTuning& tuning,
                const z13::building::primitives::BlockPalette* palette) {
        const auto* props = it.world().try_get<StationProps>();
        SyncBlockChunks(chunks, block_query, tuning,
                        palette != nullptr ? BlockMeshes::OptionalPalette(palette->palette) : std::nullopt,
                        props != nullptr ? props->Replaced() : std::vector<std::string> {}, *synced_replaced);
      });

  // Its own query: SyncBlockChunks resets the other's changed state.
  const BlockQuery light_block_query =
      world.query_builder<const z13::station::Block>("EnvironmentRenderSystem::LightBlockQuery")
          .detect_changes()
          .build();
  world.system<CellLighting, const z13::building::primitives::BlockPalette*, const z13::utility::DimmedLamps*,
               const z13::PowerTuning*>("EnvironmentRenderSystem::SyncCellLighting")
      .kind<Render>()
      .tick_source<RenderGate>()
      .read<z13::station::Block>()
      .each([light_block_query](CellLighting& lighting, const z13::building::primitives::BlockPalette* palette,
                                const z13::utility::DimmedLamps* dimmed, const z13::PowerTuning* power) {
        const BlockMeshes::OptionalPalette shown =
            palette != nullptr ? BlockMeshes::OptionalPalette(palette->palette) : std::nullopt;
        const EmergencyLamps emergency {.dimmed = dimmed != nullptr ? std::span<const z13::utility::DimmedLamp>(dimmed->list)
                                                                      : std::span<const z13::utility::DimmedLamp>(),
                                        .percent = power != nullptr ? static_cast<int>(power->emergency_light_percent) : 0};
        if (light_block_query.changed() || !lighting.SyncedWith(shown, emergency)) {
          if (const Status synced = lighting.Sync(CollectBlocks(light_block_query), shown, emergency); !synced) {
            log_error("[raylib] lamps: {}", synced.error());
          }
        }
      });

  // After SyncRaylibCamera, so the tour's camera wins.
  world.system<RaylibCamera, RenderTour, const RenderStats, const z13::RenderTuning>(
           "EnvironmentRenderSystem::StepRenderTour")
      .kind<Render>()
      .tick_source<RenderGate>()
      .read<z13::station::rooms::RoomCache>()
      .with<z13::input::CurrentActionListenerTag>()
      .write<RaylibWindowClosed>()
      .each([](flecs::iter& it, size_t, RaylibCamera& camera, RenderTour& tour, const RenderStats& stats,
               const z13::RenderTuning& tuning) {
        flecs::world world = it.world();
        const auto main_rooms = z13::station::MainGridData<z13::station::rooms::RoomCache>(world);
        const TourFrame frame {
            .stats = stats,
            .rooms = CurrentRooms(main_rooms ? &main_rooms->get() : nullptr),
            .chunk_cells = tuning.chunk_cells,
            .sample_systems = [world] { return z13::SystemTimes(world); },
        };
        if (!world.has<RaylibWindowClosed>() && !StepRenderTour(camera, tour, frame)) {
          world.add<RaylibWindowClosed>();
        }
      });

  // Render phase: 3D scene between FrameBegin (PreRender) and FrameEnd (FinalizeRender).
  world.system<const RaylibCamera, const WindowSize, RenderStats, FrameProfile, z13::station::RoomOverlay*,
               const RenderTour*, const z13::RenderTuning, const CellLighting*, ShadowMaps*>(
           "EnvironmentRenderSystem::Draw")
      .kind<Render>()
      .tick_source<RenderGate>()
      .read<z13::station::BrushPreview>()
      .read<z13::station::rooms::RoomCache>()
      .each([brush_query, remote_player_query, ghost_query, drawn_query, flashlight_query, drawing](
                flecs::iter& it, size_t, const RaylibCamera& raylib_camera, const WindowSize& size, RenderStats& stats,
                FrameProfile& profile, z13::station::RoomOverlay* overlay, const RenderTour* tour,
                const z13::RenderTuning& tuning, const CellLighting* cell_lighting, ShadowMaps* shadows) {
        flecs::world world = it.world();
        const auto main_rooms = z13::station::MainGridData<z13::station::rooms::RoomCache>(world);
        const z13::station::rooms::RoomCache* rooms = main_rooms ? &main_rooms->get() : nullptr;
        profile.timeline.MarkDrawStart(FrameProfile::Clock::now());
        double background_ms = MsOf([&] { SetViewPosition(world, raylib_camera.camera.position); });
        BeginScene3D(raylib_camera.camera, size.size);

        const ::Vector3& eye = raylib_camera.camera.position;
        const auto culling_start = std::chrono::steady_clock::now();
        std::vector<z13::building::primitives::CellBox> opened;
        if (const auto* doors = world.try_get<z13::station::DoorPortals>()) {
          std::ranges::transform(doors->open, std::back_inserter(opened), [](const z13::station::DoorOpening& door) {
            return z13::building::primitives::CellBox {.min = door.min, .extent = door.extent};
          });
        }
        const CameraView camera {
            .view_projection = CurrentViewProjection(),
            .eye = Eigen::Vector3f(eye.x, eye.y, eye.z),
            .min_portal_extent = PixelsInNdc(tuning.min_portal_pixels, size.size),
            .opened = opened,
        };
        const SceneView view {
            .eye = eye,
            .culling = ViewCulling(camera, tour == nullptr || tour->CullsRooms() ? CurrentRooms(rooms) : std::nullopt),
        };
        const double culling_us =
            std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - culling_start).count();

        LitScene lit;
        const double shadows_ms = MsOf([&] {
          if (!world.has<BlockChunks>()) {
            return;
          }
          const ShadowCasters casters = CastersOf(world.get<BlockChunks>(), world.try_get<AvatarModel>(),
                                                  PlayerCasters(drawn_query, drawing->poses, tuning.avatar_radius_m));
          lit = LightScene(world, FlashlightsOf(flashlight_query, drawing->poses, view.culling, camera.eye, tuning),
                           cell_lighting, shadows, casters, tuning);
          // The shadow passes drew elsewhere, with their own viewport and matrices.
          rlViewport(0, 0, size.size.x(), size.size.y());
          BeginScene3D(raylib_camera.camera, size.size);
        });
        background_ms += MsOf([&] {
          UploadLighting(world, lit, shadows, CurrentClusterView(size.size), tuning);
          DrawBackground(world);
        });

        DrawScene(world, brush_query, remote_player_query, ghost_query, drawing->poses, view, tuning, stats);
        LogRemotePlayers(world, remote_player_query, *drawing, view, tuning.avatar_radius_m);
        stats.culling_us = culling_us;
        stats.times.background_ms = background_ms;
        stats.times.shadows_ms = shadows_ms;
        stats.lights = lit.lights.size();
        stats.debug_view = world.has<Lighting>() && world.get<Lighting>().res ? world.get<Lighting>().res->debug_view : 0;
        stats.shadowed_lights =
            std::ranges::count_if(lit.lights, [](const FrameLight& light) { return light.shadow_slot.has_value(); });
        const ::Camera3D& pose = raylib_camera.camera;
        stats.camera = z13::PoseLookingAt(Eigen::Vector3f(pose.position.x, pose.position.y, pose.position.z),
                                          Eigen::Vector3f(pose.target.x, pose.target.y, pose.target.z));
        PublishSeenRooms(view.culling, rooms, stats, overlay);

        stats.times.flush_ms = MsOf(EndScene3D);
      });
}

}  // namespace

void EnvironmentRenderSystem::Register(flecs::world& world) {
  OnRegisterComponents(world, RegisterComponents);

  OnInitSystems(world, RegisterSystems);
}

}  // namespace z13::raylib
