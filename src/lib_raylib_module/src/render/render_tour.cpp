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

#include "render_tour.h"

#include <algorithm>
#include <array>
#include <format>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>

namespace z13::raylib {

namespace {

constexpr size_t kTourRooms = 12;
constexpr int kStartupFrames = 60;
// Frames left to settle after the camera moves, then the frames measured.
constexpr int kWarmupFrames = 30;
constexpr int kMeasuredFrames = 120;
// Each viewpoint is measured culled by rooms, then by the frustum alone.
constexpr std::array kCullRooms {true, false};

constexpr std::string_view kCsvHeader =
    "viewpoint,room,room_cells,eye_x,eye_y,eye_z,target_x,target_y,target_z,culling,chunk_cells,frames,"
    "avg_ms,worst_ms,culling_us,chunks_drawn,chunks,meshes_drawn,glass_drawn,glass,rooms_seen\n";
constexpr std::string_view kRoomsCulling = "rooms";
constexpr std::string_view kFrustumCulling = "frustum";

}  // namespace

RenderTour::RenderTour(std::filesystem::path output) : output_(std::move(output)) {
}

bool RenderTour::ReadyToStart() {
  return ++startup_frames_ >= kStartupFrames;
}

void RenderTour::Start(const z13::station::rooms::RoomGraph& graph, int chunk_cells) {
  chunk_cells_ = chunk_cells;
  for (const z13::station::rooms::Viewpoint& view : z13::station::rooms::TourViewpoints(graph, kTourRooms)) {
    for (const bool cull_rooms : kCullRooms) {
      stops_.push_back({.view = view, .room_cells = graph.rooms[view.room].volume_cells, .cull_rooms = cull_rooms});
    }
  }
  started_ = true;
}

// The frame ending at a stop's first step was still drawn at the previous stop.
void RenderTour::Step(Clock::time_point now, const RenderStats& stats) {
  const auto stop = Current();
  if (!stop) {
    return;
  }
  if (last_frame_ && frame_ > kWarmupFrames) {
    const double ms = std::chrono::duration<double, std::milli>(now - *last_frame_).count();
    ++tally_.frames;
    tally_.total_ms += ms;
    tally_.worst_ms = std::max(tally_.worst_ms, ms);
    tally_.culling_us += stats.culling_us;
    tally_.last = stats;
  }
  last_frame_ = now;
  if (frame_ < kWarmupFrames + kMeasuredFrames) {
    ++frame_;
    return;
  }
  const Eigen::Vector3f& eye = stop->view.eye;
  const Eigen::Vector3f& target = stop->view.target;
  const RenderStats& drawn = tally_.last;
  const double frames = std::max(tally_.frames, 1);
  rows_.push_back(std::format(
      "{},{},{},{:.1f},{:.1f},{:.1f},{:.1f},{:.1f},{:.1f},{},{},{},{:.3f},{:.3f},{:.1f},{},{},{},{},{},{}\n",
      stop_ / kCullRooms.size(), stop->view.room, stop->room_cells, eye.x(), eye.y(), eye.z(), target.x(), target.y(),
      target.z(), stop->cull_rooms ? kRoomsCulling : kFrustumCulling, chunk_cells_, tally_.frames,
      tally_.total_ms / frames, tally_.worst_ms, tally_.culling_us / frames, drawn.chunks_drawn, drawn.chunks,
      drawn.meshes_drawn, drawn.glass_drawn, drawn.glass,
      drawn.rooms_seen ? std::to_string(*drawn.rooms_seen) : std::string()));
  ++stop_;
  frame_ = 0;
  tally_ = {};
}

std::optional<TourStop> RenderTour::Current() const {
  return stop_ < stops_.size() ? std::optional(stops_[stop_]) : std::nullopt;
}

bool RenderTour::CullsRooms() const {
  const auto stop = Current();
  return !stop || stop->cull_rooms;
}

std::string RenderTour::Csv() const {
  return std::format("{}{}", kCsvHeader, rows_ | std::views::join | std::ranges::to<std::string>());
}

}  // namespace z13::raylib
