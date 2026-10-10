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
#include <iterator>
#include <numeric>
#include <string>
#include <string_view>
#include <utility>

#include <z13/components/station.h>

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
    "avg_ms,worst_ms,culling_us,chunks_drawn,chunks,meshes_drawn,glass_chunks_drawn,glass_chunks,lights,shadowed_lights,"
    "rooms_seen,background_ms,shadows_ms,players_ms,opaque_ms,glass_ms,previews_ms,overlay_ms,release_ms,flush_ms\n";
constexpr std::string_view kSystemsCsvHeader = "viewpoint,culling,chunk_cells,system,ms_per_frame\n";
constexpr std::string_view kSystemsFileSuffix = "_systems";
// Systems below this per frame are left out of the systems CSV.
constexpr double kMinSystemMs = 0.01;
constexpr std::string_view kRoomsCulling = "rooms";
constexpr std::string_view kFrustumCulling = "frustum";

// std::accumulate rather than ranges::to, which GCC 13 lacks.
std::string CsvOf(std::string_view header, const std::vector<std::string>& rows) {
  return std::accumulate(rows.begin(), rows.end(), std::string(header));
}

// Quoted, so a comma in a template-named system's path stays in one field.
std::string CsvField(std::string_view text) {
  std::string quoted = "\"";
  for (const char c : text) {
    quoted += c == '"' ? std::string_view("\"\"") : std::string_view(&c, 1);
  }
  return quoted + '"';
}

// A pose inside a block is filed under the vacuum.
z13::station::rooms::Viewpoint InCells(const z13::CameraPose& pose, const z13::station::rooms::RoomGraph& graph) {
  const Eigen::Vector3f eye = pose.eye / z13::station::kCellSize;
  return {.room = graph.RoomAt(eye.array().floor().cast<int>()).value_or(z13::station::rooms::kVacuumRoom),
          .eye = eye,
          .target = eye + z13::Forward(pose)};
}

}  // namespace

RenderTour::RenderTour(std::filesystem::path output, std::vector<z13::CameraPose> views)
    : output_(std::move(output)), views_(std::move(views)) {
}

bool RenderTour::ReadyToStart() {
  return ++startup_frames_ >= kStartupFrames;
}

void RenderTour::Start(const z13::station::rooms::RoomGraph& graph, int chunk_cells) {
  chunk_cells_ = chunk_cells;
  std::vector<z13::station::rooms::Viewpoint> viewpoints;
  if (views_.empty()) {
    viewpoints = z13::station::rooms::TourViewpoints(graph, kTourRooms);
  } else {
    std::ranges::transform(views_, std::back_inserter(viewpoints),
                           [&graph](const z13::CameraPose& pose) { return InCells(pose, graph); });
  }
  for (const z13::station::rooms::Viewpoint& view : viewpoints) {
    for (const bool cull_rooms : kCullRooms) {
      stops_.push_back({.view = view, .room_cells = graph.rooms[view.room].volume_cells, .cull_rooms = cull_rooms});
    }
  }
  started_ = true;
}

// The frame ending at a stop's first step was still drawn at the previous stop.
void RenderTour::Step(Clock::time_point now, const RenderStats& stats, const z13::SystemSampler& sample_systems) {
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
    tally_.times += stats.times;
    tally_.last = stats;
  }
  last_frame_ = now;
  if (frame_ == kWarmupFrames) {
    systems_before_ = sample_systems();
  }
  if (frame_ < kWarmupFrames + kMeasuredFrames) {
    ++frame_;
    return;
  }
  const std::string_view culling = stop->cull_rooms ? kRoomsCulling : kFrustumCulling;
  for (const z13::SystemTime& system : z13::SystemTimesPerFrame(systems_before_, sample_systems(), tally_.frames)) {
    if (system.ms >= kMinSystemMs) {
      system_rows_.push_back(std::format("{},{},{},{},{:.4f}\n", stop_ / kCullRooms.size(), culling, chunk_cells_,
                                         CsvField(system.path), system.ms));
    }
  }
  const Eigen::Vector3f& eye = stop->view.eye;
  const Eigen::Vector3f& target = stop->view.target;
  const RenderStats& drawn = tally_.last;
  const double frames = std::max(tally_.frames, 1);
  rows_.push_back(std::format(
      "{},{},{},{:.1f},{:.1f},{:.1f},{:.1f},{:.1f},{:.1f},{},{},{},{:.3f},{:.3f},{:.1f},{},{},{},{},{},{},{},{},"
      "{:.3f},{:.3f},{:.3f},{:.3f},{:.3f},{:.3f},{:.3f},{:.3f},{:.3f}\n",
      stop_ / kCullRooms.size(), stop->view.room, stop->room_cells, eye.x(), eye.y(), eye.z(), target.x(), target.y(),
      target.z(), culling, chunk_cells_, tally_.frames,
      tally_.total_ms / frames, tally_.worst_ms, tally_.culling_us / frames, drawn.chunks_drawn, drawn.chunks,
      drawn.meshes_drawn, drawn.glass_chunks_drawn, drawn.glass_chunks, drawn.lights, drawn.shadowed_lights,
      drawn.rooms_seen ? std::to_string(*drawn.rooms_seen) : std::string(), tally_.times.background_ms / frames,
      tally_.times.shadows_ms / frames, tally_.times.players_ms / frames, tally_.times.opaque_ms / frames, tally_.times.glass_ms / frames,
      tally_.times.previews_ms / frames, tally_.times.overlay_ms / frames, tally_.times.release_ms / frames,
      tally_.times.flush_ms / frames));
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
  return CsvOf(kCsvHeader, rows_);
}

std::filesystem::path RenderTour::SystemsOutput() const {
  std::filesystem::path path = output_;
  path.replace_filename(std::format("{}{}{}", output_.stem().string(), kSystemsFileSuffix, output_.extension().string()));
  return path;
}

std::string RenderTour::SystemsCsv() const {
  return CsvOf(kSystemsCsvHeader, system_rows_);
}

}  // namespace z13::raylib
