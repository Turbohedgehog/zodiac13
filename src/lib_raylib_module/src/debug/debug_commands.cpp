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



#include "debug_commands.h"

#include <algorithm>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <lib_core/commands/command_registry.h>
#include <lib_core/state/world_state.h>
#include <lib_core/world/lifecycle.h>
#include <lib_core/utils/asset_source.h>
#include <z13/components/gameplay/player.h>
#include <z13/components/gameplay/player_parts.h>
#include <z13/components/input/input_state.h>

#include "gui/game_scenes.h"
#include "gui/game_transitions.h"
#include "gui/pending_transition.h"
#include "gui/profile_teleport.h"
#include "render/frame_profile.h"
#include "render/render_components.h"
#include "render/render_stats.h"
#include "render/scene_models.h"

namespace z13::raylib {

namespace {

struct FrameReportCommand {
  using Command = z13::Command<std::string,
                               "The frame report of the last seconds, as the Profile window shows it: the build, "
                               "the GPU, counters, where the frame's time goes. With `restart` the frames are "
                               "forgotten after the report, so the next one covers only what follows.",
                               Tags<kDebugTag>>;

  bool restart {};
};

struct CameraInfo {
  double x {};
  double y {};
  double z {};
  double yaw_deg {};
  double pitch_deg {};
};

struct DrawTimesInfo {
  double background_ms {};
  double shadows_ms {};
  double players_ms {};
  double opaque_ms {};
  double glass_ms {};
  double previews_ms {};
  double overlay_ms {};
  double release_ms {};
  double flush_ms {};
};

struct RenderStatsInfo {
  CameraInfo camera;
  std::size_t chunks {};
  std::size_t chunks_drawn {};
  std::size_t meshes_drawn {};
  std::size_t glass_chunks {};
  std::size_t glass_chunks_drawn {};
  std::size_t lights {};
  std::size_t shadowed_lights {};
  int debug_view {};
  std::optional<std::size_t> rooms_seen;  // absent while rooms don't cull
  double culling_us {};
  DrawTimesInfo draw_times;
};

struct RenderStatsCommand {
  using Command = z13::Command<RenderStatsInfo,
                               "What the last frame drew: the camera, chunks and meshes drawn, lamps, the CPU "
                               "time of each part of the drawing.",
                               Tags<kDebugTag>>;

  int unused {};
};

struct TeleportCommand {
  using Command = z13::Command<std::string,
                               "Puts the local player at x, y metres (z is the height of the feet; the current "
                               "height without it) and stops their fall, looking `yaw_deg` and `pitch_deg` "
                               "(degrees, pitch positive down; what is left out stays) if given. Only in a game "
                               "of one player.",
                               Tags<kDebugTag, "cheat">>;

  double x {};
  double y {};
  std::optional<double> z;
  std::optional<double> yaw_deg;
  std::optional<double> pitch_deg;
};

struct NoclipCommand {
  using Command = z13::Command<std::string,
                               "Turns noclip of the local player on or off (the opposite of now without `enabled`): "
                               "they fly through blocks, with no gravity. Only in a game of one player.",
                               Tags<kDebugTag, "cheat">>;

  std::optional<bool> enabled;
};

struct ScenesCommand {
  using Command = z13::Command<std::vector<std::string>, "The scenes a game can start in: the names start_game takes.",
                               Tags<kDebugTag>>;

  int unused {};
};

struct StartGameCommand {
  using Command = z13::Command<std::string,
                               "Starts a local game in `scene` (a name from scenes; the default station without it), "
                               "from the main menu or in place of the game that runs.",
                               Tags<kDebugTag, "cheat">>;

  std::optional<std::string> scene;
};

struct LeaveGameCommand {
  using Command = z13::Command<std::string, "Leaves the game for the main menu.", Tags<kDebugTag, "cheat">>;

  int unused {};
};

struct ReloadShadersCommand {
  using Command = z13::Command<std::string,
                               "Recompiles the lighting shader without a restart: a #define for each name of "
                               "`defines` (the parts lighting.fs can leave out: Z13_NO_LIGHTING, Z13_NO_CELL_LIGHT, "
                               "Z13_NO_PLAYER_SHADOWS, Z13_NO_CLUSTER_LIGHTS, Z13_NO_SUN, Z13_NO_FILL, "
                               "Z13_NO_BUMP), or the text of `vertex`/`fragment` instead of the files. Without "
                               "arguments it brings the shipped shader back; a shader that fails to compile is "
                               "reported and the old one keeps drawing.",
                               Tags<kDebugTag>>;

  std::string shader = "lighting";
  std::vector<std::string> defines;
  std::optional<std::string> vertex;
  std::optional<std::string> fragment;
};

constexpr std::string_view kLightingShaderName = "lighting";

RenderStatsInfo InfoOf(const RenderStats& stats) {
  const DrawTimes& times = stats.times;
  return {
      .camera = {.x = stats.camera.eye.x(), .y = stats.camera.eye.y(), .z = stats.camera.eye.z(),
                 .yaw_deg = stats.camera.look.yaw_deg, .pitch_deg = stats.camera.look.pitch_deg},
      .chunks = stats.chunks,
      .chunks_drawn = stats.chunks_drawn,
      .meshes_drawn = stats.meshes_drawn,
      .glass_chunks = stats.glass_chunks,
      .glass_chunks_drawn = stats.glass_chunks_drawn,
      .lights = stats.lights,
      .shadowed_lights = stats.shadowed_lights,
      .debug_view = stats.debug_view,
      .rooms_seen = stats.rooms_seen,
      .culling_us = stats.culling_us,
      .draw_times = {.background_ms = times.background_ms, .shadows_ms = times.shadows_ms,
                     .players_ms = times.players_ms, .opaque_ms = times.opaque_ms, .glass_ms = times.glass_ms,
                     .previews_ms = times.previews_ms, .overlay_ms = times.overlay_ms,
                     .release_ms = times.release_ms, .flush_ms = times.flush_ms},
  };
}

void RegisterComponents(flecs::world world) {
  flecs_tools::RegisterComponents<FrameReportCommand, RenderStatsCommand, TeleportCommand, NoclipCommand, ReloadShadersCommand, ScenesCommand,
                                  StartGameCommand, LeaveGameCommand>(world);
}

void RegisterSystems(flecs::world world) {
  CommandSystem<FrameReportCommand>(
      world, "DebugCommands::FrameReport", [](auto& builder) { builder.template write<FrameProfile>(); },
      [](flecs::iter& it, size_t, const FrameReportCommand& command, ReplyOf<FrameReportCommand>& reply) {
        reply.Succeed(ProfileReport(it.world()));
        if (command.restart) {
          it.world().get_mut<FrameProfile>().timeline.Clear();
        }
      });
  CommandSystem<RenderStatsCommand>(
      world, "DebugCommands::RenderStats",
      [](flecs::iter& it, size_t, const RenderStatsCommand&, ReplyOf<RenderStatsCommand>& reply) {
        if (const auto* stats = it.world().try_get<RenderStats>()) {
          reply.Succeed(InfoOf(*stats));
        } else {
          reply.Fail("nothing has been drawn yet");
        }
      });
  CommandSystem<ScenesCommand>(
      world, "DebugCommands::Scenes",
      [](auto& builder) { builder.template read<z13::Assets>(); },
      [](flecs::iter& it, size_t, const ScenesCommand&, ReplyOf<ScenesCommand>& reply) {
        std::vector<std::string> names;
        for (const gui::GameScene& scene : gui::GameScenes(it.world().get<z13::Assets>().source)) {
          names.push_back(scene.label);
        }
        reply.Succeed(std::move(names));
      });
  CommandSystem<StartGameCommand>(
      world, "DebugCommands::StartGame",
      [](auto& builder) {
        builder.template read<z13::Assets>();
        builder.template write<gui::PendingTransition>();
      },
      [](flecs::iter& it, size_t, const StartGameCommand& command, ReplyOf<StartGameCommand>& reply) {
        flecs::world world = it.world();
        const std::vector<gui::GameScene> scenes = gui::GameScenes(world.get<z13::Assets>().source);
        const auto chosen = command.scene ? std::ranges::find(scenes, *command.scene, &gui::GameScene::label)
                                          : scenes.begin();
        if (chosen == scenes.end()) {
          reply.Fail(command.scene ? std::format("no scene '{}': see scenes", *command.scene) : "there are no scenes");
          return;
        }
        gui::StartGame(world, *chosen);
        reply.Succeed(std::format("starting '{}'", chosen->label));
      });
  CommandSystem<LeaveGameCommand>(
      world, "DebugCommands::LeaveGame", [](auto& builder) { builder.template write<gui::PendingTransition>(); },
      [](flecs::iter& it, size_t, const LeaveGameCommand&, ReplyOf<LeaveGameCommand>& reply) {
        gui::LeaveGame(it.world());
        reply.Succeed("leaving the game");
      });
  CommandSystem<ReloadShadersCommand>(
      world, "DebugCommands::ReloadShaders",
      [](auto& builder) {
        builder.template read<Lighting>();
        builder.template read<z13::Assets>();
      },
      [](flecs::iter& it, size_t, const ReloadShadersCommand& command, ReplyOf<ReloadShadersCommand>& reply) {
        if (command.shader != kLightingShaderName) {
          reply.Fail(std::format("no shader '{}' to reload: {}", command.shader, kLightingShaderName));
          return;
        }
        flecs::world world = it.world();
        const ShaderTextOverride text {.vertex = command.vertex, .fragment = command.fragment};
        if (const auto reloaded = ReloadLighting(world.get<Lighting>(), world.get<z13::Assets>().source,
                                                 command.defines, text);
            !reloaded) {
          reply.Fail(reloaded.error());
          return;
        }
        reply.Succeed(std::format("lighting shader rebuilt with {} defines", command.defines.size()));
      });
  CommandSystem<NoclipCommand>(
      world, "DebugCommands::NoClip", [](auto& builder) { builder.template write<z13::gameplay::NoClip>(); },
      [](flecs::iter& it, size_t, const NoclipCommand& command, ReplyOf<NoclipCommand>& reply) {
        const auto on = gui::SetNoClip(it.world(), command.enabled);
        if (!on) {
          reply.Fail(on.error());
          return;
        }
        reply.Succeed(*on ? "noclip on" : "noclip off");
      });
  CommandSystem<TeleportCommand>(
      world, "DebugCommands::Teleport", [](auto& builder) { builder.template write<z13::input::InputState>(); },
      [](flecs::iter& it, size_t, const TeleportCommand& command, ReplyOf<TeleportCommand>& reply) {
        const Eigen::Vector2f position(static_cast<float>(command.x), static_cast<float>(command.y));
        std::optional<float> height;
        if (command.z) {
          height = static_cast<float>(*command.z);
        }
        const auto angle = [](const std::optional<double>& degrees) {
          return degrees ? std::optional(static_cast<float>(*degrees)) : std::nullopt;
        };
        const gui::LookTarget look {.yaw_deg = angle(command.yaw_deg), .pitch_deg = angle(command.pitch_deg)};
        if (const auto moved = gui::TeleportLocalPlayer(it.world(), position, height, look); !moved) {
          reply.Fail(moved.error());
          return;
        }
        reply.Succeed(std::format("teleported to x {:.2f}, y {:.2f}", position.x(), position.y()));
      });
}

}  // namespace

void DebugCommands::Register(flecs::world& world) {
  OnRegisterComponents(world, RegisterComponents);
  OnInitSystems(world, RegisterSystems);
}

}  // namespace z13::raylib
