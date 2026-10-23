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

#pragma once

#include <chrono>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <flecs.h>

#include <lib_core/utils/frame_recorder.h>
#include <lib_core/utils/frame_timeline.h>

#include "render_stats.h"

namespace z13::raylib {

// How the game's frames are timed for a frame report (lib_core/utils/frame_report.h): the
// timeline the frame's parts leave marks in, and the --frame-log line. Never state.
struct FrameProfile {
  using Singleton = void;
  using Clock = std::chrono::steady_clock;

  z13::FrameTimeline timeline;
  // Set by --frame-log: a line of the frame times goes to the log once a second.
  bool log_each_second {};
  std::optional<Clock::time_point> last_logged;
};

std::vector<z13::FrameStage> StagesOf(const DrawTimes& times);

// The frame report of the last seconds, with the device and the station it was measured in.
std::string ProfileReport(flecs::world world);

// What --frame-log writes: the last frames' fps and where their time went, on one line.
std::string ProfileLine(const z13::FrameRecorder& recorder, const RenderStats& stats);

// Writes `report` to the logs directory under a name with the time, and returns the path.
std::expected<std::filesystem::path, std::string> SaveProfileReport(const std::string& report);

}  // namespace z13::raylib
