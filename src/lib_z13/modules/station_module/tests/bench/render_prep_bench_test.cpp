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

#include <primitives/chunk_mesh.h>
#include <primitives/decor.h>
#include <primitives/draw_order.h>
#include <primitives/station_assets.h>
#include <rooms/corridor_walk.h>
#include <rooms/room_builder.h>
#include <rooms/space_walk.h>
#include <rooms/tour_viewpoints.h>
#include <rooms/view_culling.h>
#include <z13/components/block.h>
#include <z13_settings/settings.h>
#include <z13_tests/camera_view.h>
#include <z13_tests/shipped_station.h>

namespace z13::testing {
namespace {

namespace primitives = z13::building::primitives;
namespace rooms = z13::station::rooms;

constexpr std::string_view kOutputDirEnv = "Z13_RENDER_PREP_DIR";
constexpr std::string_view kDefaultOutputDirName = "z13_render_prep";
constexpr std::string_view kCsvFileName = "viewpoints.csv";
// Each pick is timed this many times and the median kept.
constexpr int kRepeats = 5;
// How many rooms the corridor walk may enter, and how many the tour picks.
constexpr size_t kCorridorRooms = 40;
constexpr size_t kTourRooms = 8;
// A prop is culled by a box this far (meters) around its position: its model isn't loaded here.
constexpr float kPropHalfExtentM = 0.5f;
// Where the 19 fps were seen (release, 2026-10-22), in meters.
const Eigen::Vector3f kSlowSpotM {193.32f, 80.17f, 1.75f};

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

// A chunk as the renderer keeps it: where it is (meters) and what drawing it costs.
struct Chunk {
  Eigen::Vector3i key = Eigen::Vector3i::Zero();
  Eigen::AlignedBox3f bounds;
  size_t meshes {};
  size_t triangles {};
};

std::vector<Chunk> ChunksOf(
    const std::vector<z13::station::Block>& blocks, primitives::OptionalPalette palette, int chunk_cells) {
  std::vector<Chunk> chunks;
  for (const auto& [key, chunk_blocks] : primitives::GroupByChunk(blocks, chunk_cells)) {
    const primitives::ChunkMesh mesh = primitives::BuildChunkMesh(chunk_blocks, palette);
    Chunk chunk {.key = key,
                 .bounds = {mesh.bounds.min.cast<float>() * z13::station::kCellSize,
                            mesh.bounds.End().cast<float>() * z13::station::kCellSize},
                 .meshes = mesh.parts.size()};
    for (const primitives::MaterialMesh& part : mesh.parts) {
      chunk.triangles += part.mesh.positions.size() / 3;
    }
    chunks.push_back(chunk);
  }
  std::ranges::sort(chunks, [](const Chunk& a, const Chunk& b) {
    return std::lexicographical_compare(a.key.begin(), a.key.end(), b.key.begin(), b.key.end());
  });
  return chunks;
}

// The station as the renderer shows it: the blocks the props don't stand in for, split into opaque
// and glass chunks, and the props' boxes.
struct Scene {
  std::vector<Chunk> opaque;
  std::vector<Chunk> glass;
  std::vector<Eigen::AlignedBox3f> props;
};

Scene SceneOf(const std::vector<z13::station::Block>& all, const primitives::Palette& palette,
              const primitives::Decor& decor, int chunk_cells) {
  std::vector<uint32_t> hidden;
  for (const std::string& name : decor.replaces) {
    if (const auto primitive = palette.Find(name)) {
      hidden.push_back(primitive->get().id);
    }
  }
  std::vector<z13::station::Block> opaque;
  std::vector<z13::station::Block> glass;
  for (const z13::station::Block& block : all) {
    if (std::ranges::contains(hidden, block.spec.type_id)) {
      continue;
    }
    (primitives::IsTransparent(block, palette) ? glass : opaque).push_back(block);
  }
  Scene scene {.opaque = ChunksOf(opaque, palette, chunk_cells), .glass = ChunksOf(glass, palette, chunk_cells)};
  for (const primitives::Prop& prop : decor.props) {
    scene.props.push_back({prop.position.array() - kPropHalfExtentM, prop.position.array() + kPropHalfExtentM});
  }
  return scene;
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

struct Stop {
  std::string label;
  Eigen::Vector3f eye = Eigen::Vector3f::Zero();
  Eigen::Vector3f target = Eigen::Vector3f::Zero();
};

// What one view costs and draws.
struct Result {
  double culling_ms {};
  double pick_ms {};
  size_t seen_rooms {};
  size_t chunks_drawn {};
  size_t meshes_drawn {};
  size_t triangles_drawn {};
  size_t glass_drawn {};
  size_t props_drawn {};
};

Result Measure(const Scene& scene, const rooms::RoomGraph& graph, const Stop& stop) {
  const rooms::CameraView view =
      CameraViewFrom(stop.eye * z13::station::kCellSize, stop.target * z13::station::kCellSize);
  Result result;
  result.culling_ms = MedianMs([&] { const rooms::ViewCulling culling(view, std::cref(graph)); });
  const rooms::ViewCulling culling(view, std::cref(graph));
  result.seen_rooms = culling.SeenRoomCount().value_or(0);
  std::vector<primitives::ChunkBounds> visible_glass;
  result.pick_ms = MedianMs([&] {
    Result drawn;
    for (const Chunk& chunk : scene.opaque) {
      if (culling.Visible(chunk.bounds)) {
        ++drawn.chunks_drawn;
        drawn.meshes_drawn += chunk.meshes;
        drawn.triangles_drawn += chunk.triangles;
      }
    }
    visible_glass.clear();
    for (const Chunk& chunk : scene.glass) {
      if (culling.Visible(chunk.bounds)) {
        visible_glass.push_back({.key = chunk.key, .bounds = chunk.bounds});
        drawn.meshes_drawn += chunk.meshes;
        drawn.triangles_drawn += chunk.triangles;
      }
    }
    primitives::SortFarthestFirst(visible_glass, view.eye);
    drawn.glass_drawn = visible_glass.size();
    drawn.props_drawn = std::ranges::count_if(scene.props, [&](const auto& box) { return culling.Visible(box); });
    result.chunks_drawn = drawn.chunks_drawn;
    result.meshes_drawn = drawn.meshes_drawn;
    result.triangles_drawn = drawn.triangles_drawn;
    result.glass_drawn = drawn.glass_drawn;
    result.props_drawn = drawn.props_drawn;
  });
  return result;
}

std::filesystem::path OutputDir() {
  const char* dir = std::getenv(kOutputDirEnv.data());
  return dir != nullptr ? std::filesystem::path(dir) : std::filesystem::temp_directory_path() / kDefaultOutputDirName;
}

void AddStops(std::vector<Stop>& stops, std::string_view prefix, const std::vector<rooms::Viewpoint>& viewpoints) {
  for (size_t i = 0; i < viewpoints.size(); ++i) {
    stops.push_back(
        {.label = std::format("{}{}", prefix, i), .eye = viewpoints[i].eye, .target = viewpoints[i].target});
  }
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

  const Eigen::Vector3f slow_spot = kSlowSpotM / z13::station::kCellSize;
  std::vector<Stop> stops;
  AddStops(stops, "corridor", rooms::CorridorWalk(*graph, slow_spot, kCorridorRooms));
  AddStops(stops, "tour", rooms::TourViewpoints(*graph, kTourRooms));
  const std::vector<Eigen::Vector3f> inside {slow_spot};
  const std::vector<rooms::SpaceWalkStop> outside = rooms::SpaceWalkRoute(HullOf(*blocks), inside);
  for (const rooms::SpaceWalkStop& stop : std::span(outside).subspan(1)) {
    stops.push_back({.label = stop.label, .eye = stop.eye, .target = stop.target});
  }
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
        result.chunks_drawn, result.meshes_drawn, result.triangles_drawn, result.glass_drawn, result.props_drawn);
    csv += std::format(
        "{},{:.2f},{:.2f},{:.2f},{:.3f},{:.3f},{},{},{},{},{},{}\n", stop.label, eye.x(), eye.y(), eye.z(),
        result.culling_ms, result.pick_ms, result.seen_rooms, result.chunks_drawn, result.meshes_drawn,
        result.triangles_drawn, result.glass_drawn, result.props_drawn);
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
