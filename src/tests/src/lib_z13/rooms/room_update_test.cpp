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

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <random>
#include <tuple>
#include <vector>

#include <rooms/room_builder.h>
#include <rooms/room_update.h>

namespace z13::station::rooms {
namespace {

using z13::building::primitives::CellBox;

// Small chunks, so that edits cross many of them.
constexpr int kChunkCells = 8;
constexpr int kSide = 20;
constexpr int kHeight = 8;
constexpr int kOuter = kSide + 2;
constexpr int kTall = kHeight + 2;
constexpr int kSteps = 150;
constexpr int kPanel = 4;
// Seeds 7 and 2026 once caught pockets the new blocks closed off; 13 is the first tried.
constexpr std::array kSeeds {7U, 13U, 2026U};
// Cells are compared on a sparser lattice: every room's anchor, volume and bounds already
// are, and a full comparison per step would cost seconds in Debug.
constexpr int kCellStride = 3;

std::vector<CellBox> Shell() {
  return {
      {.min = {0, 0, 0}, .extent = {kOuter, kOuter, 1}},
      {.min = {0, 0, kTall - 1}, .extent = {kOuter, kOuter, 1}},
      {.min = {0, 0, 1}, .extent = {1, kOuter, kHeight}},
      {.min = {kOuter - 1, 0, 1}, .extent = {1, kOuter, kHeight}},
      {.min = {1, 0, 1}, .extent = {kSide, 1, kHeight}},
      {.min = {1, kOuter - 1, 1}, .extent = {kSide, 1, kHeight}},
  };
}

const CellBox kWall {.min = {kSide / 2, 1, 1}, .extent = {1, kSide, kHeight}};

RoomGraph Full(const RoomSources& sources) {
  auto graph = BuildRooms(sources, kVacuumRoom + 1, kChunkCells);
  EXPECT_TRUE(graph.has_value()) << graph.error();
  return graph.value_or(RoomGraph {});
}

RoomGraph Update(const RoomGraph& previous, const RoomSources& sources) {
  auto graph = UpdateRooms(previous, sources, previous.next_id, kChunkCells);
  EXPECT_TRUE(graph.has_value()) << graph.error();
  return graph.value_or(RoomGraph {});
}

using Anchor = std::tuple<int, int, int>;

// A room by its anchor, the vacuum as nothing: what doesn't depend on the ids.
std::optional<Anchor> AnchorOf(const RoomGraph& graph, RoomId room) {
  if (room == kVacuumRoom) {
    return std::nullopt;
  }
  const Eigen::Vector3i& anchor = graph.rooms.at(room).anchor;
  return Anchor {anchor.x(), anchor.y(), anchor.z()};
}

struct CanonicalPortal {
  std::tuple<int, int, int> opening;
  std::optional<Anchor> low;
  std::optional<Anchor> high;
  int area_cells {};
  bool visible {};

  auto operator<=>(const CanonicalPortal&) const = default;
};

// Everything the graph says about the cells in `region`, with rooms named by anchor.
struct Canonical {
  std::map<Anchor, std::tuple<int64_t, int, int, int, int, int, int>> rooms;
  std::vector<std::optional<std::optional<Anchor>>> cells;
  std::vector<CanonicalPortal> portals;

  bool operator==(const Canonical&) const = default;
};

Canonical CanonicalOf(const RoomGraph& graph, const CellBox& region) {
  Canonical canonical;
  for (const auto& [id, room] : graph.rooms) {
    if (id != kVacuumRoom) {
      const CellBox& b = room.bounds;
      canonical.rooms[*AnchorOf(graph, id)] = {room.volume_cells, b.min.x(), b.min.y(), b.min.z(),
                                               b.extent.x(),      b.extent.y(), b.extent.z()};
    }
  }
  for (int z = region.min.z(); z < region.End().z(); z += kCellStride) {
    for (int y = region.min.y(); y < region.End().y(); y += kCellStride) {
      for (int x = region.min.x(); x < region.End().x(); x += kCellStride) {
        const auto room = graph.RoomAt({x, y, z});
        canonical.cells.push_back(room ? std::optional(AnchorOf(graph, *room)) : std::nullopt);
      }
    }
  }
  for (const Portal& portal : graph.portals) {
    const auto other = portal.low_side == portal.a ? portal.b : portal.a;
    canonical.portals.push_back({.opening = {portal.opening.min.x(), portal.opening.min.y(), portal.opening.min.z()},
                                 .low = AnchorOf(graph, portal.low_side),
                                 .high = AnchorOf(graph, other),
                                 .area_cells = portal.area_cells,
                                 .visible = portal.visible});
  }
  std::ranges::sort(canonical.portals);
  return canonical;
}

RoomId IdAt(const RoomGraph& graph, const Eigen::Vector3i& cell) {
  return graph.RoomAt(cell).value_or(kVacuumRoom);
}

// The shell's walls as panels kPanel wide, so that one can be taken out as a hole.
std::vector<CellBox> ShellPanels() {
  std::vector<CellBox> panels {Shell()[0], Shell()[1]};
  for (int at = 0; at < kOuter; at += kPanel) {
    const int width = std::min(kPanel, kOuter - at);
    panels.push_back({.min = {0, at, 1}, .extent = {1, width, kHeight}});
    panels.push_back({.min = {kOuter - 1, at, 1}, .extent = {1, width, kHeight}});
  }
  for (int at = 1; at < kOuter - 1; at += kPanel) {
    const int width = std::min(kPanel, kOuter - 1 - at);
    panels.push_back({.min = {at, 0, 1}, .extent = {width, 1, kHeight}});
    panels.push_back({.min = {at, kOuter - 1, 1}, .extent = {width, 1, kHeight}});
  }
  return panels;
}

enum class Edit : uint8_t { kWall, kWindow, kRemoveInside, kTogglePanel, kOutside, kRemoveOutside, kSwapWindow };

// What the random edits went through, so the test fails if they stop covering the cases.
struct Coverage {
  size_t most_rooms {};
  size_t portals {};
  int holes {};
  int outside {};
  int swaps {};
};

class RandomRoomEditsTest : public ::testing::TestWithParam<uint32_t> {};

TEST_P(RandomRoomEditsTest, EndWhereAFullBuildDoes) {
  std::mt19937 random(GetParam());
  const std::vector<CellBox> panels = ShellPanels();
  std::vector<bool> panel_in(panels.size(), true);
  std::vector<CellBox> inside;
  std::vector<CellBox> outside;
  std::vector<PortalSource> windows;
  const auto sources = [&] {
    RoomSources built {.portals = windows};
    for (size_t i = 0; i < panels.size(); ++i) {
      if (panel_in[i]) {
        built.sealed.push_back(panels[i]);
      }
    }
    std::ranges::copy(inside, std::back_inserter(built.sealed));
    std::ranges::copy(outside, std::back_inserter(built.sealed));
    return built;
  };
  const auto free = [&](const CellBox& box) {
    return std::ranges::none_of(sources().sealed, [&box](const CellBox& other) { return other.Overlaps(box); });
  };
  std::discrete_distribution<int> edit({30, 15, 20, 10, 10, 5, 10});
  std::uniform_int_distribution<int> across(1, kSide);
  std::uniform_int_distribution<int> length(2, kSide / 2);
  std::uniform_int_distribution<int> beyond(kOuter + 1, kOuter + 3 * kChunkCells);
  std::bernoulli_distribution along_x(0.5);
  const CellBox region {.min = Eigen::Vector3i::Constant(-kChunkCells),
                        .extent = Eigen::Vector3i::Constant(kOuter + 5 * kChunkCells)};
  RoomGraph graph = Full(sources());
  Coverage coverage;
  for (int step = 0; step < kSteps; ++step) {
    const auto chosen = static_cast<Edit>(edit(random));
    switch (chosen) {
      case Edit::kWall:
        [[fallthrough]];
      case Edit::kWindow: {
        const bool x = along_x(random);
        // Half of them wall to wall, so rooms split.
        const bool span = across(random) % 2 == 0;
        const int run = span ? kSide : length(random);
        const Eigen::Vector2i at(x ? (span ? 1 : across(random)) : across(random),
                                 x ? across(random) : (span ? 1 : across(random)));
        const CellBox wall {.min = {at.x(), at.y(), 1}, .extent = {x ? run : 1, x ? 1 : run, kHeight}};
        const bool fits = (wall.End().head<2>().array() <= kSide + 1).all() && free(wall);
        if (fits) {
          inside.push_back(wall);
          if (chosen == Edit::kWindow) {
            windows.push_back({.opening = wall, .axis = x ? Axis::kY : Axis::kX, .visible = true});
          }
        }
        break;
      }
      case Edit::kRemoveInside:
        if (!inside.empty()) {
          std::uniform_int_distribution<size_t> pick(0, inside.size() - 1);
          const CellBox removed = inside[pick(random)];
          std::erase(inside, removed);
          std::erase_if(windows, [&removed](const PortalSource& window) { return window.opening == removed; });
        }
        break;
      case Edit::kTogglePanel: {
        // A hole is closed again by the next toggle, so the shell is mostly whole. The floor
        // and roof stay: a hole in a wall is enough to open it.
        const auto hole = std::ranges::find(panel_in, false);
        if (hole != panel_in.end()) {
          *hole = true;
        } else {
          std::uniform_int_distribution<size_t> pick(2, panels.size() - 1);
          panel_in[pick(random)] = false;
          ++coverage.holes;
        }
        break;
      }
      case Edit::kOutside: {
        const CellBox box {.min = {beyond(random), across(random), 1}, .extent = {2, 2, 2}};
        if (free(box)) {
          outside.push_back(box);
          ++coverage.outside;
        }
        break;
      }
      case Edit::kRemoveOutside:
        if (!outside.empty()) {
          outside.erase(outside.begin());
        }
        break;
      case Edit::kSwapWindow:
        // The same cells as a wall or as a window: only the portal sources change.
        if (!inside.empty()) {
          std::uniform_int_distribution<size_t> pick(0, inside.size() - 1);
          const CellBox& wall = inside[pick(random)];
          const auto window = std::ranges::find(windows, wall, &PortalSource::opening);
          if (window == windows.end()) {
            windows.push_back({.opening = wall, .axis = wall.extent.x() == 1 ? Axis::kX : Axis::kY, .visible = true});
            ++coverage.swaps;
          } else {
            windows.erase(window);
          }
        }
        break;
    }
    graph = Update(graph, sources());
    coverage.most_rooms = std::max(coverage.most_rooms, graph.rooms.size());
    coverage.portals += graph.portals.size();
    ASSERT_EQ(CanonicalOf(graph, region), CanonicalOf(Full(sources()), region)) << "step " << step;
  }
  EXPECT_GT(coverage.most_rooms, 3U);
  EXPECT_GT(coverage.portals, 0U);
  EXPECT_GT(coverage.holes, 0);
  EXPECT_GT(coverage.outside, 0);
  EXPECT_GT(coverage.swaps, 0);
}

INSTANTIATE_TEST_SUITE_P(Seeds, RandomRoomEditsTest, ::testing::ValuesIn(kSeeds));

TEST(RoomUpdateTest, ASplitRoomKeepsItsIdOnTheSideOfItsAnchor) {
  RoomSources sources {.sealed = Shell()};
  const RoomGraph whole = Full(sources);
  const RoomId id = IdAt(whole, {1, 1, 1});
  sources.sealed.push_back(kWall);

  const RoomGraph divided = Update(whole, sources);

  EXPECT_EQ(IdAt(divided, {1, 1, 1}), id);
  EXPECT_GE(IdAt(divided, {kSide, 1, 1}), whole.next_id);
}

TEST(RoomUpdateTest, MergedRoomsKeepTheIdOfTheLowestAnchor) {
  RoomSources sources {.sealed = Shell()};
  sources.sealed.push_back(kWall);
  const RoomGraph divided = Full(sources);
  const RoomId low = IdAt(divided, {1, 1, 1});
  sources.sealed.pop_back();

  const RoomGraph merged = Update(divided, sources);

  EXPECT_EQ(IdAt(merged, {1, 1, 1}), low);
  EXPECT_EQ(IdAt(merged, {kSide, 1, 1}), low);
  EXPECT_FALSE(merged.rooms.contains(IdAt(divided, {kSide, 1, 1})));
}

TEST(RoomUpdateTest, ClosingTheLastWallMakesARoomOfTheVacuum) {
  RoomSources sources {.sealed = Shell()};
  const CellBox last = sources.sealed.back();
  sources.sealed.pop_back();
  const RoomGraph open = Full(sources);
  ASSERT_EQ(IdAt(open, {5, 5, 3}), kVacuumRoom);
  sources.sealed.push_back(last);

  const RoomGraph closed = Update(open, sources);

  EXPECT_NE(IdAt(closed, {5, 5, 3}), kVacuumRoom);
  EXPECT_EQ(closed.rooms.at(IdAt(closed, {5, 5, 3})).volume_cells, kSide * kSide * kHeight);
}

TEST(RoomUpdateTest, BuildingOutsideGrowsTheArea) {
  RoomSources sources {.sealed = Shell()};
  const RoomGraph before = Full(sources);
  const CellBox far {.min = {kOuter + 3 * kChunkCells, 2, 2}, .extent = {2, 2, 2}};
  sources.sealed.push_back(far);

  const RoomGraph after = Update(before, sources);

  EXPECT_TRUE(after.space.Area().Contains(far.min));
  EXPECT_EQ(after.RoomAt(far.min), std::nullopt);
  EXPECT_NE(IdAt(after, {5, 5, 3}), kVacuumRoom);
}

TEST(RoomUpdateTest, AWallTurnedWindowOfTheSameCellsBecomesAPortal) {
  RoomSources sources {.sealed = Shell()};
  sources.sealed.push_back(kWall);
  const RoomGraph wall = Full(sources);
  ASSERT_TRUE(wall.portals.empty());
  sources.portals.push_back({.opening = kWall, .axis = Axis::kX, .visible = true});

  const RoomGraph window = Update(wall, sources);
  sources.portals.clear();
  const RoomGraph wall_again = Update(window, sources);

  ASSERT_EQ(window.portals.size(), 1U);
  EXPECT_EQ(window.portals.front().area_cells, kSide * kHeight);
  EXPECT_TRUE(wall_again.portals.empty());
}

TEST(RoomUpdateTest, AChunkFilledAndOpenedAgainKeepsNoStaleRooms) {
  RoomSources sources {.sealed = Shell()};
  const RoomGraph open = Full(sources);
  // A chunk of the room's inside, filled solid, then opened by one cell.
  const CellBox chunk {.min = Eigen::Vector3i(kChunkCells, kChunkCells, 0),
                       .extent = Eigen::Vector3i::Constant(kChunkCells)};
  sources.sealed.push_back(chunk);
  const RoomGraph filled = Update(open, sources);
  ASSERT_TRUE(filled.leaf_rooms.at(Eigen::Vector3i(1, 1, 0))->empty());
  sources.sealed.back() = {.min = chunk.min, .extent = {kChunkCells - 1, kChunkCells, kChunkCells}};

  const auto opened = UpdateRooms(filled, sources, filled.next_id, kChunkCells);

  ASSERT_TRUE(opened.has_value()) << opened.error();
  const CellBox region {.min = Eigen::Vector3i::Constant(-1), .extent = Eigen::Vector3i::Constant(kOuter + 2)};
  EXPECT_EQ(CanonicalOf(*opened, region), CanonicalOf(Full(sources), region));
}

TEST(RoomUpdateTest, TheAreaFollowsTheBoundsBackAfterAFarBlockGoes) {
  RoomSources sources {.sealed = Shell()};
  const RoomGraph before = Full(sources);
  sources.sealed.push_back({.min = {kOuter + 5 * kChunkCells, 2, 2}, .extent = {1, 1, 1}});
  const RoomGraph far = Update(before, sources);
  sources.sealed.pop_back();

  const RoomGraph back = Update(far, sources);

  EXPECT_EQ(back.space.Area(), before.space.Area());
  EXPECT_EQ(back.space.AllChunks().size(), before.space.AllChunks().size());
  EXPECT_EQ(back.leaf_rooms.size(), back.space.AllChunks().size());
  EXPECT_NE(IdAt(back, {5, 5, 3}), kVacuumRoom);
}

TEST(RoomUpdateTest, AnEditSharesTheChunksItDoesNotTouch) {
  RoomSources sources {.sealed = Shell()};
  const RoomGraph before = Full(sources);
  sources.sealed.push_back({.min = {3, 3, 1}, .extent = {1, 1, 1}});

  const RoomGraph after = Update(before, sources);

  const auto shared = std::ranges::count_if(before.space.AllChunks(), [&after](const auto& chunk) {
    return after.space.AllChunks().at(chunk.first) == chunk.second;
  });
  EXPECT_EQ(static_cast<size_t>(shared), before.space.AllChunks().size() - 1);
}

}  // namespace
}  // namespace z13::station::rooms
