// A bench, not a test (see docs/frame-time-plan.md). Run it with
//   z13_test_runner --gtest_also_run_disabled_tests --gtest_filter='RenderPrepBench.*'
// A "virtual render" of octant: along a walk through its corridors and out around it, what the
// renderer's CPU does before it draws (the room visibility and frustum of ViewCulling, picking the
// chunks, glass and props in view), with the work that picking leaves the GPU counted instead of
// done: chunks, meshes (draw calls) and triangles drawn. CSV goes to $Z13_RENDER_PREP_DIR
// (default: <temp>/z13_render_prep).

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include <Eigen/Dense>

#include <primitives/decor.h>
#include <primitives/station_assets.h>
#include <rooms/corridor_walk.h>
#include <rooms/room_builder.h>
#include <rooms/space_walk.h>
#include <rooms/tour_viewpoints.h>
#include <rooms/room_visibility.h>
#include <rooms/view_culling.h>
#include <lib_core/utils/frustum.h>
#include <z13/components/block.h>
#include <z13_settings/settings.h>
#include <z13_tests/camera_view.h>
#include <z13_tests/octant_route.h>
#include <z13_tests/shipped_station.h>
#include <z13_tests/virtual_render.h>

namespace z13::testing {
namespace {

namespace primitives = z13::building::primitives;
namespace rooms = z13::station::rooms;

constexpr std::string_view kOutputDirEnv = "Z13_RENDER_PREP_DIR";
constexpr std::string_view kDefaultOutputDirName = "z13_render_prep";
constexpr std::string_view kCsvFileName = "viewpoints.csv";
// Each pick is timed this many times and the median kept.
constexpr int kRepeats = 5;

double Ms(std::chrono::steady_clock::duration duration) {
  return std::chrono::duration<double, std::milli>(duration).count();
}

template <class Work>
double MedianMs(Work&& work) {
  std::vector<double> times;
  for (int i = 0; i < kRepeats; ++i) {
    const auto start = std::chrono::steady_clock::now();
    work();
    times.push_back(Ms(std::chrono::steady_clock::now() - start));
  }
  std::ranges::sort(times);
  return times[times.size() / 2];
}

// What one view costs and draws.
struct Result {
  double culling_ms {};
  double pick_ms {};
  size_t seen_rooms {};
  Drawn drawn;
};

Result Measure(const Scene& scene, const rooms::RoomGraph& graph, const Stop& stop) {
  const rooms::CameraView view =
      CameraViewFrom(stop.eye * z13::station::kCellSize, stop.target * z13::station::kCellSize);
  Result result;
  result.culling_ms = MedianMs([&] { const rooms::ViewCulling culling(view, std::cref(graph)); });
  const rooms::ViewCulling culling(view, std::cref(graph));
  result.seen_rooms = culling.SeenRoomCount().value_or(0);
  result.pick_ms = MedianMs([&] { result.drawn = PickVisible(scene, culling, view.eye); });
  return result;
}

// Where the culling time of a view goes: the walk over the portals (room_visibility.h), which the
// ViewCulling constructor does first, against the whole constructor, and how many portals the
// walk can touch.
struct CullingBreakdown {
  double walk_ms {};
  size_t portals {};
};

CullingBreakdown BreakDownCulling(const rooms::RoomGraph& graph, const Stop& stop) {
  const rooms::CameraView view =
      CameraViewFrom(stop.eye * z13::station::kCellSize, stop.target * z13::station::kCellSize);
  const auto eye_room = graph.RoomAt(stop.eye.array().floor().cast<int>());
  CullingBreakdown breakdown;
  if (!eye_room) {
    return breakdown;
  }
  const Eigen::Matrix4f cells_to_clip =
      view.view_projection * Eigen::Affine3f(Eigen::Scaling(z13::station::kCellSize)).matrix();
  const rooms::RoomView room_view {.eye_room = *eye_room, .eye = stop.eye, .view_projection = cells_to_clip,
                                   .screen = z13::math::Frustum::FullScreen(), .min_extent = Eigen::Vector2f::Zero()};
  rooms::ScreenRegions regions;
  breakdown.walk_ms = MedianMs([&] { regions = rooms::VisibleRooms(graph, room_view); });
  for (const auto& [room, region] : regions) {
    for (const size_t index : graph.PortalsOf(room)) {
      breakdown.portals += graph.portals[index].visible ? 1 : 0;
    }
  }
  return breakdown;
}

std::filesystem::path OutputDir() {
  const char* dir = std::getenv(kOutputDirEnv.data());
  return dir != nullptr ? std::filesystem::path(dir) : std::filesystem::temp_directory_path() / kDefaultOutputDirName;
}

TEST(RenderPrepBench, DISABLED_OctantWalk) {
  const auto palette = ShippedPalette();
  ASSERT_TRUE(palette.has_value()) << palette.error();
  const auto blocks = ShippedBlueprint(kOctantScene, *palette);
  ASSERT_TRUE(blocks.has_value()) << blocks.error();
  const auto decor_json = SourceAssets().ReadAsset(primitives::DecorFile(kOctantScene));
  ASSERT_TRUE(decor_json.has_value()) << decor_json.error();
  const auto decor = primitives::ParseDecor(*decor_json);
  ASSERT_TRUE(decor.has_value()) << decor.error();

  const auto rooms_start = std::chrono::steady_clock::now();
  const auto graph = rooms::BuildRooms(RoomSourcesOf(*blocks, *palette));
  ASSERT_TRUE(graph.has_value()) << graph.error();
  const double rooms_ms = Ms(std::chrono::steady_clock::now() - rooms_start);
  const auto scene_start = std::chrono::steady_clock::now();
  const Scene scene = SceneOf(*blocks, *palette, *decor, z13::RenderTuning().chunk_cells);
  const double scene_ms = Ms(std::chrono::steady_clock::now() - scene_start);
  std::cout << std::format(
      "[ bench ] rooms built in {:.0f} ms ({} rooms, {} portals); {} opaque and {} glass chunks, {} props built in "
      "{:.0f} ms\n",
      rooms_ms, graph->rooms.size(), graph->portals.size(), scene.opaque.size(), scene.glass.size(),
      scene.props.size(), scene_ms);

  const std::vector<Stop> stops = OctantRoute(*graph, *blocks);
  ASSERT_GT(stops.size(), 1U);

  std::string csv = "stop,eye_x_m,eye_y_m,eye_z_m,culling_ms,pick_ms,seen_rooms,chunks,meshes,triangles,glass,props\n";
  double worst_total_ms = 0.0;
  std::string worst;
  for (const Stop& stop : stops) {
    const Result result = Measure(scene, *graph, stop);
    const Eigen::Vector3f eye = stop.eye * z13::station::kCellSize;
    std::cout << std::format(
        "[ bench ] {:<11} eye {:7.2f} {:7.2f} {:6.2f} m  culling {:7.3f} ms  pick {:7.3f} ms  rooms {:4}  chunks {:4}  "
        "meshes {:5}  triangles {:8}  glass {:3}  props {:4}\n",
        stop.label, eye.x(), eye.y(), eye.z(), result.culling_ms, result.pick_ms, result.seen_rooms,
        result.drawn.chunks, result.drawn.meshes, result.drawn.triangles, result.drawn.glass, result.drawn.props);
    csv += std::format(
        "{},{:.2f},{:.2f},{:.2f},{:.3f},{:.3f},{},{},{},{},{},{}\n", stop.label, eye.x(), eye.y(), eye.z(),
        result.culling_ms, result.pick_ms, result.seen_rooms, result.drawn.chunks, result.drawn.meshes,
        result.drawn.triangles, result.drawn.glass, result.drawn.props);
    if (stop.label.starts_with("orbit") || stop.label == "overview" || stop.label == "exit") {
      const CullingBreakdown breakdown = BreakDownCulling(*graph, stop);
      std::cout << std::format(
          "[ bench ]   culling: the walk over portals {:7.3f} ms of {:7.3f}, {} visible portals in the rooms it saw\n",
          breakdown.walk_ms, result.culling_ms, breakdown.portals);
    }
    if (result.culling_ms + result.pick_ms > worst_total_ms) {
      worst_total_ms = result.culling_ms + result.pick_ms;
      worst = stop.label;
    }
  }
  std::cout << std::format("[ bench ] slowest view: {} ({:.3f} ms of culling and picking)\n", worst, worst_total_ms);

  const std::filesystem::path dir = OutputDir();
  std::filesystem::create_directories(dir);
  std::ofstream(dir / kCsvFileName) << csv;
  std::cout << std::format("[ bench ] CSV in {}\n", dir.string());
}

}  // namespace
}  // namespace z13::testing
