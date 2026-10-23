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
//   z13_test_runner --gtest_also_run_disabled_tests --gtest_filter='PropsCullingBench.*'
// Picking the props in view along RenderPrepBench's route, repeated 1, 10 and 100 times, by
// ViewCulling::Visible on each (the renderer before BoxTree), a Boost.Geometry R-tree and BoxTree.
// DynamicProps moves them every frame and times updating the structure plus picking. Every way
// must pick the same props.

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <format>
#include <iostream>
#include <memory>
#include <numbers>
#include <random>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <boost/function_output_iterator.hpp>
#include <boost/geometry.hpp>
#include <boost/geometry/index/rtree.hpp>
#include <Eigen/Dense>

#include <primitives/decor.h>
#include <primitives/station_assets.h>
#include <rooms/box_tree.h>
#include <rooms/room_builder.h>
#include <rooms/view_culling.h>
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
namespace bg = boost::geometry;
namespace bgi = boost::geometry::index;

using Point = bg::model::point<float, 3, bg::cs::cartesian>;
using Box = bg::model::box<Point>;
using Entry = std::pair<Box, uint32_t>;
using Rtree = bgi::rtree<Entry, bgi::rstar<16>>;

// Each timing is repeated this many times and the median kept.
constexpr int kRepeats = 9;
// How far (meters) a repeated prop is moved from the original, so the density follows the station.
constexpr float kScatterM = 4.f;
constexpr std::array kRepeatCounts {1, 10, 100};

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

Box ToBox(const Eigen::AlignedBox3f& box) {
  return {Point(box.min().x(), box.min().y(), box.min().z()), Point(box.max().x(), box.max().y(), box.max().z())};
}

// The frustum's bounding box in meters: the view-projection's inverse applied to the clip cube.
Eigen::AlignedBox3f FrustumBounds(const Eigen::Matrix4f& view_projection) {
  const Eigen::Matrix4f inverse = view_projection.inverse();
  Eigen::AlignedBox3f bounds;
  for (const float x : {-1.f, 1.f}) {
    for (const float y : {-1.f, 1.f}) {
      for (const float z : {-1.f, 1.f}) {
        const Eigen::Vector4f corner = inverse * Eigen::Vector4f(x, y, z, 1.f);
        bounds.extend((corner / corner.w()).head<3>());
      }
    }
  }
  return bounds;
}

std::vector<Eigen::AlignedBox3f> Repeated(const std::vector<Eigen::AlignedBox3f>& boxes, int times) {
  std::mt19937 random(7);
  std::uniform_real_distribution<float> scatter(-kScatterM, kScatterM);
  std::vector<Eigen::AlignedBox3f> result = boxes;
  for (int copy = 1; copy < times; ++copy) {
    for (const Eigen::AlignedBox3f& box : boxes) {
      const Eigen::Vector3f offset(scatter(random), scatter(random), 0.f);
      result.emplace_back(box.min() + offset, box.max() + offset);
    }
  }
  return result;
}

double LooseRatio() {
  return z13::RenderTuning().prop_tree_loose_ratio;
}

std::vector<uint32_t> LinearPick(const std::vector<Eigen::AlignedBox3f>& boxes, const rooms::ViewCulling& culling) {
  std::vector<uint32_t> visible;
  for (uint32_t i = 0; i < boxes.size(); ++i) {
    if (culling.Visible(boxes[i])) {
      visible.push_back(i);
    }
  }
  return visible;
}

std::vector<uint32_t> RtreePick(
    const Rtree& tree, const std::vector<Eigen::AlignedBox3f>& boxes, const rooms::ViewCulling& culling,
    const Eigen::AlignedBox3f& frustum) {
  std::vector<uint32_t> visible;
  tree.query(bgi::intersects(ToBox(frustum)), boost::make_function_output_iterator([&](const Entry& entry) {
               if (culling.Visible(boxes[entry.second])) {
                 visible.push_back(entry.second);
               }
             }));
  return visible;
}

struct OctantProps {
  std::vector<z13::station::Block> blocks;
  rooms::RoomGraph graph;
  std::vector<Eigen::AlignedBox3f> props;
};

std::optional<OctantProps> LoadOctantProps() {
  const auto palette = ShippedPalette();
  const auto blocks = palette ? ShippedBlueprint(kOctantScene, *palette) : std::unexpected(palette.error());
  const auto decor_json = SourceAssets().ReadAsset(primitives::DecorFile(kOctantScene));
  const auto decor = decor_json ? primitives::ParseDecor(*decor_json) : std::unexpected(decor_json.error());
  if (!blocks || !decor) {
    return std::nullopt;
  }
  auto graph = rooms::BuildRooms(RoomSourcesOf(*blocks, *palette));
  if (!graph) {
    return std::nullopt;
  }
  Scene scene = SceneOf(*blocks, *palette, *decor, z13::RenderTuning().chunk_cells);
  return OctantProps {.blocks = *blocks, .graph = *std::move(graph), .props = std::move(scene.props)};
}

struct Timings {
  double linear_ms {};
  double rtree_ms {};
  double tree_ms {};
  size_t found {};
};

TEST(PropsCullingBench, DISABLED_OctantWalk) {
  const auto octant = LoadOctantProps();
  ASSERT_TRUE(octant.has_value());
  const std::vector<Stop> stops = OctantRoute(octant->graph, octant->blocks);
  ASSERT_GT(stops.size(), 1U);

  Timings previous;
  for (const int times : kRepeatCounts) {
    const std::vector<Eigen::AlignedBox3f> boxes = Repeated(octant->props, times);
    const auto build_start = std::chrono::steady_clock::now();
    const rooms::BoxTree tree(boxes, LooseRatio());
    const double tree_build_ms = Ms(std::chrono::steady_clock::now() - build_start);
    const auto rtree_start = std::chrono::steady_clock::now();
    std::vector<Entry> entries;
    for (uint32_t i = 0; i < boxes.size(); ++i) {
      entries.emplace_back(ToBox(boxes[i]), i);
    }
    const Rtree rtree(entries.begin(), entries.end());
    const double rtree_build_ms = Ms(std::chrono::steady_clock::now() - rtree_start);
    std::cout << std::format(
        "[ bench ] x{}: {} props; built BoxTree {:.2f} ms, R-tree {:.2f} ms\n", times, boxes.size(), tree_build_ms,
        rtree_build_ms);

    Timings total;
    Timings worst;
    for (const Stop& stop : stops) {
      const rooms::CameraView view =
          CameraViewFrom(stop.eye * z13::station::kCellSize, stop.target * z13::station::kCellSize);
      const rooms::ViewCulling culling(view, std::cref(octant->graph));
      const Eigen::AlignedBox3f frustum = FrustumBounds(view.view_projection);

      std::vector<uint32_t> expected = LinearPick(boxes, culling);
      std::vector<uint32_t> from_rtree = RtreePick(rtree, boxes, culling, frustum);
      std::vector<uint32_t> from_tree;
      tree.Query(culling, from_tree);
      std::ranges::sort(from_rtree);
      std::ranges::sort(from_tree);
      EXPECT_EQ(from_rtree, expected) << stop.label;
      EXPECT_EQ(from_tree, expected) << stop.label;

      Timings timings {.found = expected.size()};
      timings.linear_ms = MedianMs([&] { auto picked = LinearPick(boxes, culling); });
      timings.rtree_ms = MedianMs([&] { auto picked = RtreePick(rtree, boxes, culling, frustum); });
      timings.tree_ms = MedianMs([&] {
        std::vector<uint32_t> picked;
        tree.Query(culling, picked);
      });
      total.linear_ms += timings.linear_ms;
      total.rtree_ms += timings.rtree_ms;
      total.tree_ms += timings.tree_ms;
      total.found += timings.found;
      if (timings.linear_ms > worst.linear_ms) {
        worst = timings;
      }
    }
    const auto count = static_cast<double>(stops.size());
    std::cout << std::format(
        "[ bench ] x{}: mean over {} views  linear {:.3f}  R-tree {:.3f}  BoxTree {:.3f} ms; slowest linear view "
        "{:.3f} (R-tree {:.3f}, BoxTree {:.3f}, {} props)\n",
        times, stops.size(), total.linear_ms / count, total.rtree_ms / count, total.tree_ms / count, worst.linear_ms,
        worst.rtree_ms, worst.tree_ms, worst.found);
    if (previous.linear_ms > 0.0) {
      std::cout << std::format(
          "[ bench ]   growth over the previous scale (10x the props): linear x{:.1f}  R-tree x{:.1f}  BoxTree x{:.1f}\n",
          total.linear_ms / previous.linear_ms, total.rtree_ms / previous.rtree_ms,
          total.tree_ms / previous.tree_ms);
    }
    previous = total;
  }
}

// One way to keep props picked while they move: it updates its structure after the props moved
// and then picks the props in view.
class Strategy {
 public:
  virtual ~Strategy() = default;
  virtual std::string Name() const = 0;
  virtual void Update(const std::vector<uint32_t>& moved, const std::vector<Eigen::AlignedBox3f>& boxes) = 0;
  virtual std::vector<uint32_t> Pick(
      const std::vector<Eigen::AlignedBox3f>& boxes, const rooms::ViewCulling& culling) = 0;
  virtual size_t Rebuilds() const { return 0; }
};

class LinearStrategy : public Strategy {
 public:
  std::string Name() const override { return "linear"; }
  void Update(const std::vector<uint32_t>&, const std::vector<Eigen::AlignedBox3f>&) override {}
  std::vector<uint32_t> Pick(const std::vector<Eigen::AlignedBox3f>& boxes, const rooms::ViewCulling& culling) override {
    return LinearPick(boxes, culling);
  }
};

class TreeStrategy : public Strategy {
 public:
  // kRebuild builds a new tree every frame; kUpdate lets the tree refit and rebuild itself.
  enum class Mode { kRebuild, kUpdate };

  TreeStrategy(Mode mode, const std::vector<Eigen::AlignedBox3f>& boxes)
      : mode_(mode), tree_(boxes, LooseRatio()) {}

  std::string Name() const override { return mode_ == Mode::kRebuild ? "rebuild each frame" : "BoxTree::Update"; }

  void Update(const std::vector<uint32_t>& moved, const std::vector<Eigen::AlignedBox3f>& boxes) override {
    if (mode_ == Mode::kRebuild) {
      tree_ = rooms::BoxTree(boxes, LooseRatio());
      ++rebuilt_;
    } else {
      tree_.Update(moved, boxes);
    }
  }

  std::vector<uint32_t> Pick(const std::vector<Eigen::AlignedBox3f>&, const rooms::ViewCulling& culling) override {
    std::vector<uint32_t> visible;
    tree_.Query(culling, visible);
    return visible;
  }

  size_t Rebuilds() const override { return mode_ == Mode::kRebuild ? rebuilt_ : tree_.Rebuilds(); }

 private:
  Mode mode_;
  rooms::BoxTree tree_;
  size_t rebuilt_ {};
};

// What a case moves: how many props, how far (meters) they travel there and back over the run.
struct DynamicCase {
  int times {};
  float share {};
  float travel_m {};
  int frames {};
};

// Furniture pushed about, and debris thrown fast.
constexpr float kNudgeM = 2.f;
constexpr float kFlungM = 30.f;
constexpr std::array kDynamicCases {
    DynamicCase {.times = 1, .share = 0.1f, .travel_m = kNudgeM, .frames = 300},
    DynamicCase {.times = 1, .share = 1.f, .travel_m = kFlungM, .frames = 300},
    DynamicCase {.times = 10, .share = 0.1f, .travel_m = kNudgeM, .frames = 200},
    DynamicCase {.times = 10, .share = 0.1f, .travel_m = kFlungM, .frames = 200},
    DynamicCase {.times = 10, .share = 1.f, .travel_m = kFlungM, .frames = 200},
    DynamicCase {.times = 100, .share = 0.1f, .travel_m = kNudgeM, .frames = 20},
    DynamicCase {.times = 100, .share = 1.f, .travel_m = kFlungM, .frames = 20},
};
// The views the frames are picked from, one a frame in turn.
constexpr std::array kDynamicStops {"corridor3", "tour33", "overview"};
// Every this many frames each strategy's picking is checked against the linear one.
constexpr int kCheckEvery = 25;

// 0 to 1 and back to 0 over the run.
float OutAndBack(int frame, int frames) {
  const float phase = static_cast<float>(frame) / static_cast<float>(frames);
  return 1.f - std::abs((2.f * phase) - 1.f);
}

struct FrameStats {
  double update_ms {};
  double pick_ms {};
  double worst_ms {};
};

TEST(PropsCullingBench, DISABLED_DynamicProps) {
  const auto octant = LoadOctantProps();
  ASSERT_TRUE(octant.has_value());
  const std::vector<Stop> stops = OctantRoute(octant->graph, octant->blocks);
  std::vector<const Stop*> views;
  for (const Stop& stop : stops) {
    if (std::ranges::contains(kDynamicStops, stop.label)) {
      views.push_back(&stop);
    }
  }
  ASSERT_EQ(views.size(), kDynamicStops.size());

  for (const DynamicCase& dynamic : kDynamicCases) {
    const std::vector<Eigen::AlignedBox3f> base = Repeated(octant->props, dynamic.times);
    std::vector<Eigen::AlignedBox3f> boxes = base;
    std::mt19937 random(11);
    std::uniform_real_distribution<float> unit(0.f, 1.f);
    std::vector<uint32_t> moved;
    std::vector<Eigen::Vector3f> direction(base.size(), Eigen::Vector3f::Zero());
    for (uint32_t i = 0; i < base.size(); ++i) {
      if (unit(random) < dynamic.share) {
        moved.push_back(i);
        const float angle = unit(random) * 2.f * std::numbers::pi_v<float>;
        direction[i] = Eigen::Vector3f(std::cos(angle), std::sin(angle), 0.f) * unit(random);
      }
    }

    std::vector<std::unique_ptr<Strategy>> strategies;
    strategies.push_back(std::make_unique<LinearStrategy>());
    strategies.push_back(std::make_unique<TreeStrategy>(TreeStrategy::Mode::kRebuild, boxes));
    strategies.push_back(std::make_unique<TreeStrategy>(TreeStrategy::Mode::kUpdate, boxes));
    std::vector<FrameStats> stats(strategies.size());

    std::cout << std::format(
        "[ bench ] x{}: {} props, {} move up to {} m out and back over {} frames\n", dynamic.times, base.size(),
        moved.size(), dynamic.travel_m, dynamic.frames) << std::flush;
    for (int frame = 0; frame < dynamic.frames; ++frame) {
      const float travel = dynamic.travel_m * OutAndBack(frame, dynamic.frames);
      for (const uint32_t id : moved) {
        boxes[id] = base[id];
        boxes[id].translate(direction[id] * travel);
      }
      const Stop& stop = *views[static_cast<size_t>(frame) % views.size()];
      const rooms::CameraView view =
          CameraViewFrom(stop.eye * z13::station::kCellSize, stop.target * z13::station::kCellSize);
      const rooms::ViewCulling culling(view, std::cref(octant->graph));
      const std::vector<uint32_t> expected = LinearPick(boxes, culling);
      for (size_t s = 0; s < strategies.size(); ++s) {
        const auto update_start = std::chrono::steady_clock::now();
        strategies[s]->Update(moved, boxes);
        const auto pick_start = std::chrono::steady_clock::now();
        std::vector<uint32_t> picked = strategies[s]->Pick(boxes, culling);
        const auto end = std::chrono::steady_clock::now();
        stats[s].update_ms += Ms(pick_start - update_start);
        stats[s].pick_ms += Ms(end - pick_start);
        stats[s].worst_ms = std::max(stats[s].worst_ms, Ms(end - update_start));
        if (frame % kCheckEvery == 0) {
          std::ranges::sort(picked);
          EXPECT_EQ(picked, expected) << strategies[s]->Name() << " frame " << frame;
        }
      }
    }
    const auto frames = static_cast<double>(dynamic.frames);
    for (size_t s = 0; s < strategies.size(); ++s) {
      std::cout << std::format(
          "[ bench ]   {:<20} update {:9.3f}  pick {:8.3f}  frame {:9.3f} ms (worst {:9.3f}), {} rebuilds\n",
          strategies[s]->Name(), stats[s].update_ms / frames, stats[s].pick_ms / frames,
          (stats[s].update_ms + stats[s].pick_ms) / frames, stats[s].worst_ms, strategies[s]->Rebuilds())
                << std::flush;
    }
  }
}

}  // namespace
}  // namespace z13::testing
