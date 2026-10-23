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

#include "frame_profile.h"

#include <algorithm>
#include <array>
#include <format>
#include <functional>
#include <ranges>

#include <SDL3/SDL.h>

#include <lib_core/utils/file_io.h>
#include <lib_core/utils/frame_report.h>
#include <lib_core/version.h>
#include <z13/components/station_mode.h>
#include <z13_settings/environment.h>
#include <z13_settings/settings.h>

#include <raylib_module/raylib_components.h>

#include "../platform/sdl_platform.h"
#include "station_props.h"

namespace z13::raylib {

namespace {

constexpr std::string_view kFileNamePrefix = "frame-report-";
constexpr std::string_view kFileNameTimeFormat = "{:%Y%m%d-%H%M%S}";
constexpr std::string_view kFileExtension = ".txt";
// The parts of the frame --frame-log names, the largest first.
constexpr size_t kLineShares = 3;

#ifdef NDEBUG
constexpr std::string_view kBuildKind = "release";
#else
constexpr std::string_view kBuildKind = "debug";
#endif

std::string BuildText() {
  std::string text = std::format("{} {} ({})", z13::kEngineVersion, z13::kBuildDate, kBuildKind);
#ifdef Z13_PROFILE
  text += ", Tracy zones";
#endif
  return text;
}

std::string Fraction(size_t part, size_t whole) {
  return std::format("{} / {}", part, whole);
}

}  // namespace

std::vector<z13::FrameStage> StagesOf(const DrawTimes& times) {
  return {{.name = "draw: background", .ms = times.background_ms}, {.name = "draw: shadows", .ms = times.shadows_ms},
          {.name = "draw: players", .ms = times.players_ms},       {.name = "draw: opaque", .ms = times.opaque_ms},
          {.name = "draw: glass", .ms = times.glass_ms},           {.name = "draw: previews", .ms = times.previews_ms},
          {.name = "draw: overlay", .ms = times.overlay_ms},       {.name = "draw: release", .ms = times.release_ms},
          {.name = "draw: flush", .ms = times.flush_ms}};
}

std::string ProfileReport(flecs::world world) {
  z13::FrameReportInput input;
  input.header.emplace_back("Build", BuildText());
  input.header.emplace_back("Platform", std::format("{}, {} cores", SDL_GetPlatform(), SDL_GetNumLogicalCPUCores()));
  if (const auto* platform = world.try_get<SdlPlatformData>(); platform != nullptr && platform->platform) {
    input.header.emplace_back("GPU", std::format("{} ({})", platform->platform->Gl().renderer,
                                                  platform->platform->Gl().version));
  }
  if (const auto* size = world.try_get<WindowSize>(); size != nullptr) {
    input.header.emplace_back("Window", std::format("{} x {}", size->size.x(), size->size.y()));
  }
  if (const auto* choice = world.try_get<z13::station::StationSceneChoice>(); choice != nullptr && choice->scene) {
    input.header.emplace_back("Scene", *choice->scene);
  }
  if (const auto* tuning = world.try_get<z13::RenderTuning>(); tuning != nullptr) {
    input.header.emplace_back(
        "Lamp shadows", std::format("{} slots of {} px, {} updates a frame", tuning->shadow_lights,
                                    tuning->shadow_map_size, tuning->shadow_updates_per_frame));
  }
  if (const auto* stats = world.try_get<RenderStats>(); stats != nullptr) {
    const z13::CameraPose& camera = stats->camera;
    input.header.emplace_back("Eye", std::format("x {:.2f}, y {:.2f}, z {:.2f} m, yaw {:.1f}, pitch {:.1f}",
                                                  camera.eye.x(), camera.eye.y(), camera.eye.z(),
                                                  camera.look.yaw_deg, camera.look.pitch_deg));
    input.counters.emplace_back("Chunks drawn", Fraction(stats->chunks_drawn, stats->chunks));
    input.counters.emplace_back("Glass chunks drawn", Fraction(stats->glass_chunks_drawn, stats->glass_chunks));
    input.counters.emplace_back("Meshes drawn", std::to_string(stats->meshes_drawn));
    input.counters.emplace_back("Lamps, with shadows", Fraction(stats->shadowed_lights, stats->lights));
    input.counters.emplace_back(
        "Rooms seen", stats->rooms_seen ? std::to_string(*stats->rooms_seen) : std::string("rooms don't cull"));
  }
  if (const auto* props = world.try_get<StationProps>(); props != nullptr) {
    input.counters.emplace_back("Props", std::to_string(props->Count()));
  }
  const auto* profile = world.try_get<FrameProfile>();
  if (profile == nullptr) {
    return z13::FrameReport({}, input);
  }
  input.systems = profile->timeline.Recorder().SystemsPerFrame();
  const std::vector<z13::FrameSample> frames = profile->timeline.Recorder().Frames();
  return z13::FrameReport(frames, input);
}

std::string ProfileLine(const z13::FrameRecorder& recorder, const RenderStats& stats) {
  const std::vector<z13::FrameSample> frames = recorder.Frames();
  const z13::FrameSummary summary = z13::SummarizeFrames(frames);
  std::string line = std::format("{:.0f} fps, mean {:.1f} ms, worst {:.1f} ms |", summary.mean_fps, summary.mean_ms,
                                 summary.worst_ms);
  const std::vector<z13::FrameShare> shares = z13::TimeShares(frames);
  for (const z13::FrameShare& share : shares | std::views::take(kLineShares)) {
    line += std::format(" {} {:.1f}", share.name, share.ms);
  }
  return line + std::format(" | chunks {}/{}, meshes {}, lamps {}", stats.chunks_drawn, stats.chunks,
                            stats.meshes_drawn, stats.lights);
}

std::expected<std::filesystem::path, std::string> SaveProfileReport(const std::string& report) {
  const auto now = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
  const std::filesystem::path path =
      z13::tools::environment::GetGameLogsDirectory() /
      std::format("{}{}{}", kFileNamePrefix, std::vformat(kFileNameTimeFormat, std::make_format_args(now)),
                  kFileExtension);
  if (const auto written = z13::WriteFile(path, report); !written) {
    return std::unexpected(written.error());
  }
  return path;
}

}  // namespace z13::raylib
