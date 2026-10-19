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
#include <cstdlib>
#include <format>
#include <numeric>

namespace z13::building::primitives {

namespace {

// Beyond the seam a wall keeps at least this much of its run.
constexpr int kMinRemainingCells = 1;
// A panel is at most this long (the palette's max_size_cells); the products below stay within int64.
constexpr int kMaxComponent = 256;

bool IsUsable(const PlanWall& wall) {
  const auto fits = [](const Eigen::Vector2i& vector) {
    return !vector.isZero() && vector.cwiseAbs().maxCoeff() <= kMaxComponent;
  };
  return fits(wall.direction) && fits(wall.offset);
}

Homogeneous LineThrough(const Eigen::Vector2i& point, const Eigen::Vector2i& direction) {
  const Homogeneous from(point.x(), point.y(), 1);
  const Homogeneous toward_infinity(direction.x(), direction.y(), 0);
  return from.cross(toward_infinity);
}

int64_t CrossOf(const Eigen::Vector2i& p, const Eigen::Vector2i& q) {
  Eigen::Matrix2<int64_t> columns;
  columns << p.cast<int64_t>(), q.cast<int64_t>();
  return columns.determinant();
}

int Sign(int64_t value) {
  return (value > 0) - (value < 0);
}

PlanPoint WithPositiveWeight(const Homogeneous& point) {
  return point.z() < 0 ? Homogeneous(-point) : point;
}

SeamLine Reduced(const Homogeneous& line) {
  const int64_t divisor = std::gcd(std::gcd(std::abs(line.x()), std::abs(line.y())), std::abs(line.z()));
  const Homogeneous reduced = line / divisor;
  return (reduced.x() < 0 || (reduced.x() == 0 && reduced.y() < 0)) ? Homogeneous(-reduced) : reduced;
}

// The axis a wall runs along: the longer one, the lower on a tie (as PlanAxesOf).
int RunAxis(const Eigen::Vector2i& direction) {
  return std::abs(direction.x()) >= std::abs(direction.y()) ? 0 : 1;
}

std::optional<std::string> CheckReach(const PlanPoint& point, int max_reach) {
  const int64_t weight = point.z();
  const int64_t reach = std::max(std::abs(point.x()), std::abs(point.y()));
  if (reach > static_cast<int64_t>(max_reach) * weight) {
    return std::format("the joint's tip would reach {} cells from the node, more than the {} allowed",
                       (reach + weight - 1) / weight, max_reach);
  }
  return std::nullopt;
}

// The seam must leave the wall a cell of its run; the setback is the same however long the wall is.
std::optional<std::string> CheckRemaining(const PlanWall& wall, const PlanPoint& point) {
  const int axis = RunAxis(wall.direction);
  const int run = std::abs(wall.direction[axis]);
  const int64_t weight = point.z();
  const int64_t along = Sign(wall.direction[axis]) * point[axis];
  if (along > static_cast<int64_t>(run - kMinRemainingCells) * weight) {
    const int64_t setback = (along + weight - 1) / weight;
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
  return Sign(line.dot(point));
}

std::expected<WallSeam, std::string> PlanSeam(const PlanWall& first, const PlanWall& second, int max_reach) {
  if (first.node != second.node) {
    return std::unexpected(std::string {"the walls don't share a node"});
  }
  if (!IsUsable(first) || !IsUsable(second)) {
    return std::unexpected(std::string {"a wall of no length or thickness, or longer than a panel, has no seam"});
  }
  const int64_t turn = CrossOf(first.direction, second.direction);
  if (turn == 0) {
    if (first.direction.dot(second.direction) > 0) {
      return std::unexpected(std::string {"the walls leave the node the same way"});
    }
    // A straight run: both end across the thickness, at the node.
    const Homogeneous line = LineThrough(Eigen::Vector2i::Zero(), first.offset);
    const PlanPoint node(0, 0, 1);
    return WallSeam {.line = Reduced(line), .inner = node, .outer = node};
  }

  // Each wall's face toward the other wall is the one on the other's side of its direction.
  const int toward = Sign(turn);
  const Homogeneous first_ref = LineThrough(Eigen::Vector2i::Zero(), first.direction);
  const Homogeneous first_off = LineThrough(first.offset, first.direction);
  const Homogeneous second_ref = LineThrough(Eigen::Vector2i::Zero(), second.direction);
  const Homogeneous second_off = LineThrough(second.offset, second.direction);
  const bool first_off_faces = Sign(CrossOf(first.direction, first.offset)) == toward;
  const bool second_off_faces = Sign(CrossOf(second.direction, second.offset)) == -toward;
  const PlanPoint inner = WithPositiveWeight(
      (first_off_faces ? first_off : first_ref).cross(second_off_faces ? second_off : second_ref));
  const PlanPoint outer = WithPositiveWeight(
      (first_off_faces ? first_ref : first_off).cross(second_off_faces ? second_ref : second_off));

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
  return WallSeam {.line = Reduced(inner.cross(outer)), .inner = inner, .outer = outer};
}

}  // namespace z13::building::primitives
