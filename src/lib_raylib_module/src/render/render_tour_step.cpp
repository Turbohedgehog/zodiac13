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

#include "render_tour_step.h"

#include <filesystem>
#include <string>

#include <Eigen/Dense>

#include <lib_core/utils/file_io.h>
#include <lib_core/utils/log.h>
#include <z13/components/station/block.h>

namespace z13::raylib {

namespace {

void WriteTourFile(const std::filesystem::path& path, const std::string& csv) {
  if (const auto written = z13::WriteFile(path, csv); !written) {
    log_error("[raylib] render tour: {}", written.error());
  } else {
    log_info("[raylib] render tour written to '{}'", path.string());
  }
}

}  // namespace

bool StepRenderTour(RaylibCamera& camera, RenderTour& tour, const TourFrame& frame) {
  if (!tour.Started()) {
    if (frame.rooms && tour.ReadyToStart()) {
      tour.Start(*frame.rooms, frame.chunk_cells);
      log_info("[raylib] render tour: {} stops", tour.StopCount());
    }
    return true;
  }
  const size_t before = tour.StopIndex();
  tour.Step(RenderTour::Clock::now(), frame.stats, frame.sample_systems);
  if (tour.StopIndex() != before) {
    log_info("[raylib] render tour: stop {} of {} measured", tour.StopIndex(), tour.StopCount());
  }
  if (const auto stop = tour.Current()) {
    const Eigen::Vector3f eye = stop->view.eye * z13::station::kCellSize;
    const Eigen::Vector3f target = stop->view.target * z13::station::kCellSize;
    camera.camera.position = {eye.x(), eye.y(), eye.z()};
    camera.camera.target = {target.x(), target.y(), target.z()};
    camera.camera.up = {0.f, 0.f, 1.f};
    return true;
  }
  WriteTourFile(tour.Output(), tour.Csv());
  WriteTourFile(tour.SystemsOutput(), tour.SystemsCsv());
  return false;
}

}  // namespace z13::raylib
