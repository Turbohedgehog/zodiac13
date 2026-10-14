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

// Finding the openings (rooms/openings.h) of every shipped station, whole and near the edits
// of RoomEditBench. Built by `make.py --bench`, run with `z13.py --bench --filter 'OpeningBench.*'`.

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <format>
#include <iostream>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

#include <primitives/station_assets.h>
#include <rooms/openings.h>
#include <rooms/portal_source.h>
#include <rooms/room_builder.h>
#include <rooms/room_graph.h>
#include <z13_tests/shipped_station.h>
#include <z13/components/block.h>

namespace z13::testing {
namespace {

namespace primitives = z13::building::primitives;
namespace rooms = z13::station::rooms;
using primitives::CellBox;

constexpr int kRuns = 3;
// RoomEditBench's wall panel: a 1 m square, one cell thick.
const Eigen::Vector3i kPanel {4, 1, 4};

using Clock = std::chrono::steady_clock;

double MsSince(Clock::time_point start) {
  return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

std::vector<CellBox> SealedOf(const std::string& scene, const primitives::Palette& palette) {
  const auto blocks = ShippedBlueprint(scene, palette);
  EXPECT_TRUE(blocks.has_value());
  std::vector<CellBox> sealed;
  for (const z13::station::Block& block : blocks.value_or(std::vector<z13::station::Block> {})) {
    if (rooms::IsSealing(palette.Find(block.spec.type_id)->get())) {
      sealed.push_back(primitives::OccupiedCells(block));
    }
  }
  return sealed;
}

bool NearTo(const CellBox& area, const CellBox& box, int cells) {
  return area.Grown(Eigen::Vector3i::Constant(cells)).Overlaps(box);
}

// Searches near `changed` as an update does (UpdateSpace).
void TimeEdit(const std::string& label, const std::vector<CellBox>& sealed, const CellBox& changed, int max_cells) {
  const CellBox near = changed.Grown(Eigen::Vector3i::Constant(max_cells + 2));
  std::vector<CellBox> walls;
  std::ranges::copy_if(sealed, std::back_inserter(walls), [&](const CellBox& box) { return NearTo(near, box, 1); });
  std::vector<CellBox> around;
  std::ranges::copy_if(sealed, std::back_inserter(around),
                       [&](const CellBox& box) { return NearTo(near, box, 2 * max_cells + 2); });
  const auto start = Clock::now();
  const std::vector<rooms::PortalSource> found = rooms::FindOpenings(around, walls, max_cells);
  std::cout << std::format("[ bench ] {}: {} walls, {} around; {} openings in {:.1f} ms\n", label, walls.size(),
                           around.size(), found.size(), MsSince(start))
            << std::flush;
}

TEST(OpeningBench, DISABLED_FullSearch) {
  const primitives::Palette palette = ShippedPalette().value();
  const int max_cells = rooms::RoomSettings {}.max_opening_cells;
  for (const std::string& scene : z13::testing::ShippedScenes()) {
    const std::vector<CellBox> sealed = SealedOf(scene, palette);
    for (int run = 0; run < kRuns; ++run) {
      const auto start = Clock::now();
      const std::vector<rooms::PortalSource> found = rooms::FindOpenings(sealed, sealed, max_cells);
      std::cout << std::format("[ bench ] '{}' run {}: {} sealed, {} openings in {:.1f} ms\n", scene, run,
                               sealed.size(), found.size(), MsSince(start))
                << std::flush;
    }
  }
}

// RoomEditBench's edits: a wall panel in the largest room, a cube in the smallest and one
// outside the hull, built; one of the station's walls destroyed.
TEST(OpeningBench, DISABLED_EditSearch) {
  const primitives::Palette palette = ShippedPalette().value();
  const int max_cells = rooms::RoomSettings {}.max_opening_cells;
  for (const std::string& scene : z13::testing::ShippedScenes()) {
    std::vector<CellBox> sealed = SealedOf(scene, palette);
    const auto graph = rooms::BuildRooms({.sealed = sealed});
    ASSERT_TRUE(graph.has_value());
    const std::vector<rooms::RoomId> by_anchor = graph->ByAnchor();
    if (by_anchor.empty()) {
      continue;
    }
    const auto volume = [&graph](rooms::RoomId room) { return graph->rooms.at(room).volume_cells; };
    const CellBox in_largest {.min = graph->rooms.at(*std::ranges::max_element(by_anchor, {}, volume)).anchor,
                              .extent = kPanel};
    const CellBox in_smallest {.min = graph->rooms.at(*std::ranges::min_element(by_anchor, {}, volume)).anchor,
                               .extent = Eigen::Vector3i::Ones()};
    const CellBox outside {.min = graph->space.Bounds().min, .extent = Eigen::Vector3i::Ones()};
    const auto thin = std::ranges::find_if(sealed, [](const CellBox& box) { return (box.extent.array() == 1).count() == 1; });
    const std::optional<CellBox> wall = thin != sealed.end() ? std::optional(*thin) : std::nullopt;
    for (int run = 0; run < kRuns; ++run) {
      for (const auto& [name, box] : {std::pair {"largest room", in_largest}, std::pair {"smallest room", in_smallest},
                                      std::pair {"outside the hull", outside}}) {
        sealed.push_back(box);
        TimeEdit(std::format("'{}' {} {}: build", scene, name, run), sealed, box, max_cells);
        sealed.pop_back();
      }
      if (wall) {
        std::vector<CellBox> without = sealed;
        std::erase(without, *wall);
        TimeEdit(std::format("'{}' station wall {}: destroy", scene, run), without, *wall, max_cells);
      }
    }
  }
}

}  // namespace
}  // namespace z13::testing
