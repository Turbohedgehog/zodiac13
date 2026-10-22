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

#include "wall_brush.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <map>

#include <flecs.h>

#include <primitives/placement.h>
#include <primitives/wall_bend.h>

namespace z13::building {

namespace {

using z13::building::primitives::WallGeometry;
using z13::station::Block;
using z13::station::BrushLine;
using z13::station::WallAxis;
using z13::station::WallDraft;
using z13::station::WallNode;
using z13::station::WallSegment;

// Below this a ray counts as parallel to a plane or line.
constexpr float kParallelEpsilon = 1e-4f;
// An aimed face counts as lying in a plane through the first point within this many cells of it.
constexpr float kOnPlaneCells = 0.5f;
// The arrow marking the axis of the third click: its length and the reach of its head, in cells.
constexpr float kArrowCells = 8.f;
constexpr float kArrowHeadCells = 1.5f;

int Index(WallAxis axis) {
  return static_cast<int>(axis);
}

std::optional<Eigen::Vector3f> RayDirection(const Aim& aim) {
  const Eigen::Vector3f toward = aim.point - aim.eye;
  return toward.norm() > kParallelEpsilon ? std::optional(Eigen::Vector3f(toward.normalized())) : std::nullopt;
}

bool OnAxisLine(const Eigen::Vector3i& anchor, const Eigen::Vector3i& first, WallAxis axis) {
  Eigen::Vector3i offset = anchor - first;
  offset[Index(axis)] = 0;
  return offset.isZero();
}

BrushLine Line(const Eigen::Vector3f& from, const Eigen::Vector3f& to) {
  return {.from = from, .to = to};
}

// The stick, and from its middle an arrow along `axis` toward the eye: the way the third
// click measures.
std::vector<BrushLine> StickLines(const Eigen::Vector3i& first, const StickPick& pick, const Eigen::Vector3f& eye) {
  const Eigen::Vector3f from = first.cast<float>();
  const Eigen::Vector3f to = pick.point.cast<float>();
  const Eigen::Vector3f middle = (from + to) / 2.f;
  const Eigen::Vector3f axis = Eigen::Vector3f::Unit(Index(pick.axis));
  const float side = eye[Index(pick.axis)] >= middle[Index(pick.axis)] ? 1.f : -1.f;
  const Eigen::Vector3f tip = middle + axis * (side * kArrowCells);
  const Eigen::Vector3f back = axis * (side * kArrowHeadCells);
  const Eigen::Vector3f along = (to - from).normalized() * kArrowHeadCells;
  return {Line(from, to), Line(middle, tip), Line(tip, tip - back + along), Line(tip, tip - back - along)};
}

// The line from `node` along `axis` by `length`.
BrushLine StickLine(const Eigen::Vector3i& node, WallAxis axis, int length) {
  const Eigen::Vector3f along = Eigen::Vector3f::Unit(Index(axis)) * static_cast<float>(length);
  return Line(node.cast<float>(), node.cast<float>() + along);
}

std::vector<BrushLine> HeightLines(const WallDraft& draft, int length) {
  return {StickLine(draft.first, draft.stick_axis, length), StickLine(draft.second, draft.stick_axis, length)};
}

// The first node's stick, and the second's where the fourth click would put it.
std::vector<BrushLine> TipLines(const WallDraft& draft, const std::optional<z13::station::WallStick>& tip) {
  std::vector<BrushLine> lines {StickLine(draft.first, draft.stick_axis, draft.stick_length)};
  if (tip) {
    lines.push_back(StickLine(draft.second, tip->axis, tip->length));
  }
  return lines;
}

// The axis of the face the aim rests on, when that face lies in the plane through `first`.
std::optional<WallAxis> AimedPlane(const Aim& aim, const Eigen::Vector3i& first) {
  for (const WallAxis axis : {WallAxis::kX, WallAxis::kY, WallAxis::kZ}) {
    const int index = Index(axis);
    if (aim.normal[index] != 0 && std::abs(aim.point[index] - static_cast<float>(first[index])) <= kOnPlaneCells) {
      return axis;
    }
  }
  return std::nullopt;
}

}  // namespace

Block CellMarker(const Eigen::Vector3i& cell) {
  return {.spec = {.type_id = z13::station::kCubePrimitiveId, .size = Eigen::Vector3i::Ones()}, .cell = cell};
}

Eigen::Vector3i WallClickPoint(const Aim& aim, std::span<const Eigen::Vector3i> anchors, float snap_cells) {
  const auto nearest = std::ranges::min_element(anchors, {}, [&aim](const Eigen::Vector3i& anchor) {
    return (anchor.cast<float>() - aim.point).squaredNorm();
  });
  if (nearest != anchors.end() && (nearest->cast<float>() - aim.point).norm() <= snap_cells) {
    return *nearest;
  }
  return aim.point.array().round().cast<int>();
}

std::vector<Eigen::Vector3i> WallAnchors(flecs::world world) {
  std::map<uint32_t, Eigen::Vector3i> nodes;
  std::vector<Eigen::Vector3i> anchors;
  world.query<const WallNode>().each([&](const WallNode& node) {
    nodes.emplace(node.id, node.cell);
    anchors.push_back(node.cell);
  });
  world.query<const WallSegment>().each([&](const WallSegment& segment) {
    for (const bool at_b : {false, true}) {
      const z13::station::WallStick& stick = at_b ? segment.stick_b : segment.stick_a;
      Eigen::Vector3i along = Eigen::Vector3i::Zero();
      along[Index(stick.axis)] = stick.length;
      if (const auto node = nodes.find(at_b ? segment.node_b : segment.node_a); node != nodes.end()) {
        anchors.push_back(node->second + along);
      }
    }
  });
  // Flecs' order differs between participants, and a tie between two anchors must not.
  std::ranges::sort(anchors, [](const Eigen::Vector3i& a, const Eigen::Vector3i& b) {
    return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end());
  });
  return anchors;
}

std::optional<StickPick> PickStick(
    const Aim& aim, const Eigen::Vector3i& first, std::span<const Eigen::Vector3i> anchors,
    const WallPicking& picking) {
  const auto direction = RayDirection(aim);
  if (!direction) {
    return std::nullopt;
  }
  // A face aimed at in a plane through `first` is the plane: pointing at the floor or at the wall
  // chooses between them where they meet. In the air, the nearest plane the ray crosses.
  std::optional<WallAxis> plane_axis = AimedPlane(aim, first);
  Eigen::Vector3f hit = aim.point;
  if (!plane_axis) {
    float nearest = picking.reach_cells;
    for (const WallAxis axis : {WallAxis::kX, WallAxis::kY, WallAxis::kZ}) {
      const float slope = (*direction)[Index(axis)];
      if (std::abs(slope) < kParallelEpsilon) {
        continue;
      }
      const float distance = (static_cast<float>(first[Index(axis)]) - aim.eye[Index(axis)]) / slope;
      if (distance > 0.f && distance <= nearest) {
        plane_axis = axis;
        nearest = distance;
      }
    }
    if (!plane_axis) {
      return std::nullopt;
    }
    hit = aim.eye + *direction * nearest;
  }
  StickPick pick {.point = hit.array().round().cast<int>(), .axis = *plane_axis};
  pick.point[Index(pick.axis)] = first[Index(pick.axis)];
  float anchor_distance = picking.snap_cells;
  for (const Eigen::Vector3i& anchor : anchors) {
    const float distance = (anchor.cast<float>() - hit).norm();
    if (anchor[Index(pick.axis)] == first[Index(pick.axis)] && distance <= anchor_distance) {
      pick.point = anchor;
      anchor_distance = distance;
    }
  }
  return pick.point == first ? std::nullopt : std::optional(pick);
}

std::optional<int> PickHeight(
    const Aim& aim, const Eigen::Vector3i& first, const Eigen::Vector3i& second, WallAxis axis,
    std::span<const Eigen::Vector3i> anchors, const WallPicking& picking) {
  const auto direction = RayDirection(aim);
  if (!direction) {
    return std::nullopt;
  }
  // The height is where the ray meets the wall's own plane (the stick and the axis); along that
  // plane, its closest approach to the line (first + s * axis) stands in. `apart` is the line's
  // point minus the ray's.
  const Eigen::Vector3f unit = Eigen::Vector3f::Unit(Index(axis));
  const Eigen::Vector3f apart = first.cast<float>() - aim.eye;
  const Eigen::Vector3f normal = (second - first).cast<float>().cross(unit);
  const float facing = normal.dot(*direction);
  float along_ray {};
  float along_line {};
  if (std::abs(facing) > kParallelEpsilon * normal.norm()) {
    along_ray = normal.dot(apart) / facing;
    along_line = (aim.eye + (along_ray * *direction) - first.cast<float>()).dot(unit);
  } else {
    const float cosine = unit.dot(*direction);
    const float opening = 1.f - cosine * cosine;
    if (opening < kParallelEpsilon) {
      return std::nullopt;
    }
    along_ray = (direction->dot(apart) - cosine * unit.dot(apart)) / opening;
    along_line = (cosine * direction->dot(apart) - unit.dot(apart)) / opening;
  }
  if (along_ray <= 0.f || along_ray > picking.reach_cells) {
    return std::nullopt;
  }

  int length = static_cast<int>(std::lround(along_line));
  float anchor_distance = picking.snap_cells;
  for (const Eigen::Vector3i& anchor : anchors) {
    const float distance = std::abs(static_cast<float>(anchor[Index(axis)] - first[Index(axis)]) - along_line);
    if (OnAxisLine(anchor, first, axis) && distance <= anchor_distance) {
      length = anchor[Index(axis)] - first[Index(axis)];
      anchor_distance = distance;
    }
  }
  return length != 0 ? std::optional(length) : std::nullopt;
}

std::optional<z13::station::WallStick> PickTip(
    const Aim& aim, const Eigen::Vector3i& second, std::span<const Eigen::Vector3i> anchors,
    const WallPicking& picking) {
  const auto pick = PickStick(aim, second, anchors, picking);
  if (!pick) {
    return std::nullopt;
  }
  const Eigen::Vector3i offset = pick->point - second;
  Eigen::Index axis = 0;
  offset.cwiseAbs().maxCoeff(&axis);
  return z13::station::WallStick {.axis = static_cast<WallAxis>(axis), .length = offset[axis]};
}

std::expected<WallClick, std::string> ClickWall(
    const std::optional<WallDraft>& draft, const Aim& aim, std::span<const Eigen::Vector3i> anchors,
    const WallPicking& picking, int thickness) {
  if (!draft) {
    return WallClick {.draft = WallDraft {.stage = WallDraft::Stage::kStick,
                                          .first = WallClickPoint(aim, anchors, picking.snap_cells)}};
  }
  if (draft->stage == WallDraft::Stage::kStick) {
    const auto stick = PickStick(aim, draft->first, anchors, picking);
    if (!stick) {
      return std::unexpected(
          std::string {"the view ray meets no plane through the first point, or the stick is empty"});
    }
    WallDraft next = *draft;
    next.stage = WallDraft::Stage::kHeight;
    next.second = stick->point;
    next.stick_axis = stick->axis;
    return WallClick {.draft = next};
  }
  if (draft->stage == WallDraft::Stage::kHeight) {
    const auto length = PickHeight(aim, draft->first, draft->second, draft->stick_axis, anchors, picking);
    if (!length) {
      return std::unexpected(std::string {"the view ray finds no length along the axis"});
    }
    if (draft->bent) {
      WallDraft next = *draft;
      next.stage = WallDraft::Stage::kTip;
      next.stick_length = *length;
      return WallClick {.draft = next};
    }
    return WallClick {.wall = WallGeometry {
                          .node_a = draft->first, .node_b = draft->second,
                          .stick_a = {.axis = draft->stick_axis, .length = *length},
                          .stick_b = {.axis = draft->stick_axis, .length = *length}, .thickness = thickness}};
  }
  const auto tip = PickTip(aim, draft->second, anchors, picking);
  if (!tip) {
    return std::unexpected(std::string {"the view ray meets no plane through the second point, or the stick is empty"});
  }
  WallGeometry wall {.node_a = draft->first, .node_b = draft->second,
                     .stick_a = {.axis = draft->stick_axis, .length = draft->stick_length}, .stick_b = *tip,
                     .thickness = thickness};
  if (z13::building::primitives::IsBent(wall)) {
    wall.diagonal = z13::building::primitives::DefaultDiagonal(wall);
    if (draft->flipped) {
      wall.diagonal = z13::building::primitives::Flipped(wall.diagonal);
    }
  }
  return WallClick {.wall = wall};
}

std::expected<FacedWall, std::string> FacingEye(
    WallGeometry wall, const Eigen::Vector3f& eye, const z13::building::primitives::Palette& palette) {
  std::expected<FacedWall, std::string> best = std::unexpected(std::string {"the wall has no thickness"});
  float best_distance = std::numeric_limits<float>::max();
  // The tool's thickness is a wish: an oblique wall needs more to be airtight.
  const int thickness =
      std::max(std::abs(wall.thickness), z13::building::primitives::MinWallThickness(wall).value_or(1));
  for (const int side : {1, -1}) {
    wall.thickness = side * thickness;
    auto blocks = z13::building::primitives::WallPanelBlocks(wall, palette);
    if (!blocks) {
      if (!best) {
        best = std::unexpected(blocks.error());
      }
      continue;
    }
    Eigen::Vector3f low = Eigen::Vector3f::Constant(std::numeric_limits<float>::max());
    Eigen::Vector3f high = Eigen::Vector3f::Constant(std::numeric_limits<float>::lowest());
    for (const z13::station::Block& block : *blocks) {
      const auto cells = z13::building::primitives::OccupiedCells(block);
      low = low.cwiseMin(cells.min.cast<float>());
      high = high.cwiseMax(cells.End().cast<float>());
    }
    const float distance = ((low + high) / 2.f - eye).squaredNorm();
    if (!best || distance < best_distance) {
      best = FacedWall {.wall = wall, .blocks = *blocks};
      best_distance = distance;
    }
  }
  return best;
}

WallPreview PreviewWall(
    const std::optional<WallDraft>& draft, const Aim& aim, std::span<const Eigen::Vector3i> anchors,
    const WallPicking& picking, int thickness, const z13::building::primitives::Palette& palette) {
  const Eigen::Vector3i hovered = WallClickPoint(aim, anchors, picking.snap_cells);
  if (!draft) {
    return {.block = CellMarker(hovered), .makeable = true};
  }
  if (draft->stage == WallDraft::Stage::kStick) {
    const auto stick = PickStick(aim, draft->first, anchors, picking);
    if (!stick) {
      return {.block = CellMarker(hovered), .makeable = false};
    }
    return {.block = CellMarker(stick->point), .lines = StickLines(draft->first, *stick, aim.eye), .makeable = true};
  }
  std::vector<BrushLine> lines;
  if (draft->stage == WallDraft::Stage::kHeight) {
    const auto length = PickHeight(aim, draft->first, draft->second, draft->stick_axis, anchors, picking);
    if (!length) {
      return {.block = CellMarker(draft->second), .makeable = false};
    }
    lines = HeightLines(*draft, *length);
  } else {
    const auto tip = PickTip(aim, draft->second, anchors, picking);
    lines = TipLines(*draft, tip);
    if (!tip) {
      return {.block = CellMarker(draft->second), .lines = lines, .makeable = false};
    }
  }
  const auto click = ClickWall(draft, aim, anchors, picking, thickness);
  if (click && click->draft) {
    return {.block = CellMarker(draft->second), .lines = lines, .makeable = true};
  }
  if (const auto faced = click && click->wall ? FacingEye(*click->wall, aim.eye, palette)
                                              : std::unexpected(std::string {"no wall"})) {
    return {.block = faced->blocks.front(),
            .parts = {faced->blocks.begin() + 1, faced->blocks.end()},
            .lines = lines,
            .makeable = true,
            .wall = faced->wall};
  }
  return {.block = CellMarker(draft->second), .lines = lines, .makeable = false};
}

}  // namespace z13::building
