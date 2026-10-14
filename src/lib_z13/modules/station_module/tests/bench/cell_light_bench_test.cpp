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

// Flooding the lamps' light (cell_light/cell_light.h) through every shipped station, whole and
// after a wall panel is built and taken away. Built by `make.py --bench`, run with
// `z13.py --bench --filter 'CellLightBench.*'`.

#include <gtest/gtest.h>

#include <chrono>
#include <format>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include <Eigen/Dense>

#include <cell_light/cell_light.h>
#include <primitives/draw_order.h>
#include <primitives/lights.h>
#include <primitives/placement.h>
#include <z13_tests/shipped_station.h>
#include <z13/components/block.h>

namespace z13::testing {
namespace {

namespace primitives = z13::building::primitives;
namespace light = z13::station::light;

constexpr double kBytesPerMb = 1024.0 * 1024.0;
// On the GPU, per node: light and direction, four half floats each.
constexpr size_t kNodeTextureBytes = 8 + 8;
// A wall panel, 1 m square and a cell thick, this far beside a lamp.
const Eigen::Vector3i kPanel {4, 1, 4};
const Eigen::Vector3i kBesideLamp {0, 8, -6};
// In the middle of spire's atrium (x 50-70 m, y 38-58 m), above deck 5's floor at 16 m.
const Eigen::Vector3i kSpireAtrium {240, 192, 66};

template <typename F>
double Ms(F&& work) {
  const auto start = std::chrono::steady_clock::now();
  work();
  return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

void TimePanel(light::CellLight& cell_light, const std::string& label, const Eigen::Vector3i& at) {
  const primitives::CellBox panel {.min = at, .extent = kPanel};
  if (!cell_light.Layout().CellBounds().Contains(at) ||
      cell_light.Labels()[cell_light.Layout().CellIndex(at)] == light::kOpaqueLabel) {
    return;
  }
  std::optional<light::LightChanges> built;
  const double build_ms = Ms([&] { built = cell_light.Apply({.opaque_added = {panel}}); });
  const double destroy_ms = Ms([&] { cell_light.Apply({.opaque_removed = {panel}}); });
  const Eigen::Vector3i lit = built->light ? built->light->extent : Eigen::Vector3i::Zero();
  std::cout << std::format("[ bench ]   panel {}: build {:.1f} ms, destroy {:.1f} ms, light changed in {}x{}x{} cubes\n",
                           label, build_ms, destroy_ms, lit.x(), lit.y(), lit.z())
            << std::flush;
}

TEST(CellLightBench, DISABLED_FloodAndEdit) {
  const primitives::Palette palette = ShippedPalette().value();
  for (const std::string& scene : ShippedScenes()) {
    const auto blocks = ShippedBlueprint(scene, palette);
    ASSERT_TRUE(blocks.has_value()) << blocks.error();
    std::vector<primitives::CellBox> opaque;
    for (const z13::station::Block& block : *blocks) {
      if (!primitives::IsTransparent(block, primitives::OptionalPalette(palette))) {
        opaque.push_back(primitives::OccupiedCells(block));
      }
    }
    std::vector<light::Lamp> lamps;
    for (const auto& point : primitives::LightsOf(*blocks, primitives::OptionalPalette(palette))) {
      lamps.push_back(light::LampOf(point));
    }
    std::optional<light::CellLight> cell_light;
    const double build_ms = Ms([&] { cell_light.emplace(opaque, lamps); });
    const light::LightLayout& layout = cell_light->Layout();
    const double labels_mb = static_cast<double>(layout.CellCount()) / kBytesPerMb;
    const double light_mb =
        static_cast<double>(layout.CubeCount() * light::kPiecesPerNode * kNodeTextureBytes) / kBytesPerMb;
    std::cout << std::format(
                     "[ bench ] '{}': {} lamps, {}x{}x{} cubes, {} with joined pieces; built in {:.0f} ms; "
                     "textures {:.1f} MB labels + {:.1f} MB light\n",
                     scene, lamps.size(), layout.cubes.x(), layout.cubes.y(), layout.cubes.z(),
                     cell_light->JoinedCubes(), build_ms, labels_mb, light_mb)
              << std::flush;
    if (lamps.empty()) {
      continue;
    }
    for (const size_t index : {size_t {0}, lamps.size() / 2, lamps.size() - 1}) {
      TimePanel(*cell_light, std::format("by lamp {}", index),
                lamps[index].position.array().floor().cast<int>().matrix() + kBesideLamp);
    }
    if (scene == "spire") {
      TimePanel(*cell_light, "in the atrium", kSpireAtrium);
    }
  }
}

}  // namespace
}  // namespace z13::testing
