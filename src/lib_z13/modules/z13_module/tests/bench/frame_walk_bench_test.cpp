// A bench, not a test (see docs/frame-time-plan.md). Run it with
//   z13_test_runner --gtest_also_run_disabled_tests --gtest_filter='FrameWalkBench.*'
// Frames per second on octant without a window: the player is moved a step each frame along the
// corridors of several rooms and around the outside of the station, the world ticks, and what the
// renderer's CPU picks to draw is done for the view (ViewCulling and the pick of chunks, glass and
// props; the GPU's share is not). Nothing paces the frames, as with the frame limit raised to 1000.
// CSV goes to $Z13_FRAME_WALK_DIR (default: <temp>/z13_frame_walk).

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <string>
#include <span>
#include <string_view>
#include <vector>

#include <Eigen/Dense>

#include <lib_core/utils/camera_pose.h>
#include <lib_core/utils/system_stats.h>
#include <lib_core/utils/system_times.h>
#include <primitives/decor.h>
#include <primitives/station_assets.h>
#include <rooms/corridor_walk.h>
#include <rooms/room_cache.h>
#include <rooms/space_walk.h>
#include <rooms/tour_viewpoints.h>
#include <rooms/view_culling.h>
#include <z13/components/block.h>
#include <z13_settings/settings.h>
#include <z13_tests/slow_spot.h>
#include <z13_tests/camera_view.h>
#include <z13_tests/shipped_station.h>
#include <z13_tests/virtual_render.h>

#include "../support/z13_test_world.h"

namespace z13::testing {
namespace {

namespace primitives = z13::building::primitives;
namespace rooms = z13::station::rooms;

constexpr std::string_view kOutputDirEnv = "Z13_FRAME_WALK_DIR";
constexpr std::string_view kDefaultOutputDirName = "z13_frame_walk";
constexpr std::string_view kCsvFileName = "legs.csv";
// What the world has built before the walk starts: bodies, rooms, the first snapshots.
constexpr int kWarmUpTicks = 250;
constexpr int kRuns = 3;
// The frames the player takes between two stops, and the ticks it settles after the first move.
constexpr int kFramesPerLeg = 30;
constexpr int kSettleTicks = 30;
// How many rooms a corridor walk enters, and how many rooms the tour starts others from.
constexpr size_t kCorridorRooms = 12;
constexpr size_t kTourRooms = 6;
// Every this many tour viewpoints starts one corridor walk (the tour turns four ways in a room).
constexpr size_t kTourHeadings = 4;
constexpr size_t kTopSystems = 8;
// Systems under this mean per frame are left out of the list.
constexpr double kMinSystemMs = 0.001;
// What a frame must stay under anywhere in the station, in Debug (docs/frame-time-plan.md): the
// network's rollbacks and the real drawing come on top of what is measured here.
constexpr double kTargetFps = 80.0;
constexpr double kBudgetMs = z13::kMsPerSecond / kTargetFps;
constexpr size_t kSlowestFrames = 12;

double Ms(std::chrono::steady_clock::duration duration) {
  return std::chrono::duration<double, std::milli>(duration).count();
}

struct Stop {
  Eigen::Vector3f eye = Eigen::Vector3f::Zero();
  Eigen::Vector3f target = Eigen::Vector3f::Zero();
};

// A walk between two stops, in cells.
struct Leg {
  std::string label;
  bool outside {};
  Stop from;
  Stop to;
};

void AddWalk(std::vector<Leg>& legs, const std::string& name, bool outside, const std::vector<Stop>& stops) {
  for (size_t i = 1; i < stops.size(); ++i) {
    legs.push_back({.label = std::format("{}.{}", name, i), .outside = outside, .from = stops[i - 1], .to = stops[i]});
  }
}

std::vector<Stop> StopsOf(const std::vector<rooms::Viewpoint>& viewpoints) {
  std::vector<Stop> stops;
  for (const rooms::Viewpoint& viewpoint : viewpoints) {
    stops.push_back({.eye = viewpoint.eye, .target = viewpoint.target});
  }
  return stops;
}

struct Frame {
  std::string leg;
  // The system that took the most of the tick, and how much.
  std::string top_system;
  double top_ms {};
  double tick_ms {};
  double culling_ms {};
  double pick_ms {};
  Drawn drawn;

  double Total() const { return tick_ms + culling_ms + pick_ms; }
};

double Mean(const std::vector<double>& values) {
  double sum = 0.0;
  for (const double value : values) {
    sum += value;
  }
  return values.empty() ? 0.0 : sum / static_cast<double>(values.size());
}

double Median(std::vector<double> values) {
  std::ranges::sort(values);
  return values.empty() ? 0.0 : values[values.size() / 2];
}

double Fps(double frame_ms) {
  return frame_ms > 0.0 ? 1000.0 / frame_ms : 0.0;
}

struct LegResult {
  std::string label;
  bool outside {};
  std::vector<Frame> frames;

  std::vector<double> Column(double Frame::* member) const {
    std::vector<double> column;
    for (const Frame& frame : frames) {
      column.push_back(frame.*member);
    }
    return column;
  }

  std::vector<double> Totals() const {
    std::vector<double> totals;
    for (const Frame& frame : frames) {
      totals.push_back(frame.Total());
    }
    return totals;
  }
};

std::string Summary(const std::string& label, const std::vector<LegResult>& legs, bool outside) {
  std::vector<double> totals;
  for (const LegResult& leg : legs) {
    if (leg.outside == outside) {
      const std::vector<double> leg_totals = leg.Totals();
      totals.insert(totals.end(), leg_totals.begin(), leg_totals.end());
    }
  }
  if (totals.empty()) {
    return {};
  }
  const double worst = *std::ranges::max_element(totals);
  const double best = *std::ranges::min_element(totals);
  return std::format("[ bench ] {:<14} {:5} frames: {:6.1f} fps on average, {:6.1f} median, {:6.1f} at the worst frame "
                     "({:.1f} ms), {:6.1f} at the best\n",
                     label, totals.size(), Fps(Mean(totals)), Fps(Median(totals)), Fps(worst), worst, Fps(best));
}

std::filesystem::path OutputDir() {
  const char* dir = std::getenv(kOutputDirEnv.data());
  return dir != nullptr ? std::filesystem::path(dir) : std::filesystem::temp_directory_path() / kDefaultOutputDirName;
}

TEST(FrameWalkBench, DISABLED_OctantWalk) {
  const auto palette = ShippedPalette();
  ASSERT_TRUE(palette.has_value()) << palette.error();
  const auto blocks = ShippedBlueprint(kOctantScene, *palette);
  ASSERT_TRUE(blocks.has_value()) << blocks.error();
  const auto decor_json = SourceAssets().ReadAsset(primitives::DecorFile(kOctantScene));
  ASSERT_TRUE(decor_json.has_value()) << decor_json.error();
  const auto decor = primitives::ParseDecor(*decor_json);
  ASSERT_TRUE(decor.has_value()) << decor.error();
  const Scene scene = SceneOf(*blocks, *palette, *decor, z13::RenderTuning().chunk_cells);

  Z13TestWorld world({std::string(kStationSceneArg), std::string(kOctantScene)});
  world.Tick();  // bootstrap starts the game and spawns the player
  flecs::world w = world.World();
  for (int tick = 0; tick < kWarmUpTicks; ++tick) {
    world.Tick();
  }
  const auto* cache = w.try_get<rooms::RoomCache>();
  ASSERT_NE(cache, nullptr);
  const rooms::OptionalRoomGraph current = cache->Current();
  ASSERT_TRUE(current.has_value());
  const rooms::RoomGraph& graph = current->get();

  const Eigen::Vector3f spawn =
      z13::math::ExtractTranslation<float>(world.Player().get<Eigen::Matrix4f>()) / z13::station::kCellSize;
  const Eigen::Vector3f slow_spot = kSlowSpotM / z13::station::kCellSize;
  std::vector<Leg> legs;
  AddWalk(legs, "corridor-slow", false, StopsOf(rooms::CorridorWalk(graph, slow_spot, kCorridorRooms)));
  AddWalk(legs, "corridor-spawn", false, StopsOf(rooms::CorridorWalk(graph, spawn, kCorridorRooms)));
  const std::vector<rooms::Viewpoint> tour = rooms::TourViewpoints(graph, kTourRooms);
  for (size_t i = 0; i < tour.size(); i += kTourHeadings) {
    AddWalk(legs, std::format("corridor-tour{}", i / kTourHeadings), false,
            StopsOf(rooms::CorridorWalk(graph, tour[i].eye, kCorridorRooms)));
  }
  const std::vector<Eigen::Vector3f> inside {slow_spot};
  std::vector<Stop> outside;
  outside.push_back({.eye = slow_spot, .target = slow_spot + Eigen::Vector3f::UnitX()});
  for (const rooms::SpaceWalkStop& stop : rooms::SpaceWalkRoute(HullOf(*blocks), inside)) {
    outside.push_back({.eye = stop.eye, .target = stop.target});
  }
  outside.erase(outside.begin() + 1);  // the route's first stop is the inside point just added
  AddWalk(legs, "outside", true, outside);
  ASSERT_FALSE(legs.empty());

  ecs_measure_system_time(w, true);
  z13::SystemFrameStats systems;
  std::vector<z13::SystemTime> before = z13::SystemTimes(w);
  bool moved = false;
  const auto walk_once = [&](bool first_run) {
    std::vector<LegResult> walked;
    for (const Leg& leg : legs) {
      LegResult result {.label = leg.label, .outside = leg.outside};
      for (int frame_index = 0; frame_index < kFramesPerLeg; ++frame_index) {
        const float t = static_cast<float>(frame_index + 1) / static_cast<float>(kFramesPerLeg);
        const Eigen::Vector3f eye = leg.from.eye + (t * (leg.to.eye - leg.from.eye));
        const Eigen::Vector3f target = leg.from.target + (t * (leg.to.target - leg.from.target));
        const Eigen::Vector3f eye_m = eye * z13::station::kCellSize;
        const Eigen::Vector3f target_m = target * z13::station::kCellSize;
        world.Player().set(PoseMatrix(z13::PoseLookingAt(eye_m, target_m)));
        if (!moved) {
          moved = true;
          for (int tick = 0; tick < kSettleTicks; ++tick) {
            world.Tick();
          }
          before = z13::SystemTimes(w);
        }

        Frame frame;
        const auto tick_start = std::chrono::steady_clock::now();
        world.Tick();
        frame.tick_ms = Ms(std::chrono::steady_clock::now() - tick_start);
        std::vector<z13::SystemTime> after = z13::SystemTimes(w);
        if (first_run) {
          systems.AddFrame(before, after);
        }
        const std::vector<z13::SystemTime> spent = z13::SystemTimesPerFrame(before, after, 1);
        frame.leg = leg.label;
        if (!spent.empty()) {
          frame.top_system = spent.front().path;
          frame.top_ms = spent.front().ms;
        }
        before = std::move(after);

        const rooms::CameraView view = CameraViewFrom(eye_m, target_m);
        const auto culling_start = std::chrono::steady_clock::now();
        const rooms::ViewCulling culling(view, std::cref(graph));
        frame.culling_ms = Ms(std::chrono::steady_clock::now() - culling_start);
        const auto pick_start = std::chrono::steady_clock::now();
        frame.drawn = PickVisible(scene, culling, view.eye);
        frame.pick_ms = Ms(std::chrono::steady_clock::now() - pick_start);
        result.frames.push_back(frame);
      }
      walked.push_back(std::move(result));
    }
    return walked;
  };
  // The walk is repeated and each frame keeps its fastest run: the phone's scheduler stalls a frame
  // here and there for tens of ms, while a spike of our own (a snapshot, a rollback) comes back
  // at the same frame of every run (a leg is two snapshot intervals long).
  std::vector<LegResult> results = walk_once(true);
  for (int run = 1; run < kRuns; ++run) {
    const std::vector<LegResult> again = walk_once(false);
    for (size_t leg = 0; leg < results.size(); ++leg) {
      for (size_t frame = 0; frame < results[leg].frames.size(); ++frame) {
        if (again[leg].frames[frame].Total() < results[leg].frames[frame].Total()) {
          results[leg].frames[frame] = again[leg].frames[frame];
        }
      }
    }
  }
  ecs_measure_system_time(w, false);

  std::string csv = "leg,outside,frames,tick_ms,culling_ms,pick_ms,frame_ms,fps_mean,fps_worst,meshes,props\n";
  for (const LegResult& leg : results) {
    const double tick_ms = Mean(leg.Column(&Frame::tick_ms));
    const double culling_ms = Mean(leg.Column(&Frame::culling_ms));
    const double pick_ms = Mean(leg.Column(&Frame::pick_ms));
    const std::vector<double> totals = leg.Totals();
    const double frame_ms = Mean(totals);
    const double worst_ms = *std::ranges::max_element(totals);
    const Drawn& drawn = leg.frames.back().drawn;
    std::cout << std::format("[ bench ] {:<18} tick {:6.2f}  culling {:6.2f}  pick {:6.2f}  = {:6.2f} ms: {:6.1f} fps "
                             "({:6.1f} worst)  meshes {:5}  props {:4}\n",
                             leg.label, tick_ms, culling_ms, pick_ms, frame_ms, Fps(frame_ms), Fps(worst_ms),
                             drawn.meshes, drawn.props);
    csv += std::format("{},{},{},{:.3f},{:.3f},{:.3f},{:.3f},{:.1f},{:.1f},{},{}\n", leg.label, leg.outside,
                       leg.frames.size(), tick_ms, culling_ms, pick_ms, frame_ms, Fps(frame_ms), Fps(worst_ms),
                       drawn.meshes, drawn.props);
  }
  std::cout << Summary("corridors, rooms", results, false) << Summary("outside", results, true);
  std::vector<const Frame*> frames;
  for (const LegResult& leg : results) {
    for (const Frame& frame : leg.frames) {
      frames.push_back(&frame);
    }
  }
  std::ranges::sort(frames, [](const Frame* a, const Frame* b) { return a->Total() > b->Total(); });
  const auto over = std::ranges::count_if(frames, [](const Frame* frame) { return frame->Total() > kBudgetMs; });
  std::cout << std::format("[ bench ] frames over the {:.0f} fps budget ({:.1f} ms): {} of {}\n", kTargetFps, kBudgetMs,
                           over, frames.size());
  EXPECT_EQ(over, 0) << "frames slower than the " << kTargetFps << " fps budget";
  for (const Frame* frame : std::span(frames).first(std::min(frames.size(), kSlowestFrames))) {
    std::cout << std::format(
        "    {:<18} {:6.2f} ms ({:5.1f} fps): tick {:5.2f}  culling {:5.2f}  pick {:5.2f}  worst system {} {:.2f} ms\n",
        frame->leg, frame->Total(), Fps(frame->Total()), frame->tick_ms, frame->culling_ms, frame->pick_ms,
        frame->top_system, frame->top_ms);
  }
  const std::vector<z13::SystemSummary> top = systems.Summary();
  std::cout << "[ bench ] the systems that took the most per frame over the whole walk:\n";
  for (const z13::SystemSummary& system : std::span(top).first(std::min(top.size(), kTopSystems))) {
    if (system.mean_ms >= kMinSystemMs) {
      std::cout << std::format("    {:<64} mean {:7.3f}  max {:7.3f} ms\n", system.path, system.mean_ms, system.max_ms);
    }
  }

  const std::filesystem::path dir = OutputDir();
  std::filesystem::create_directories(dir);
  std::ofstream(dir / kCsvFileName) << csv;
  std::cout << std::format("[ bench ] CSV in {}\n", dir.string());
}

}  // namespace
}  // namespace z13::testing
