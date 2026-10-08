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
#include <vector>

#include <rooms/room_builder.h>
#include <rooms/room_overlap.h>

namespace z13::station::rooms {
namespace {

using z13::building::primitives::CellBox;

constexpr int kSide = 10;
constexpr int kHeight = 6;

// Six slabs enclosing a kSide x kSide x kHeight room whose inside is [1, kSide + 1) in x
// and y and [1, kHeight + 1) in z.
std::vector<CellBox> Shell() {
  constexpr int outer = kSide + 2;
  constexpr int tall = kHeight + 2;
  return {
      {.min = {0, 0, 0}, .extent = {outer, outer, 1}},
      {.min = {0, 0, tall - 1}, .extent = {outer, outer, 1}},
      {.min = {0, 0, 1}, .extent = {1, outer, kHeight}},
      {.min = {outer - 1, 0, 1}, .extent = {1, outer, kHeight}},
      {.min = {1, 0, 1}, .extent = {kSide, 1, kHeight}},
      {.min = {1, outer - 1, 1}, .extent = {kSide, 1, kHeight}},
  };
}

RoomGraph Build(const RoomSources& sources) {
  auto graph = BuildRooms(sources);
  EXPECT_TRUE(graph.has_value());
  return graph.value_or(RoomGraph {});
}

TEST(RoomBuilderTest, NoBlocksIsOnlyTheVacuum) {
  const RoomGraph graph = Build({});

  EXPECT_EQ(graph.rooms.size(), 1U);
  EXPECT_TRUE(graph.portals.empty());
}

TEST(RoomBuilderTest, ASealedShellEnclosesOneRoom) {
  const RoomGraph graph = Build({.sealed = Shell()});

  ASSERT_EQ(graph.rooms.size(), 2U);
  EXPECT_EQ(graph.rooms[1].volume_cells, kSide * kSide * kHeight);
  EXPECT_EQ(graph.rooms[1].anchor, Eigen::Vector3i(1, 1, 1));
  EXPECT_EQ(graph.RoomAt({5, 5, 3}), 1U);
  EXPECT_EQ(graph.RoomAt({-1, 5, 3}), kVacuumRoom);
  EXPECT_EQ(graph.RoomAt({5, 5, 0}), std::nullopt);
}

TEST(RoomBuilderTest, AWallSplitsTheRoom) {
  RoomSources sources {.sealed = Shell()};
  sources.sealed.push_back({.min = {5, 1, 1}, .extent = {1, kSide, kHeight}});

  const RoomGraph graph = Build(sources);

  ASSERT_EQ(graph.rooms.size(), 3U);
  EXPECT_NE(graph.RoomAt({2, 5, 3}), graph.RoomAt({8, 5, 3}));
  EXPECT_EQ(graph.rooms[1].volume_cells + graph.rooms[2].volume_cells, (kSide - 1) * kSide * kHeight);
  EXPECT_EQ(graph.rooms[1].anchor, Eigen::Vector3i(1, 1, 1));
  EXPECT_EQ(graph.rooms[2].anchor, Eigen::Vector3i(6, 1, 1));
}

TEST(RoomBuilderTest, AHoleInTheHullOpensTheRoomToTheVacuum) {
  RoomSources sources {.sealed = Shell()};
  sources.sealed.erase(sources.sealed.begin() + 3);
  sources.sealed.push_back({.min = {kSide + 1, 0, 1}, .extent = {1, 4, kHeight}});
  sources.sealed.push_back({.min = {kSide + 1, 6, 1}, .extent = {1, 6, kHeight}});

  const RoomGraph graph = Build(sources);

  EXPECT_EQ(graph.rooms.size(), 1U);
  EXPECT_EQ(graph.RoomAt({5, 5, 3}), kVacuumRoom);
}

TEST(RoomBuilderTest, ARoomWithoutARoofIsOpenToSpace) {
  RoomSources sources {.sealed = Shell()};
  sources.sealed.erase(sources.sealed.begin() + 1);

  EXPECT_EQ(Build(sources).RoomAt({5, 5, 3}), kVacuumRoom);
}

TEST(RoomBuilderTest, RoomsStackedOnTwoLevelsAreSeparate) {
  RoomSources sources {.sealed = Shell()};
  sources.sealed.push_back({.min = {1, 1, 4}, .extent = {kSide, kSide, 1}});

  const RoomGraph graph = Build(sources);

  ASSERT_EQ(graph.rooms.size(), 3U);
  EXPECT_EQ(graph.rooms[1].anchor, Eigen::Vector3i(1, 1, 1));
  EXPECT_EQ(graph.rooms[2].anchor, Eigen::Vector3i(1, 1, 5));
}

TEST(RoomBuilderTest, ADoorJoinsTheRoomsOnBothSidesAsAPortal) {
  RoomSources sources {.sealed = Shell()};
  sources.sealed.push_back({.min = {5, 1, 1}, .extent = {1, kSide, kHeight}});
  sources.portals.push_back({.opening = {.min = {5, 4, 1}, .extent = {1, 2, 4}},
                             .axis = Axis::kX,
                             .visible = false,
                             .passable = false});

  const RoomGraph graph = Build(sources);

  ASSERT_EQ(graph.rooms.size(), 3U);
  ASSERT_EQ(graph.portals.size(), 1U);
  const Portal& portal = graph.portals.front();
  EXPECT_EQ(portal.a, 1U);
  EXPECT_EQ(portal.b, 2U);
  EXPECT_EQ(portal.area_cells, 2 * 4);
  EXPECT_FALSE(portal.visible);
}

TEST(RoomBuilderTest, AWindowInTheHullIsAVisiblePortalToTheVacuum) {
  RoomSources sources {.sealed = Shell()};
  sources.portals.push_back({.opening = {.min = {kSide + 1, 3, 2}, .extent = {1, 3, 3}},
                             .axis = Axis::kX,
                             .visible = true,
                             .passable = false});

  const RoomGraph graph = Build(sources);

  ASSERT_EQ(graph.portals.size(), 1U);
  EXPECT_EQ(graph.portals.front().a, kVacuumRoom);
  EXPECT_EQ(graph.portals.front().b, 1U);
  EXPECT_EQ(graph.portals.front().area_cells, 9);
  EXPECT_TRUE(graph.portals.front().visible);
}

TEST(RoomBuilderTest, ThePortalOrderDoesNotDependOnTheOrderOfTheSources) {
  RoomSources sources {.sealed = Shell()};
  sources.sealed.push_back({.min = {5, 1, 1}, .extent = {1, kSide, kHeight}});
  sources.portals.push_back({.opening = {.min = {5, 2, 1}, .extent = {1, 2, 4}}, .axis = Axis::kX});
  sources.portals.push_back({.opening = {.min = {5, 6, 1}, .extent = {1, 2, 4}}, .axis = Axis::kX});
  RoomSources reversed = sources;
  std::ranges::reverse(reversed.portals);

  EXPECT_EQ(Build(sources).portals, Build(reversed).portals);
}

TEST(RoomBuilderTest, ABlockFarFromTheRestIsRefused) {
  const auto graph = BuildRooms({.sealed = {{.min = {0, 0, 0}, .extent = {1, 1, 1}},
                                           {.min = {100000, 100000, 0}, .extent = {1, 1, 1}}}});

  EXPECT_FALSE(graph.has_value());
}

TEST(RoomOverlapTest, ASplitSharesTheOldRoomBetweenTheNewOnes) {
  const RoomGraph whole = Build({.sealed = Shell()});
  RoomSources divided {.sealed = Shell()};
  divided.sealed.push_back({.min = {5, 1, 1}, .extent = {1, kSide, kHeight}});

  const std::vector<RoomOverlap> overlaps = OverlapsBetween(whole, Build(divided));

  const std::vector<RoomOverlap> expected {
      {.before = kVacuumRoom, .after = kVacuumRoom, .cells = overlaps.front().cells},
      {.before = 1, .after = 1, .cells = 4 * kSide * kHeight},
      {.before = 1, .after = 2, .cells = 5 * kSide * kHeight},
  };
  EXPECT_EQ(overlaps, expected);
}

TEST(RoomOverlapTest, AMergeSharesTheNewRoomBetweenTheOldOnes) {
  RoomSources divided {.sealed = Shell()};
  divided.sealed.push_back({.min = {5, 1, 1}, .extent = {1, kSide, kHeight}});
  const RoomGraph merged = Build({.sealed = Shell()});

  const std::vector<RoomOverlap> overlaps = OverlapsBetween(Build(divided), merged);

  EXPECT_TRUE(std::ranges::any_of(overlaps, [](const RoomOverlap& o) { return o.before == 1 && o.after == 1; }));
  EXPECT_TRUE(std::ranges::any_of(overlaps, [](const RoomOverlap& o) { return o.before == 2 && o.after == 1; }));
}

TEST(RoomOverlapTest, ARoomBuiltWhereThereWasNothingOverlapsTheVacuum) {
  const RoomGraph empty = Build({});
  const RoomGraph built = Build({.sealed = Shell()});

  const std::vector<RoomOverlap> grown = OverlapsBetween(empty, built);
  const std::vector<RoomOverlap> shrunk = OverlapsBetween(built, empty);

  const auto cells = [](const std::vector<RoomOverlap>& overlaps, RoomIndex before, RoomIndex after) {
    const auto it = std::ranges::find_if(
        overlaps, [&](const RoomOverlap& o) { return o.before == before && o.after == after; });
    return it == overlaps.end() ? int64_t {} : it->cells;
  };
  EXPECT_EQ(cells(grown, kVacuumRoom, 1), kSide * kSide * kHeight);
  EXPECT_EQ(cells(shrunk, 1, kVacuumRoom), kSide * kSide * kHeight);
  EXPECT_TRUE(OverlapsBetween(empty, empty).empty());
}

}  // namespace
}  // namespace z13::station::rooms
