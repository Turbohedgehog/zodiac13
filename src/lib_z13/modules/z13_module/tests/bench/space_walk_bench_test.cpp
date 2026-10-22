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

// A bench, not a test (see docs/frame-time-plan.md). Run it with
//   z13_test_runner --gtest_also_run_disabled_tests --gtest_filter='SpaceWalkBench.*'
// The player is put at each stop of a walk out of the station and around it, and each system's
// time per tick is summed up there. CSV goes to $Z13_SPACE_WALK_DIR (default: <temp>/z13_space_walk).

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <Eigen/Dense>

#include <lib_core/utils/camera_pose.h>
#include <lib_core/utils/math.h>
#include <lib_core/utils/system_stats.h>
#include <lib_core/utils/system_times.h>
#include <z13/components/block.h>
#include <primitives/placement.h>
#include <rooms/space_walk.h>
#include <z13_tests/shipped_station.h>

#include "../support/z13_test_world.h"

namespace z13::testing {
namespace {

namespace primitives = z13::building::primitives;
namespace rooms = z13::station::rooms;

constexpr std::string_view kOutputDirEnv = "Z13_SPACE_WALK_DIR";
constexpr std::string_view kDefaultOutputDirName = "z13_space_walk";
constexpr std::string_view kStopsFileName = "stops.csv";
constexpr std::string_view kSystemsFileName = "systems.csv";
// What the world has built before the walk starts: bodies, rooms, the first snapshots.
constexpr int kWarmUpTicks = 250;
// The ticks after a move that don't count: the rooms, lights and bodies around the new place.
constexpr int kSettleTicks = 30;
constexpr int kMeasuredTicks = 120;
constexpr size_t kTopSystems = 6;
// Systems under this mean per tick are left out of the CSV.
constexpr double kMinSystemMs = 0.001;
// Where the 19 fps were seen (release, 2026-10-22), in meters.
const Eigen::Vector3f kSlowSpotM {193.32f, 80.17f, 1.75f};
constexpr std::string_view kWholeTick = "whole tick";

double Ms(std::chrono::steady_clock::duration duration) {
  return std::chrono::duration<double, std::milli>(duration).count();
}

primitives::CellBox HullOf(const std::vector<z13::station::Block>& blocks) {
  Eigen::Vector3i min = Eigen::Vector3i::Constant(std::numeric_limits<int>::max());
  Eigen::Vector3i max = Eigen::Vector3i::Constant(std::numeric_limits<int>::min());
  for (const z13::station::Block& block : blocks) {
    const primitives::CellBox box = primitives::OccupiedCells(block);
    min = min.cwiseMin(box.min);
    max = max.cwiseMax(box.End());
  }
  return {.min = min, .extent = max - min};
}

Eigen::Matrix4f PoseMatrix(const z13::CameraPose& pose) {
  Eigen::Matrix4f transform = Eigen::Matrix4f::Identity();
  transform.block<3, 3>(0, 0) =
      Eigen::Quaternionf::FromTwoVectors(Eigen::Vector3f::UnitX(), z13::Forward(pose)).toRotationMatrix();
  z13::math::SetTranslation(pose.eye, transform);
  return transform;
}

std::filesystem::path OutputDir() {
  const char* dir = std::getenv(kOutputDirEnv.data());
  return dir != nullptr ? std::filesystem::path(dir) : std::filesystem::temp_directory_path() / kDefaultOutputDirName;
}

struct StopResult {
  z13::SystemFrameStats systems;
  z13::SystemSummary tick;
};

// The whole tick's spread, as the same summary a system gets.
z13::SystemSummary SummaryOf(const std::vector<double>& tick_ms) {
  z13::SystemFrameStats stats;
  double spent = 0.0;
  for (const double ms : tick_ms) {
    stats.AddFrame({{.path = std::string(kWholeTick), .ms = spent}}, {{.path = std::string(kWholeTick), .ms = spent + ms}});
    spent += ms;
  }
  return stats.Summary().front();
}

StopResult MeasureStop(Z13TestWorld& world, const rooms::SpaceWalkStop& stop) {
  flecs::world w = world.World();
  const z13::CameraPose pose =
      z13::PoseLookingAt(stop.eye * z13::station::kCellSize, stop.target * z13::station::kCellSize);
  world.Player().set(PoseMatrix(pose));
  for (int tick = 0; tick < kSettleTicks; ++tick) {
    world.Tick();
  }
  StopResult result;
  std::vector<double> tick_ms;
  std::vector<z13::SystemTime> before = z13::SystemTimes(w);
  for (int tick = 0; tick < kMeasuredTicks; ++tick) {
    const auto start = std::chrono::steady_clock::now();
    world.Tick();
    tick_ms.push_back(Ms(std::chrono::steady_clock::now() - start));
    std::vector<z13::SystemTime> after = z13::SystemTimes(w);
    result.systems.AddFrame(before, after);
    before = std::move(after);
  }
  result.tick = SummaryOf(tick_ms);
  return result;
}

std::string Describe(const z13::SystemSummary& system) {
  return std::format("    {:<64} mean {:7.3f}  min {:7.3f}  median {:7.3f}  max {:7.3f} ms", system.path, system.mean_ms,
                     system.min_ms, system.median_ms, system.max_ms);
}

Eigen::Vector3f MetersOf(const Eigen::Vector3f& cells) {
  return cells * z13::station::kCellSize;
}

TEST(SpaceWalkBench, DISABLED_OctantWalk) {
  const auto palette = ShippedPalette();
  ASSERT_TRUE(palette.has_value()) << palette.error();
  const auto blocks = ShippedBlueprint(kOctantScene, *palette);
  ASSERT_TRUE(blocks.has_value()) << blocks.error();

  Z13TestWorld world({std::string(kStationSceneArg), std::string(kOctantScene)});
  world.Tick();  // bootstrap starts the game and spawns the player
  flecs::world w = world.World();
  const Eigen::Vector3f spawn =
      z13::math::ExtractTranslation<float>(world.Player().get<Eigen::Matrix4f>()) / z13::station::kCellSize;
  const std::vector<Eigen::Vector3f> inside {spawn, kSlowSpotM / z13::station::kCellSize};
  const std::vector<rooms::SpaceWalkStop> route = rooms::SpaceWalkRoute(HullOf(*blocks), inside);

  for (int tick = 0; tick < kWarmUpTicks; ++tick) {
    world.Tick();
  }
  ecs_measure_system_time(w, true);

  std::string stops_csv = std::format("stop,eye_x_m,eye_y_m,eye_z_m,ticks,{}\n", z13::kSystemSummaryCsvColumns);
  std::string systems_csv = std::format("stop,{}\n", z13::kSystemSummaryCsvColumns);
  for (const rooms::SpaceWalkStop& stop : route) {
    const StopResult result = MeasureStop(world, stop);
    const std::vector<z13::SystemSummary> systems = result.systems.Summary();
    const Eigen::Vector3f eye = MetersOf(stop.eye);
    std::cout << std::format("[ bench ] {:<9} eye {:7.2f} {:7.2f} {:6.2f} m, {} ticks\n", stop.label, eye.x(), eye.y(),
                             eye.z(), result.systems.Frames())
              << Describe(result.tick) << '\n';
    for (const z13::SystemSummary& system : std::span(systems).first(std::min(systems.size(), kTopSystems))) {
      std::cout << Describe(system) << '\n';
    }
    stops_csv += z13::SystemSummaryRows(
        std::format("{},{:.2f},{:.2f},{:.2f},{},", stop.label, eye.x(), eye.y(), eye.z(), result.systems.Frames()),
        {result.tick}, 0.0);
    systems_csv += z13::SystemSummaryRows(std::format("{},", stop.label), systems, kMinSystemMs);
  }
  ecs_measure_system_time(w, false);

  const std::filesystem::path dir = OutputDir();
  std::filesystem::create_directories(dir);
  std::ofstream(dir / kStopsFileName) << stops_csv;
  std::ofstream(dir / kSystemsFileName) << systems_csv;
  std::cout << std::format("[ bench ] CSV in {}\n", dir.string());
}

}  // namespace
}  // namespace z13::testing
