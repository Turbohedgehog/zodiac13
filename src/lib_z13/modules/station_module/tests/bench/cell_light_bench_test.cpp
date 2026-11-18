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

#include <algorithm>
#include <numeric>
#include <tuple>
#include <array>
#include <chrono>
#include <functional>
#include <format>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include <Eigen/Dense>

#include <cell_light/cell_light.h>
#include <cell_light/light_edits.h>
#include <primitives/draw_order.h>
#include <primitives/lights.h>
#include <primitives/occupancy.h>
#include <primitives/placement.h>
#include <z13_tests/shipped_station.h>
#include <lib_core/utils/thread_pool.h>
#include <z13/components/station/block.h>
#include <z13_settings/building_tuning.h>

#include "../../../building_module/src/blueprint_walls.h"

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
  std::cout << std::format("[ bench ]   panel {}: build {:.1f} ms, destroy {:.1f} ms, light changed in {} tiles\n",
                           label, build_ms, destroy_ms, built->light_tiles.size())
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

// What CellLighting::Sync hands the flood: the cells of the blocks' solids, glass apart.
struct SealedParts {
  std::vector<primitives::CellBox> opaque;
  std::vector<light::GlassBox> glass;
};

SealedParts SealedPartsOf(const std::vector<z13::station::Block>& blocks, const primitives::Palette& palette) {
  SealedParts parts;
  for (const z13::station::Block& block : blocks) {
    const std::vector<primitives::CellBox> boxes = primitives::SealedBoxes(block, primitives::OptionalPalette(palette));
    if (primitives::IsTransparent(block, primitives::OptionalPalette(palette))) {
      const float transmission = palette.Find(block.spec.type_id)->get().light_transmission;
      for (const primitives::CellBox& cells : boxes) {
        parts.glass.push_back({.cells = cells, .transmission = transmission});
      }
    } else {
      parts.opaque.insert(parts.opaque.end(), boxes.begin(), boxes.end());
    }
  }
  return parts;
}

// Opening and shutting each door of a scene on the flooded light, as SyncCellLighting does (the doors left out of the
// lit blocks).
TEST(CellLightBench, DISABLED_Doors) {
  const primitives::Palette palette = ShippedPalette().value();
  for (const std::string& scene : ShippedScenes()) {
    const auto blocks = ShippedBlueprint(scene, palette);
    ASSERT_TRUE(blocks.has_value()) << blocks.error();
    const SealedParts parts = SealedPartsOf(*blocks, palette);
    std::vector<light::Lamp> lamps;
    for (const auto& point : primitives::LightsOf(*blocks, primitives::OptionalPalette(palette))) {
      lamps.push_back(light::LampOf(point));
    }
    const double parts_ms = Ms([&] { std::ignore = SealedPartsOf(*blocks, palette); });
    light::CellLight cell_light(parts.opaque, parts.glass, lamps);
    std::vector<double> opens;
    std::vector<double> shuts;
    for (const z13::station::Block& block : *blocks) {
      const auto primitive = palette.Find(block.spec.type_id);
      if (!primitive || !primitive->get().IsDoor() || opens.size() >= 40) {
        continue;
      }
      const std::vector<primitives::CellBox> boxes = primitives::SealedBoxes(block, primitives::OptionalPalette(palette));
      opens.push_back(Ms([&] { cell_light.Apply({.opaque_removed = boxes}); }));
      shuts.push_back(Ms([&] { cell_light.Apply({.opaque_added = boxes}); }));
    }
    std::ranges::sort(opens);
    std::ranges::sort(shuts);
    if (opens.empty()) {
      continue;
    }
    std::cout << std::format("[ bench ] '{}': {} doors, open median {:.1f} max {:.1f} ms, shut median {:.1f} max {:.1f} ms; sealed parts of all {} blocks {:.1f} ms\n",
                             scene, opens.size(), opens[opens.size() / 2], opens.back(), shuts[shuts.size() / 2],
                             shuts.back(), blocks->size(), parts_ms)
              << std::flush;
  }
}

// The blueprint's blocks flooded first and its wall panels (spawned in the first tick) applied after, against all of
// them flooded at once.
TEST(CellLightBench, DISABLED_BlocksThenWallPanels) {
  const primitives::Palette palette = ShippedPalette().value();
  for (const std::string scene : {"hub", "octant"}) {
    const auto blocks = ShippedBlueprint(scene, palette).value();
    const auto walls = ShippedBlueprintWalls(scene).value();
    const auto openings = ShippedBlueprintOpenings(scene).value();
    const auto panels = z13::building::BlueprintPanels(walls, palette, z13::BuildingTuning {}, openings).value();
    std::vector<z13::station::Block> all = blocks;
    all.insert(all.end(), panels.begin(), panels.end());
    std::vector<light::Lamp> lamps;
    for (const auto& point : primitives::LightsOf(all, primitives::OptionalPalette(palette))) {
      lamps.push_back(light::LampOf(point));
    }
    const SealedParts first = SealedPartsOf(blocks, palette);
    const SealedParts panel_parts = SealedPartsOf(panels, palette);
    const double panel_parts_ms = Ms([&] { std::ignore = SealedPartsOf(panels, palette); });
    const double block_parts_ms = Ms([&] { std::ignore = SealedPartsOf(blocks, palette); });
    const SealedParts everything = SealedPartsOf(all, palette);

    std::optional<light::CellLight> at_once;
    const double once_ms = Ms([&] { at_once.emplace(everything.opaque, everything.glass, lamps); });
    std::optional<light::CellLight> staged;
    const double blocks_ms = Ms([&] { staged.emplace(first.opaque, first.glass, lamps); });
    const double panels_ms = Ms([&] {
      staged->Apply({.opaque_added = panel_parts.opaque, .glass_added = panel_parts.glass});
    });
    std::cout << std::format(
                     "[ bench ] '{}': {} blocks + {} panels ({} opaque boxes, {} glass), {} lamps; all at once "
                     "{:.0f} ms; blocks {:.0f} ms, then the panels {:.0f} ms (together {:.0f}); "
                     "sealed boxes of the blocks {:.0f} ms, of the panels {:.0f} ms\n",
                     scene, blocks.size(), panels.size(), panel_parts.opaque.size(), panel_parts.glass.size(),
                     lamps.size(), once_ms, blocks_ms, panels_ms, blocks_ms + panels_ms, block_parts_ms, panel_parts_ms)
              << std::flush;
  }
}

// What lighting the lamps of the hub batch by batch costs, as CellLighting does it: the flood of a batch, then the
// texels the renderer uploads for it (labels of the cell tiles, light of the light tiles).
TEST(CellLightBench, DISABLED_LampBatches) {
  constexpr size_t kFirstBatch = 48;
  constexpr size_t kBatch = 6;
  constexpr int kBatches = 20;
  const z13::ThreadPool pool(4);
  const primitives::Palette palette = ShippedPalette().value();
  const auto blocks = ShippedBlueprint("hub", palette).value();
  const auto walls = ShippedBlueprintWalls("hub").value();
  const auto openings = ShippedBlueprintOpenings("hub").value();
  const auto panels = z13::building::BlueprintPanels(walls, palette, z13::BuildingTuning {}, openings).value();
  std::vector<z13::station::Block> all = blocks;
  all.insert(all.end(), panels.begin(), panels.end());
  const SealedParts parts = SealedPartsOf(all, palette);
  std::vector<light::Lamp> lamps;
  for (const auto& point : primitives::LightsOf(all, primitives::OptionalPalette(palette))) {
    lamps.push_back(light::LampOf(point));
  }
  ASSERT_GT(lamps.size(), kFirstBatch + (kBatch * kBatches));
  const std::span<const light::Lamp> first(lamps.data(), kFirstBatch);
  light::CellLight cell_light(parts.opaque, parts.glass, first);
  double apply_ms = 0;
  double labels_ms = 0;
  double light_ms = 0;
  size_t label_texels = 0;
  size_t tiles_total = 0;
  size_t biggest = 0;
  size_t light_texels = 0;
  for (int batch = 0; batch < kBatches; ++batch) {
    light::LightEdits edits;
    const size_t begin = kFirstBatch + (batch * kBatch);
    edits.lamps_added.assign(lamps.begin() + static_cast<std::ptrdiff_t>(begin),
                             lamps.begin() + static_cast<std::ptrdiff_t>(begin + kBatch));
    light::LightChanges changes;
    apply_ms += Ms([&] { changes = cell_light.Apply(edits); });
    labels_ms += Ms([&] {
      std::vector<size_t> counts(changes.cell_tiles.size());
      pool.ForEachIndex(changes.cell_tiles.size(),
                        [&](size_t tile) { counts[tile] = cell_light.CellTexels(changes.cell_tiles[tile]).size(); });
      label_texels += std::accumulate(counts.begin(), counts.end(), size_t {});
      tiles_total += counts.size();
      biggest = std::max(biggest, *std::ranges::max_element(counts));
    });
    light_ms += Ms([&] {
      for (const primitives::CellBox& tile : changes.light_tiles) {
        for (uint8_t piece = 0; piece < light::kPiecesPerNode; ++piece) {
          light_texels += cell_light.Texels(tile, piece, light::NodeTexel::kLight).size();
          light_texels += cell_light.Texels(tile, piece, light::NodeTexel::kDirection).size();
        }
      }
    });
  }
  std::cout << std::format(
                   "[ bench ] hub lamp batches of {}, per batch: flood {:.1f} ms, label texels {:.1f} ms ({} texels), "
                   "light texels {:.1f} ms ({} texels)\n",
                   kBatch, apply_ms / kBatches, labels_ms / kBatches, label_texels / kBatches, light_ms / kBatches,
                   light_texels / kBatches)
            << std::flush;
  std::cout << std::format("[ bench ] {} cell tiles per batch, the biggest {} texels, {} threads\n",
                           tiles_total / kBatches, biggest, pool.Threads())
            << std::flush;
}

}  // namespace

// The first build of each scene's light on the process's pool, as the game builds it, by the size of the pool.
TEST(CellLightBench, DISABLED_FirstBuildOnPool) {
  constexpr std::array kPoolSizes {1, 2, 4, 8};
  const primitives::Palette palette = ShippedPalette().value();
  for (const std::string scene : {"hub", "octant", "spire"}) {
    const auto blocks = ShippedBlueprint(scene, palette).value();
    const SealedParts parts = SealedPartsOf(blocks, palette);
    std::vector<light::Lamp> lamps;
    for (const auto& point : primitives::LightsOf(blocks, primitives::OptionalPalette(palette))) {
      lamps.push_back(light::LampOf(point));
    }
    for (const int threads : kPoolSizes) {
      const z13::ThreadPool pool(static_cast<size_t>(threads));
      std::optional<light::CellLight> built;
      const double ms = Ms([&] {
        built.emplace(parts.opaque, parts.glass, lamps, [&pool](size_t count, const std::function<void(size_t)>& body) {
          pool.ForEachIndex(count, body);
        });
      });
      std::cout << std::format("[ bench ] '{}': first build on {} threads {:.0f} ms\n", scene, threads, ms) << std::flush;
    }
  }
}

}  // namespace z13::testing
