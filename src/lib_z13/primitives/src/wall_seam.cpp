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

#include <primitives/wall_seam.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <format>
#include <numeric>

namespace z13::building::primitives {

namespace {

// Beyond the seam a wall keeps at least this much of its run.
constexpr int kMinRemainingCells = 1;

struct Homogeneous {
  int64_t x {};
  int64_t y {};
  int64_t w {};
};

// Points and lines share the cross product: the line through two points, the point on two lines.
Homogeneous Cross(const Homogeneous& p, const Homogeneous& q) {
  return {p.y * q.w - p.w * q.y, p.w * q.x - p.x * q.w, p.x * q.y - p.y * q.x};
}

Homogeneous LineThrough(const Eigen::Vector2i& point, const Eigen::Vector2i& direction) {
  return {-direction.y(), direction.x(),
          static_cast<int64_t>(direction.y()) * point.x() - static_cast<int64_t>(direction.x()) * point.y()};
}

int64_t CrossOf(const Eigen::Vector2i& p, const Eigen::Vector2i& q) {
  return static_cast<int64_t>(p.x()) * q.y() - static_cast<int64_t>(p.y()) * q.x();
}

int Sign(int64_t value) {
  return (value > 0) - (value < 0);
}

PlanPoint AsPoint(Homogeneous point) {
  if (point.w < 0) {
    point = {-point.x, -point.y, -point.w};
  }
  return {.x = point.x, .y = point.y, .w = point.w};
}

SeamLine Reduced(const Homogeneous& line) {
  const int64_t divisor = std::gcd(std::gcd(std::abs(line.x), std::abs(line.y)), std::abs(line.w));
  Homogeneous reduced {line.x / divisor, line.y / divisor, line.w / divisor};
  if (reduced.x < 0 || (reduced.x == 0 && reduced.y < 0)) {
    reduced = {-reduced.x, -reduced.y, -reduced.w};
  }
  return {.a = reduced.x, .b = reduced.y, .c = reduced.w};
}

// The axis a wall runs along: the longer one, the lower on a tie (as PlanAxesOf).
int RunAxis(const Eigen::Vector2i& direction) {
  return std::abs(direction.x()) >= std::abs(direction.y()) ? 0 : 1;
}

std::optional<std::string> CheckReach(const PlanPoint& point, int max_reach) {
  const int64_t reach = std::max(std::abs(point.x), std::abs(point.y));
  if (reach > static_cast<int64_t>(max_reach) * point.w) {
    return std::format("the joint's tip would reach {} cells from the node, more than the {} allowed",
                       (reach + point.w - 1) / point.w, max_reach);
  }
  return std::nullopt;
}

// The seam must leave the wall a cell of its run; the setback is the same however long the wall is.
std::optional<std::string> CheckRemaining(const PlanWall& wall, const PlanPoint& point) {
  const int axis = RunAxis(wall.direction);
  const int run = std::abs(wall.direction[axis]);
  const int64_t along = Sign(wall.direction[axis]) * (axis == 0 ? point.x : point.y);
  if (along > static_cast<int64_t>(run - kMinRemainingCells) * point.w) {
    const int64_t setback = (along + point.w - 1) / point.w;
    return std::format("the joint takes {} cells of a wall {} cells long; the wall must be at least {} long",
                       setback, run, setback + kMinRemainingCells);
  }
  return std::nullopt;
}

}  // namespace

std::optional<PlanWall> PlanWallAt(const WallGeometry& wall, bool at_a) {
  const int stick = static_cast<int>(wall.stick_axis);
  Eigen::Vector3i span = wall.node_b - wall.node_a;
  span[stick] = 0;
  if (span.isZero() || wall.thickness == 0) {
    return std::nullopt;
  }
  const PlanAxes axes = PlanAxesOf(wall.stick_axis, span);
  const auto flat = [&axes](const Eigen::Vector3i& cell) {
    return Eigen::Vector2i(cell[axes.first], cell[axes.second]);
  };
  const Eigen::Vector2i from = flat(at_a ? wall.node_a : wall.node_b);
  const Eigen::Vector2i to = flat(at_a ? wall.node_b : wall.node_a);
  Eigen::Vector2i offset = Eigen::Vector2i::Zero();
  offset[axes.minor == axes.first ? 0 : 1] = wall.thickness;
  return PlanWall {.node = from, .direction = to - from, .offset = offset};
}

int SideOf(const SeamLine& line, const PlanPoint& point) {
  return Sign(line.a * point.x + line.b * point.y + line.c * point.w);
}

std::expected<WallSeam, std::string> PlanSeam(const PlanWall& first, const PlanWall& second, int max_reach) {
  if (first.node != second.node) {
    return std::unexpected(std::string {"the walls don't share a node"});
  }
  const int64_t turn = CrossOf(first.direction, second.direction);
  if (turn == 0) {
    if (first.direction.dot(second.direction) > 0) {
      return std::unexpected(std::string {"the walls leave the node the same way"});
    }
    // A straight run: both end across the thickness, at the node.
    const Homogeneous line = LineThrough(Eigen::Vector2i::Zero(), first.offset);
    return WallSeam {.line = Reduced(line), .inner = {.x = 0, .y = 0, .w = 1}, .outer = {.x = 0, .y = 0, .w = 1}};
  }

  // Each wall's face toward the other wall is the one on the other's side of its direction.
  const int toward = Sign(turn);
  const Homogeneous first_ref = LineThrough(Eigen::Vector2i::Zero(), first.direction);
  const Homogeneous first_off = LineThrough(first.offset, first.direction);
  const Homogeneous second_ref = LineThrough(Eigen::Vector2i::Zero(), second.direction);
  const Homogeneous second_off = LineThrough(second.offset, second.direction);
  const bool first_off_faces = Sign(CrossOf(first.direction, first.offset)) == toward;
  const bool second_off_faces = Sign(CrossOf(second.direction, second.offset)) == -toward;
  const PlanPoint inner =
      AsPoint(Cross(first_off_faces ? first_off : first_ref, second_off_faces ? second_off : second_ref));
  const PlanPoint outer =
      AsPoint(Cross(first_off_faces ? first_ref : first_off, second_off_faces ? second_ref : second_off));

  for (const PlanPoint& tip : {inner, outer}) {
    if (const auto problem = CheckReach(tip, max_reach)) {
      return std::unexpected(*problem);
    }
    for (const PlanWall* wall : {&first, &second}) {
      if (const auto problem = CheckRemaining(*wall, tip)) {
        return std::unexpected(*problem);
      }
    }
  }
  const Homogeneous line = Cross({inner.x, inner.y, inner.w}, {outer.x, outer.y, outer.w});
  return WallSeam {.line = Reduced(line), .inner = inner, .outer = outer};
}

}  // namespace z13::building::primitives
