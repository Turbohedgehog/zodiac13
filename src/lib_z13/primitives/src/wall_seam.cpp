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

#include <primitives/panel_prism.h>
#include <primitives/placement.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <format>
#include <numeric>
#include <utility>

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
  const int stick = static_cast<int>(wall.stick_a.axis);
  Eigen::Vector3i span = wall.node_b - wall.node_a;
  span[stick] = 0;
  if (span.isZero() || wall.thickness == 0) {
    return std::nullopt;
  }
  const PlanAxes axes = PlanAxesOf(wall.stick_a.axis, span);
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

bool RunsThrough(const PlanWall& first, const PlanWall& second) {
  return CrossOf(first.direction, second.direction) == 0 && first.direction.dot(second.direction) < 0 &&
         first.offset == second.offset;
}

std::expected<WallSeam, std::string> PlanTee(const PlanWall& branch, const PlanWall& through, int max_reach) {
  if (branch.node != through.node) {
    return std::unexpected(std::string {"the walls don't share a node"});
  }
  if (!IsUsable(branch) || !IsUsable(through)) {
    return std::unexpected(std::string {"a wall of no length or thickness, or longer than a panel, has no seam"});
  }
  const int64_t turn = CrossOf(through.direction, branch.direction);
  if (turn == 0) {
    return std::unexpected(std::string {"the walls run along one line"});
  }
  // The branch meets the face of `through` on its own side: the thickened one when it leaves that way.
  const bool toward_offset = Sign(turn) == Sign(CrossOf(through.direction, through.offset));
  const Homogeneous face = toward_offset ? LineThrough(through.offset, through.direction)
                                         : LineThrough(Eigen::Vector2i::Zero(), through.direction);
  const PlanPoint inner = WithPositiveWeight(face.cross(LineThrough(Eigen::Vector2i::Zero(), branch.direction)));
  const PlanPoint outer = WithPositiveWeight(face.cross(LineThrough(branch.offset, branch.direction)));
  for (const PlanPoint& tip : {inner, outer}) {
    if (const auto problem = CheckReach(tip, max_reach)) {
      return std::unexpected(*problem);
    }
    if (const auto problem = CheckRemaining(branch, tip)) {
      return std::unexpected(*problem);
    }
  }
  return WallSeam {.line = Reduced(face), .inner = inner, .outer = outer};
}

bool TeesOverlap(const WallSeam& first, const WallSeam& second, const PlanWall& through) {
  if (first.line != second.line) {
    return false;
  }
  // A tip's place along the face, as the fraction first / second.
  using Place = std::pair<int64_t, int64_t>;
  const auto along = [&through](const PlanPoint& tip) {
    return Place {(tip.x() * through.direction.x()) + (tip.y() * through.direction.y()), tip.z()};
  };
  const auto before = [](const Place& left, const Place& right) {
    return left.first * right.second < right.first * left.second;
  };
  const auto stretch = [&](const WallSeam& seam) {
    const Place one = along(seam.inner);
    const Place other = along(seam.outer);
    return before(other, one) ? std::pair {other, one} : std::pair {one, other};
  };
  const auto [first_low, first_high] = stretch(first);
  const auto [second_low, second_high] = stretch(second);
  return before(first_low, second_high) && before(second_low, first_high);
}

namespace {

using z13::station::Block;
using z13::station::BlockSpec;

// floor((2x + w) / 2w) for w > 0: x / w rounded, halves up.
int64_t RoundedQuotient(int64_t x, int64_t w) {
  const int64_t doubled = (2 * x) + w;
  const int64_t divisor = 2 * w;
  return (doubled / divisor) - ((doubled % divisor) < 0 ? 1 : 0);
}

// The whole cell nearest a point; nothing for one at infinity (w = 0), where lines are parallel.
std::optional<Eigen::Vector2i> NearestCell(const Homogeneous& point) {
  if (point.z() == 0) {
    return std::nullopt;
  }
  const Homogeneous positive = WithPositiveWeight(point);
  return Eigen::Vector2i(static_cast<int>(RoundedQuotient(positive.x(), positive.z())),
                         static_cast<int>(RoundedQuotient(positive.y(), positive.z())));
}

// Where the seam (relative to `node`) crosses the face line through `point` along `direction`,
// on whole cells.
std::optional<Eigen::Vector2i> CrossingOnGrid(
    const SeamLine& seam, const Eigen::Vector2i& node, const Eigen::Vector2i& point, const Eigen::Vector2i& direction) {
  const int64_t absolute = seam.z() - (seam.x() * node.x()) - (seam.y() * node.y());
  const Homogeneous face = LineThrough(point, direction);
  return NearestCell(Homogeneous(seam.x(), seam.y(), absolute).cross(face));
}

// The section's four corners in the plane (reference face near and far, thickened face far and
// near), each end cut by its seam if it has one.
std::optional<std::array<Eigen::Vector2i, 4>> PlanCorners(const PlanWall& wall_a, const PlanWall& wall_b,
                                                         const WallEnds& ends) {
  const Eigen::Vector2i direction = wall_a.direction;
  std::array<Eigen::Vector2i, 4> corners {wall_a.node, wall_b.node, wall_b.node + wall_b.offset,
                                           wall_a.node + wall_a.offset};
  if (ends.at_a) {
    const auto reference = CrossingOnGrid(ends.at_a->line, wall_a.node, wall_a.node, direction);
    const auto thickened = CrossingOnGrid(ends.at_a->line, wall_a.node, wall_a.node + wall_a.offset, direction);
    if (!reference || !thickened) {
      return std::nullopt;
    }
    corners[0] = *reference;
    corners[3] = *thickened;
  }
  if (ends.at_b) {
    const auto reference = CrossingOnGrid(ends.at_b->line, wall_b.node, wall_a.node, direction);
    const auto thickened = CrossingOnGrid(ends.at_b->line, wall_b.node, wall_a.node + wall_a.offset, direction);
    if (!reference || !thickened) {
      return std::nullopt;
    }
    corners[1] = *reference;
    corners[2] = *thickened;
  }
  return corners;
}

// The stick at each corner: a's, grown toward b's in proportion to the corner's distance from
// a along the wall. Nothing if one is left with no length or turned about.
std::optional<std::array<int, 4>> StickLengths(
    const WallGeometry& wall, const PlanWall& plan_a, const std::array<Eigen::Vector2i, 4>& corners) {
  const int64_t along = CrossOf(plan_a.direction, plan_a.offset);
  std::array<int, 4> sticks;
  for (size_t i = 0; i < corners.size(); ++i) {
    const int64_t reached = CrossOf(corners[i] - plan_a.node, plan_a.offset);
    const int64_t grown = static_cast<int64_t>(wall.stick_b.length - wall.stick_a.length) * Sign(along) * reached;
    sticks[i] = wall.stick_a.length + static_cast<int>(RoundedQuotient(grown, std::abs(along)));
    if (sticks[i] == 0 || (sticks[i] > 0) != (wall.stick_a.length > 0)) {
      return std::nullopt;
    }
  }
  return sticks;
}

// Whether the corners, in order, turn the same way all round: a convex section with some area.
bool IsConvex(const std::array<Eigen::Vector2i, 4>& corners) {
  int direction = 0;
  for (size_t i = 0; i < corners.size(); ++i) {
    const Eigen::Vector2i in = corners[(i + 1) % corners.size()] - corners[i];
    const Eigen::Vector2i out = corners[(i + 2) % corners.size()] - corners[(i + 1) % corners.size()];
    const int turn = Sign(CrossOf(in, out));
    if (turn == 0 || (direction != 0 && turn != direction)) {
      return false;
    }
    direction = turn;
  }
  return true;
}

}  // namespace

std::expected<Block, std::string> WallPanelBlock(
    const WallGeometry& wall, const Palette& palette, const WallEnds& ends) {
  auto square = WallPanelBlock(wall, palette);
  if (!square || (!ends.at_a && !ends.at_b)) {
    return square;
  }
  return CutPanelBlock(wall, palette, ends);
}

std::expected<Block, std::string> CutPanelBlock(
    const WallGeometry& wall, const Palette& palette, const WallEnds& ends) {
  const auto plan_a = PlanWallAt(wall, true);
  const auto plan_b = PlanWallAt(wall, false);
  const auto corners = plan_a && plan_b ? PlanCorners(*plan_a, *plan_b, ends) : std::nullopt;
  if (!corners) {
    return std::unexpected(std::string {"a seam doesn't cross the wall's faces"});
  }
  if (!IsConvex(*corners)) {
    return std::unexpected(std::string {"the seams leave the wall no convex body"});
  }
  const auto sticks = StickLengths(wall, *plan_a, *corners);
  if (!sticks) {
    return std::unexpected(std::string {"a seam leaves the wall a corner with no stick"});
  }

  const int stick = static_cast<int>(wall.stick_a.axis);
  Eigen::Vector3i span = wall.node_b - wall.node_a;
  span[stick] = 0;
  const PlanAxes axes = PlanAxesOf(wall.stick_a.axis, span);
  const auto along_axis = [&axes](const Eigen::Vector2i& corner, int axis) {
    return axis == axes.first ? corner.x() : corner.y();
  };
  std::array<PrismCorner, 4> prism;
  for (size_t i = 0; i < prism.size(); ++i) {
    prism[i] = {.plan = {along_axis((*corners)[i], axes.major), along_axis((*corners)[i], axes.minor)},
                .from = wall.node_a[stick],
                .to = wall.node_a[stick] + (*sticks)[i]};
  }
  return PrismBlock({.x_axis = axes.major, .y_axis = stick, .z_axis = axes.minor}, prism, palette);
}

}  // namespace z13::building::primitives
