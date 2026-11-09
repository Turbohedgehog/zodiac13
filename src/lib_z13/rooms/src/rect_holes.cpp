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

#include "rect_holes.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace z13::station::rooms {

namespace {

std::vector<int> EdgesOf(std::span<const PlaneRect> rects, int PlaneRect::*low, int PlaneRect::*high) {
  std::vector<int> edges;
  edges.reserve(2 * rects.size());
  for (const PlaneRect& rect : rects) {
    edges.push_back(rect.*low);
    edges.push_back(rect.*high);
  }
  std::ranges::sort(edges);
  edges.erase(std::ranges::unique(edges).begin(), edges.end());
  return edges;
}

size_t IndexOf(const std::vector<int>& edges, int at) {
  return static_cast<size_t>(std::ranges::lower_bound(edges, at) - edges.begin());
}

// What the flood knows of a cell of the grid between the edges.
enum class Cell : uint8_t {
  kFree,
  kCovered,
  kDone,    // in a region that was flooded whole
  kFailed,  // in a region given up on: too big or open, so anything joined to it is too
};

struct Grid {
  std::vector<int> us;
  std::vector<int> vs;
  size_t columns {};
  size_t rows {};
  std::vector<Cell> cells;

  size_t Index(size_t u, size_t v) const { return (v * columns) + u; }
};

Grid GridOf(std::span<const PlaneRect> rects) {
  Grid grid {.us = EdgesOf(rects, &PlaneRect::u0, &PlaneRect::u1),
             .vs = EdgesOf(rects, &PlaneRect::v0, &PlaneRect::v1)};
  grid.columns = grid.us.size() - 1;
  grid.rows = grid.vs.size() - 1;
  const size_t stride = grid.columns + 1;
  std::vector<int> count(stride * (grid.rows + 1));
  for (const PlaneRect& rect : rects) {
    const size_t u0 = IndexOf(grid.us, rect.u0);
    const size_t u1 = IndexOf(grid.us, rect.u1);
    const size_t v0 = IndexOf(grid.vs, rect.v0);
    const size_t v1 = IndexOf(grid.vs, rect.v1);
    ++count[(v0 * stride) + u0];
    --count[(v0 * stride) + u1];
    --count[(v1 * stride) + u0];
    ++count[(v1 * stride) + u1];
  }
  grid.cells.resize(grid.columns * grid.rows);
  for (size_t v = 0; v < grid.rows; ++v) {
    for (size_t u = 0; u < grid.columns; ++u) {
      const size_t at = (v * stride) + u;
      count[at] += (u > 0 ? count[at - 1] : 0) + (v > 0 ? count[at - stride] : 0) -
                   (u > 0 && v > 0 ? count[at - stride - 1] : 0);
      grid.cells[grid.Index(u, v)] = count[at] > 0 ? Cell::kCovered : Cell::kFree;
    }
  }
  return grid;
}

// Floods the free region of `first`, over cells that meet at an edge or a corner. A region that turns out too big or
// open is given up on at once and its cells so far are marked failed, as is any region that meets one.
std::optional<PlaneRect> FloodHole(Grid& grid, size_t first, int max_cells, std::vector<size_t>& region,
                                   std::vector<size_t>& stack) {
  constexpr std::array<std::array<int, 2>, 8> kSteps {
      {{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {1, -1}, {-1, 1}, {-1, -1}}};
  region.clear();
  stack.assign(1, first);
  grid.cells[first] = Cell::kDone;
  size_t u_low = grid.columns;
  size_t u_high = 0;
  size_t v_low = grid.rows;
  size_t v_high = 0;
  int64_t area = 0;
  bool failed = false;
  while (!stack.empty() && !failed) {
    const size_t at = stack.back();
    stack.pop_back();
    region.push_back(at);
    const size_t u = at % grid.columns;
    const size_t v = at / grid.columns;
    u_low = std::min(u_low, u);
    u_high = std::max(u_high, u);
    v_low = std::min(v_low, v);
    v_high = std::max(v_high, v);
    area += static_cast<int64_t>(grid.us[u + 1] - grid.us[u]) * (grid.vs[v + 1] - grid.vs[v]);
    failed = u == 0 || v == 0 || u + 1 == grid.columns || v + 1 == grid.rows ||
             grid.us[u_high + 1] - grid.us[u_low] > max_cells || grid.vs[v_high + 1] - grid.vs[v_low] > max_cells;
    for (const auto& [du, dv] : kSteps) {
      const auto next_u = static_cast<std::ptrdiff_t>(u) + du;
      const auto next_v = static_cast<std::ptrdiff_t>(v) + dv;
      if (next_u < 0 || next_v < 0 || next_u >= static_cast<std::ptrdiff_t>(grid.columns) ||
          next_v >= static_cast<std::ptrdiff_t>(grid.rows)) {
        continue;
      }
      const size_t next = grid.Index(static_cast<size_t>(next_u), static_cast<size_t>(next_v));
      switch (grid.cells[next]) {
        case Cell::kFree:
          grid.cells[next] = Cell::kDone;
          stack.push_back(next);
          break;
        case Cell::kFailed:
          failed = true;
          break;
        case Cell::kCovered:
          [[fallthrough]];
        case Cell::kDone:
          break;
      }
    }
  }
  if (failed) {
    for (const size_t at : region) {
      grid.cells[at] = Cell::kFailed;
    }
    for (const size_t at : stack) {
      grid.cells[at] = Cell::kFailed;
    }
    return std::nullopt;
  }
  const int width = grid.us[u_high + 1] - grid.us[u_low];
  const int height = grid.vs[v_high + 1] - grid.vs[v_low];
  if (area != static_cast<int64_t>(width) * height) {
    return std::nullopt;
  }
  return PlaneRect {.u0 = grid.us[u_low], .v0 = grid.vs[v_low], .u1 = grid.us[u_high + 1], .v1 = grid.vs[v_high + 1]};
}

}  // namespace

std::vector<PlaneRect> RectangularHoles(std::span<const PlaneRect> rects, std::span<const PlaneRect> beside,
                                        int max_cells) {
  // A hole is shut in by four rectangles at least: one along each side.
  constexpr size_t kLeastRectsAroundAHole = 4;
  std::vector<PlaneRect> holes;
  if (rects.size() < kLeastRectsAroundAHole || beside.empty()) {
    return holes;
  }
  Grid grid = GridOf(rects);
  std::vector<size_t> region;
  std::vector<size_t> stack;
  const auto flood_from = [&](size_t u, size_t v) {
    const size_t at = grid.Index(u, v);
    if (grid.cells[at] == Cell::kFree) {
      if (const std::optional<PlaneRect> hole = FloodHole(grid, at, max_cells, region, stack)) {
        holes.push_back(*hole);
      }
    }
  };
  for (const PlaneRect& rect : beside) {
    const size_t u0 = IndexOf(grid.us, rect.u0);
    const size_t u1 = IndexOf(grid.us, rect.u1);
    const size_t v0 = IndexOf(grid.vs, rect.v0);
    const size_t v1 = IndexOf(grid.vs, rect.v1);
    for (size_t v = v0; v < v1; ++v) {
      if (u0 > 0) {
        flood_from(u0 - 1, v);
      }
      if (u1 < grid.columns) {
        flood_from(u1, v);
      }
    }
    for (size_t u = u0; u < u1; ++u) {
      if (v0 > 0) {
        flood_from(u, v0 - 1);
      }
      if (v1 < grid.rows) {
        flood_from(u, v1);
      }
    }
  }
  return holes;
}

}  // namespace z13::station::rooms
