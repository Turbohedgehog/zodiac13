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

#include <rooms/openings.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <iterator>
#include <functional>
#include <map>

#include <boost/polygon/polygon.hpp>

#include <lib_profile/profile.h>
#include <rooms/room_graph.h>
#include <grid/block_index.h>

#include "cell_boxes.h"

namespace z13::station::rooms {

namespace {

namespace gtl = boost::polygon;
using z13::building::primitives::CellBox;
using z13::building::primitives::kCellAxes;

// Per plane one cell thick across an axis, by its coordinate along it.
template <typename T>
using ByPlane = std::array<std::map<int, T>, kCellAxes>;

// One wall plane to search: each owns its section, so planes are searched side by side.
struct Plane {
  int axis {};
  int at {};
  std::reference_wrapper<gtl::polygon_90_set_data<int>> section;
};

struct Axes {
  int across {};
  int u {};
  int v {};
};

Axes AxesOf(int across) {
  return {.across = across, .u = (across + 1) % kCellAxes, .v = (across + 2) % kCellAxes};
}

CellBox Shifted(CellBox box, int axis, int by) {
  box.min[axis] += by;
  return box;
}

// The box one cell thick next to `box` along `axis`, below it or above it.
CellBox Beside(const CellBox& box, int axis, bool above) {
  CellBox side = box;
  side.min[axis] = above ? box.End()[axis] : box.min[axis] - 1;
  side.extent[axis] = 1;
  return side;
}

bool FreeOnBothSides(const z13::building::grid::BlockIndex& sealed, const CellBox& box, int axis) {
  return !sealed.Overlaps(Shifted(box, axis, -1)) && !sealed.Overlaps(Shifted(box, axis, 1));
}

// Whether the thin boxes of the plane cover `strip`: only they can hold a hole.
bool OnThin(const std::vector<CellBox>& thin, const CellBox& strip) {
  int64_t covered {};
  for (const CellBox& box : thin) {
    if (box.Overlaps(strip)) {
      covered += Volume(box.Intersection(strip));
    }
  }
  return covered == Volume(strip);
}

// The holes of one plane's union of sealed cross-sections that are boxes no wider than
// `max_cells` with two opposite sides on thin boxes: the candidates FindOpenings checks.
std::vector<CellBox> HolesOf(gtl::polygon_90_set_data<int>& section, const std::vector<CellBox>& thin,
                             const Axes& axes, int at, int max_cells) {
  std::vector<gtl::polygon_90_with_holes_data<int>> shapes;
  section.get(shapes);
  std::vector<CellBox> holes;
  for (const auto& shape : shapes) {
    for (auto hole = shape.begin_holes(); hole != shape.end_holes(); ++hole) {
      gtl::rectangle_data<int> bounds;
      gtl::extents(bounds, *hole);
      const int width = gtl::delta(bounds, gtl::HORIZONTAL);
      const int height = gtl::delta(bounds, gtl::VERTICAL);
      if (width > max_cells || height > max_cells ||
          static_cast<int64_t>(gtl::area(*hole)) != static_cast<int64_t>(width) * height) {
        continue;
      }
      CellBox cells {.min = Eigen::Vector3i::Zero(), .extent = Eigen::Vector3i::Ones()};
      cells.min[axes.across] = at;
      cells.min[axes.u] = gtl::xl(bounds);
      cells.min[axes.v] = gtl::yl(bounds);
      cells.extent[axes.u] = width;
      cells.extent[axes.v] = height;
      const auto pair_on_thin = [&](int in_plane) {
        return OnThin(thin, Beside(cells, in_plane, false)) && OnThin(thin, Beside(cells, in_plane, true));
      };
      if (pair_on_thin(axes.u) || pair_on_thin(axes.v)) {
        holes.push_back(cells);
      }
    }
  }
  return holes;
}

}  // namespace

std::vector<PortalSource> FindOpenings(std::span<const CellBox> sealed, std::span<const CellBox> walls,
                                       int max_cells, const ParallelFor& parallel) {
  Z13_PROFILE_ZONE("FindOpenings");
  ByPlane<gtl::polygon_90_set_data<int>> sections;
  for (const CellBox& wall : walls) {
    for (int axis = 0; axis < kCellAxes; ++axis) {
      if (wall.extent[axis] == 1) {
        sections[axis].try_emplace(wall.min[axis]);
      }
    }
  }
  ByPlane<std::vector<CellBox>> thin;
  z13::building::grid::BlockIndex index;
  for (size_t i = 0; i < sealed.size(); ++i) {
    const CellBox& box = sealed[i];
    index.Insert(i, box);
    for (int axis = 0; axis < kCellAxes; ++axis) {
      const Axes axes = AxesOf(axis);
      auto plane = sections[axis].lower_bound(box.min[axis]);
      for (; plane != sections[axis].end() && plane->first < box.End()[axis]; ++plane) {
        plane->second.insert(
            gtl::rectangle_data<int>(box.min[axes.u], box.min[axes.v], box.End()[axes.u], box.End()[axes.v]));
      }
      if (box.extent[axis] == 1 && sections[axis].contains(box.min[axis])) {
        thin[axis][box.min[axis]].push_back(box);
      }
    }
  }
  std::vector<Plane> planes;
  for (int axis = 0; axis < kCellAxes; ++axis) {
    for (auto& [at, section] : sections[axis]) {
      planes.push_back({.axis = axis, .at = at, .section = section});
    }
  }
  const std::vector<CellBox> no_thin;
  std::vector<std::vector<PortalSource>> by_plane(planes.size());
  parallel(planes.size(), [&](size_t i) {
    const auto [axis, at, section] = planes[i];
    const auto thin_here = thin[axis].find(at);
    const std::vector<CellBox>& thin_boxes = thin_here == thin[axis].end() ? no_thin : thin_here->second;
    for (const CellBox& hole : HolesOf(section, thin_boxes, AxesOf(axis), at, max_cells)) {
      const auto held = [&](int in_plane) {
        return FreeOnBothSides(index, Beside(hole, in_plane, false), axis) &&
               FreeOnBothSides(index, Beside(hole, in_plane, true), axis);
      };
      if (FreeOnBothSides(index, hole, axis) && (held(AxesOf(axis).u) || held(AxesOf(axis).v))) {
        by_plane[i].push_back({.opening = hole, .axis = static_cast<Axis>(axis), .visible = true, .passable = true});
      }
    }
  });
  std::vector<PortalSource> found;
  for (std::vector<PortalSource>& plane : by_plane) {
    std::ranges::move(plane, std::back_inserter(found));
  }
  std::ranges::sort(found, PortalSourceBefore);
  return found;
}

}  // namespace z13::station::rooms
