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

std::vector<BrushLine> HeightLines(const WallDraft& draft, int length) {
  const Eigen::Vector3f along = Eigen::Vector3f::Unit(Index(draft.stick_axis)) * static_cast<float>(length);
  return {Line(draft.first.cast<float>(), draft.first.cast<float>() + along),
          Line(draft.second.cast<float>(), draft.second.cast<float>() + along)};
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
    Eigen::Vector3i along = Eigen::Vector3i::Zero();
    along[Index(segment.stick_axis)] = segment.stick_length;
    for (const uint32_t id : {segment.node_a, segment.node_b}) {
      if (const auto node = nodes.find(id); node != nodes.end()) {
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
  std::optional<WallAxis> plane_axis;
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

  const Eigen::Vector3f hit = aim.eye + *direction * nearest;
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
    const Aim& aim, const Eigen::Vector3i& first, WallAxis axis, std::span<const Eigen::Vector3i> anchors,
    const WallPicking& picking) {
  const auto direction = RayDirection(aim);
  if (!direction) {
    return std::nullopt;
  }
  // The closest approach of the ray (eye + t * direction) to the line (first + s * axis); `apart`
  // is the line's point minus the ray's.
  const Eigen::Vector3f unit = Eigen::Vector3f::Unit(Index(axis));
  const Eigen::Vector3f apart = first.cast<float>() - aim.eye;
  const float cosine = unit.dot(*direction);
  const float opening = 1.f - cosine * cosine;
  if (opening < kParallelEpsilon) {
    return std::nullopt;
  }
  const float along_ray = (direction->dot(apart) - cosine * unit.dot(apart)) / opening;
  const float along_line = (cosine * direction->dot(apart) - unit.dot(apart)) / opening;
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
      return std::unexpected(std::string {"the view ray meets no plane through the first point, or the stick is empty"});
    }
    WallDraft next = *draft;
    next.stage = WallDraft::Stage::kHeight;
    next.second = stick->point;
    next.stick_axis = stick->axis;
    return WallClick {.draft = next};
  }
  const auto length = PickHeight(aim, draft->first, draft->stick_axis, anchors, picking);
  if (!length) {
    return std::unexpected(std::string {"the view ray finds no length along the axis"});
  }
  return WallClick {.wall = WallGeometry {
                        .node_a = draft->first, .node_b = draft->second, .stick_axis = draft->stick_axis,
                        .stick_length = *length, .thickness = thickness}};
}

std::expected<FacedWall, std::string> FacingEye(
    WallGeometry wall, const Eigen::Vector3f& eye, const z13::building::primitives::Palette& palette) {
  std::expected<FacedWall, std::string> best = std::unexpected(std::string {"the wall has no thickness"});
  float best_distance = std::numeric_limits<float>::max();
  const int thickness = std::abs(wall.thickness);
  for (const int side : {1, -1}) {
    wall.thickness = side * thickness;
    auto block = z13::building::primitives::WallPanelBlock(wall, palette);
    if (!block) {
      if (!best) {
        best = std::unexpected(block.error());
      }
      continue;
    }
    const auto cells = z13::building::primitives::OccupiedCells(*block);
    const float distance =
        (cells.min.cast<float>() + cells.extent.cast<float>() / 2.f - eye).squaredNorm();
    if (!best || distance < best_distance) {
      best = FacedWall {.wall = wall, .block = *block};
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
  const auto length = PickHeight(aim, draft->first, draft->stick_axis, anchors, picking);
  if (!length) {
    return {.block = CellMarker(draft->second), .lines = HeightLines(*draft, 0), .makeable = false};
  }
  const auto click = ClickWall(draft, aim, anchors, picking, thickness);
  if (const auto faced = FacingEye(*click->wall, aim.eye, palette)) {
    return {.block = faced->block, .lines = HeightLines(*draft, *length), .makeable = true};
  }
  return {.block = CellMarker(draft->second), .lines = HeightLines(*draft, *length), .makeable = false};
}

}  // namespace z13::building
