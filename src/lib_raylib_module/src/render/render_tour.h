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
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <lib_core/utils/camera_pose.h>
#include <lib_core/utils/system_times.h>
#include <rooms/room_graph.h>
#include <rooms/tour_viewpoints.h>

#include "render_stats.h"
#include "view_culling.h"

namespace z13::raylib {

// One stop of the tour: a viewpoint, drawn with the rooms culling or with the frustum alone.
struct TourStop {
  z13::station::rooms::Viewpoint view;
  int64_t room_cells {};
  bool cull_rooms {};
};

// What the frames measured at the current stop add up to.
struct TourTally {
  int frames {};
  double total_ms {};
  double worst_ms {};
  double culling_us {};
  DrawTimes times {};
  RenderStats last;
};

// What the tour reads from the world each frame.
struct TourFrame {
  std::reference_wrapper<const RenderStats> stats;
  ViewCulling::OptionalGraph rooms;
  int chunk_cells {};
  z13::SystemSampler sample_systems;
};

// The --render-tour measurement: moves the camera from stop to stop, measures the frames
// drawn at each, and holds the results as CSV. Never state.
class RenderTour {
 public:
  using Singleton = void;
  using Clock = std::chrono::steady_clock;

  RenderTour() = default;
  // Measures `views` if any, else the tour's own points (rooms/tour_viewpoints.h).
  RenderTour(std::filesystem::path output, std::vector<z13::CameraPose> views);

  // Waits a few frames after the station's rooms exist, so its first draw isn't measured.
  bool ReadyToStart();
  void Start(const z13::station::rooms::RoomGraph& graph, int chunk_cells);
  bool Started() const { return started_; }
  size_t StopCount() const { return stops_.size(); }
  size_t StopIndex() const { return stop_; }

  // Records the frame that ended at `now`, drawn with `stats`, and moves to the next stop
  // once this one is measured. `sample_systems` is called around the measured frames only.
  void Step(Clock::time_point now, const RenderStats& stats, const z13::SystemSampler& sample_systems);

  // Nothing once every stop is measured.
  std::optional<TourStop> Current() const;
  bool CullsRooms() const;

  const std::filesystem::path& Output() const { return output_; }
  std::string Csv() const;
  // Each system's time per frame at each stop, next to Output().
  std::filesystem::path SystemsOutput() const;
  std::string SystemsCsv() const;

 private:
  std::filesystem::path output_;
  std::vector<z13::CameraPose> views_;
  int chunk_cells_ {};
  int startup_frames_ {};
  bool started_ {};
  std::vector<TourStop> stops_;
  size_t stop_ {};
  int frame_ {};
  std::optional<Clock::time_point> last_frame_;
  TourTally tally_;
  std::vector<z13::SystemTime> systems_before_;
  std::vector<std::string> rows_;
  std::vector<std::string> system_rows_;
};

}  // namespace z13::raylib
