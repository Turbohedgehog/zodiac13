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

#pragma once

#include <span>
#include <vector>

namespace z13::station::rooms {

// A rectangle of cells in the plane of a wall: [u0, u1) by [v0, v1). Plain ints, not Eigen: a plane holds them by
// the hundred, a station by the hundred thousand, and a Debug build keeps every Eigen temporary (CLAUDE.md).
struct PlaneRect {
  int u0 {};
  int v0 {};
  int u1 {};
  int v1 {};

  bool operator==(const PlaneRect&) const = default;
};

// The holes of the union of `rects` that touch one of `beside` (which must be among them): the free regions shut in
// on every side that are rectangles at most `max_cells` wide and high. Free cells that meet at a corner are one
// region. Flooded from the cells next to `beside` on the grid of the rectangles' own edges, giving up on a region as
// soon as it is too big or open, so the cost does not grow with the free space.
std::vector<PlaneRect> RectangularHoles(std::span<const PlaneRect> rects, std::span<const PlaneRect> beside,
                                        int max_cells);

}  // namespace z13::station::rooms
