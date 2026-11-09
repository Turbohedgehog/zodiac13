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
#include <cstdint>
#include <iterator>
#include <random>
#include <tuple>
#include <utility>
#include <vector>

#include "../../../../lib_z13/rooms/src/rect_holes.h"

namespace {

using z13::station::rooms::PlaneRect;
using z13::station::rooms::RectangularHoles;

constexpr int kMaxCells = 6;

bool Before(const PlaneRect& a, const PlaneRect& b) {
  return std::tie(a.u0, a.v0, a.u1, a.v1) < std::tie(b.u0, b.v0, b.u1, b.v1);
}

std::vector<PlaneRect> Sorted(std::vector<PlaneRect> holes) {
  std::ranges::sort(holes, Before);
  return holes;
}

// The holes found cell by cell on a small grid: covered cells painted, the free cells joined to the frame around it
// (over edges and corners) are open, every other free region that is a box within `max_cells` is a hole. It agrees with
// the Boost.Polygon union the opening search used before RectangularHoles (checked over 20000 random sets then).
std::vector<PlaneRect> BruteForceHoles(const std::vector<PlaneRect>& rects, int max_cells) {
  constexpr int kFrame = 1;
  int high_u = 0;
  int high_v = 0;
  for (const PlaneRect& rect : rects) {
    high_u = std::max(high_u, rect.u1);
    high_v = std::max(high_v, rect.v1);
  }
  const int columns = high_u + (2 * kFrame);
  const int rows = high_v + (2 * kFrame);
  const auto at = [columns](int u, int v) { return static_cast<size_t>((v * columns) + u); };
  std::vector<int> cell(static_cast<size_t>(columns) * static_cast<size_t>(rows), 0);  // 0 free, 1 covered, 2 seen
  for (const PlaneRect& rect : rects) {
    for (int v = rect.v0; v < rect.v1; ++v) {
      for (int u = rect.u0; u < rect.u1; ++u) {
        cell[at(u + kFrame, v + kFrame)] = 1;
      }
    }
  }
  struct Region {
    int u0, v0, u1, v1, cells;
    bool open;
  };
  std::vector<PlaneRect> holes;
  for (int v = 0; v < rows; ++v) {
    for (int u = 0; u < columns; ++u) {
      if (cell[at(u, v)] != 0) {
        continue;
      }
      Region region {.u0 = u, .v0 = v, .u1 = u + 1, .v1 = v + 1, .cells = 0, .open = false};
      std::vector<std::pair<int, int>> stack {{u, v}};
      cell[at(u, v)] = 2;
      while (!stack.empty()) {
        const auto [cu, cv] = stack.back();
        stack.pop_back();
        ++region.cells;
        region.u0 = std::min(region.u0, cu);
        region.v0 = std::min(region.v0, cv);
        region.u1 = std::max(region.u1, cu + 1);
        region.v1 = std::max(region.v1, cv + 1);
        region.open = region.open || cu == 0 || cv == 0 || cu == columns - 1 || cv == rows - 1;
        for (int dv = -1; dv <= 1; ++dv) {
          for (int du = -1; du <= 1; ++du) {
            const int nu = cu + du;
            const int nv = cv + dv;
            if (nu >= 0 && nv >= 0 && nu < columns && nv < rows && cell[at(nu, nv)] == 0) {
              cell[at(nu, nv)] = 2;
              stack.push_back({nu, nv});
            }
          }
        }
      }
      const int width = region.u1 - region.u0;
      const int height = region.v1 - region.v0;
      if (!region.open && width <= max_cells && height <= max_cells && region.cells == width * height) {
        holes.push_back({.u0 = region.u0 - kFrame, .v0 = region.v0 - kFrame, .u1 = region.u1 - kFrame,
                         .v1 = region.v1 - kFrame});
      }
    }
  }
  return Sorted(std::move(holes));
}

bool Overlap(int low_a, int high_a, int low_b, int high_b) {
  return low_a < high_b && low_b < high_a;
}

// Whether `rect` covers a cell next to the box `hole` (one of the strips of cells along its four sides).
bool Touches(const PlaneRect& rect, const PlaneRect& hole) {
  const auto in_u = [&](int low, int high) { return Overlap(rect.u0, rect.u1, low, high); };
  const auto in_v = [&](int low, int high) { return Overlap(rect.v0, rect.v1, low, high); };
  return (in_u(hole.u0 - 1, hole.u0) && in_v(hole.v0, hole.v1)) ||
         (in_u(hole.u1, hole.u1 + 1) && in_v(hole.v0, hole.v1)) ||
         (in_v(hole.v0 - 1, hole.v0) && in_u(hole.u0, hole.u1)) ||
         (in_v(hole.v1, hole.v1 + 1) && in_u(hole.u0, hole.u1));
}

std::vector<PlaneRect> BruteForceHolesBeside(const std::vector<PlaneRect>& rects, const std::vector<PlaneRect>& beside,
                                        int max_cells) {
  std::vector<PlaneRect> holes = BruteForceHoles(rects, max_cells);
  std::erase_if(holes, [&beside](const PlaneRect& hole) {
    return std::ranges::none_of(beside, [&hole](const PlaneRect& rect) { return Touches(rect, hole); });
  });
  return holes;
}

// A ring of four rectangles around the cells [u0, u1) x [v0, v1).
std::vector<PlaneRect> RingAround(int u0, int v0, int u1, int v1) {
  return {{.u0 = u0 - 1, .v0 = v0 - 1, .u1 = u1 + 1, .v1 = v0},
          {.u0 = u0 - 1, .v0 = v1, .u1 = u1 + 1, .v1 = v1 + 1},
          {.u0 = u0 - 1, .v0 = v0, .u1 = u0, .v1 = v1},
          {.u0 = u1, .v0 = v0, .u1 = u1 + 1, .v1 = v1}};
}

TEST(RectHolesTest, ARingShutsInItsHole) {
  const std::vector<PlaneRect> holes = RectangularHoles(RingAround(3, 4, 6, 6), RingAround(3, 4, 6, 6), kMaxCells);

  ASSERT_EQ(holes.size(), 1u);
  EXPECT_EQ(holes[0], (PlaneRect {.u0 = 3, .v0 = 4, .u1 = 6, .v1 = 6}));
}

TEST(RectHolesTest, ARingWithAGapShutsNothing) {
  std::vector<PlaneRect> ring = RingAround(3, 4, 6, 6);
  ring.pop_back();

  EXPECT_TRUE(RectangularHoles(ring, ring, kMaxCells).empty());
}

TEST(RectHolesTest, AHoleWiderThanTheLimitIsLeftOut) {
  const std::vector<PlaneRect> too_wide = RingAround(0, 0, kMaxCells + 1, 2);
  const std::vector<PlaneRect> fits = RingAround(0, 0, kMaxCells, 2);

  EXPECT_TRUE(RectangularHoles(too_wide, too_wide, kMaxCells).empty());
  EXPECT_EQ(RectangularHoles(fits, fits, kMaxCells).size(), 1u);
}

TEST(RectHolesTest, RectanglesTouchingOnlyAtACornerLeaveAGap) {
  // Four blocks around a cell, each meeting the next at a corner: the free cell is joined to the outside there.
  const std::vector<PlaneRect> blocks {{.u0 = 0, .v0 = 0, .u1 = 2, .v1 = 2},
                                       {.u0 = 2, .v0 = 2, .u1 = 4, .v1 = 4},
                                       {.u0 = 2, .v0 = 0, .u1 = 3, .v1 = 1},
                                       {.u0 = 1, .v0 = 2, .u1 = 2, .v1 = 3}};

  EXPECT_EQ(RectangularHoles(blocks, blocks, kMaxCells), BruteForceHoles(blocks, kMaxCells));
}

TEST(RectHolesTest, AnLShapedPocketIsNotAHole) {
  std::vector<PlaneRect> rects = RingAround(0, 0, 4, 4);
  rects.push_back({.u0 = 2, .v0 = 2, .u1 = 4, .v1 = 4});

  EXPECT_TRUE(RectangularHoles(rects, rects, kMaxCells).empty());
}

TEST(RectHolesTest, OnlyAHoleBesideTheGivenRectanglesIsFound) {
  const std::vector<PlaneRect> ring = RingAround(3, 4, 6, 6);
  std::vector<PlaneRect> far_away = RingAround(30, 40, 33, 42);
  far_away.insert(far_away.end(), ring.begin(), ring.end());

  const std::vector<PlaneRect> found = RectangularHoles(far_away, ring, kMaxCells);

  ASSERT_EQ(found.size(), 1u);
  EXPECT_EQ(found[0], (PlaneRect {.u0 = 3, .v0 = 4, .u1 = 6, .v1 = 6}));
}

struct SetShape {
  int rects;
  int grid;
  int largest;
};

class RectHolesRandomTest : public ::testing::TestWithParam<SetShape> {};

TEST_P(RectHolesRandomTest, FindsTheHolesACellByCellSearchFinds) {
  const SetShape shape = GetParam();
  constexpr int kSets = 4000;
  constexpr uint32_t kSeed = 7;
  std::mt19937 random(kSeed);
  std::uniform_int_distribution<int> count(2, shape.rects);
  std::uniform_int_distribution<int> at(0, shape.grid);
  std::uniform_int_distribution<int> size(1, shape.largest);
  int with_holes = 0;
  for (int set = 0; set < kSets; ++set) {
    std::vector<PlaneRect> rects;
    for (int i = count(random); i > 0; --i) {
      const int u = at(random);
      const int v = at(random);
      rects.push_back({.u0 = u, .v0 = v, .u1 = u + size(random), .v1 = v + size(random)});
    }
    std::vector<PlaneRect> beside;
    std::ranges::copy_if(rects, std::back_inserter(beside), [&](const PlaneRect&) { return random() % 2 == 0; });
    const std::vector<PlaneRect> expected = BruteForceHoles(rects, kMaxCells);
    with_holes += expected.empty() ? 0 : 1;
    ASSERT_EQ(Sorted(RectangularHoles(rects, rects, kMaxCells)), expected) << "set " << set;
    ASSERT_EQ(Sorted(RectangularHoles(rects, beside, kMaxCells)), BruteForceHolesBeside(rects, beside, kMaxCells))
        << "set " << set << " beside half of them";
  }
  EXPECT_GT(with_holes, 0) << "the sets must hold some holes to prove anything";
}

INSTANTIATE_TEST_SUITE_P(Shapes, RectHolesRandomTest,
                         ::testing::Values(SetShape {.rects = 14, .grid = 9, .largest = 5},
                                           SetShape {.rects = 30, .grid = 14, .largest = 4},
                                           SetShape {.rects = 40, .grid = 24, .largest = 9}));

}  // namespace
