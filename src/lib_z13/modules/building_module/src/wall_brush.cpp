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
#include <cstdlib>
#include <limits>
#include <map>

#include <flecs.h>

#include <primitives/placement.h>

namespace z13::building {

namespace {

using z13::building::primitives::WallGeometry;
using z13::station::Block;
using z13::station::WallAxis;
using z13::station::WallDraft;
using z13::station::WallNode;
using z13::station::WallSegment;

// The axis of the largest displacement; ties go to the lower axis.
WallAxis LongestAxis(const Eigen::Vector3i& offset) {
  int axis {};
  offset.cwiseAbs().maxCoeff(&axis);
  return static_cast<WallAxis>(axis);
}

Block StickBlock(const Eigen::Vector3i& first, WallAxis axis, int length) {
  Eigen::Vector3i size = Eigen::Vector3i::Ones();
  size[static_cast<int>(axis)] = std::max(1, std::abs(length));
  Eigen::Vector3i cell = first;
  cell[static_cast<int>(axis)] += std::min(length, 0);
  return {.spec = {.type_id = z13::station::kCubePrimitiveId, .size = size}, .cell = cell};
}

}  // namespace

Block CellMarker(const Eigen::Vector3i& cell) {
  return StickBlock(cell, WallAxis::kX, 1);
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
    along[static_cast<int>(segment.stick_axis)] = segment.stick_length;
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

std::expected<WallClick, std::string> ClickWall(
    const std::optional<WallDraft>& draft, const Eigen::Vector3i& point, int thickness) {
  if (!draft) {
    return WallClick {.draft = WallDraft {.stage = WallDraft::Stage::kStick, .first = point}};
  }
  if (draft->stage == WallDraft::Stage::kStick) {
    const Eigen::Vector3i offset = point - draft->first;
    const WallAxis axis = LongestAxis(offset);
    if (offset[static_cast<int>(axis)] == 0) {
      return std::unexpected(std::string {"the stick has no length"});
    }
    WallDraft next = *draft;
    next.stage = WallDraft::Stage::kEnd;
    next.stick_axis = axis;
    next.stick_length = offset[static_cast<int>(axis)];
    return WallClick {.draft = next};
  }
  return WallClick {.wall = WallGeometry {
                        .node_a = draft->first, .node_b = point, .stick_axis = draft->stick_axis,
                        .stick_length = draft->stick_length, .thickness = thickness}};
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
    const std::optional<WallDraft>& draft, const Eigen::Vector3i& point, int thickness, const Eigen::Vector3f& eye,
    const z13::building::primitives::Palette& palette) {
  if (!draft) {
    return {.block = CellMarker(point), .makeable = true};
  }
  if (draft->stage == WallDraft::Stage::kStick) {
    const Eigen::Vector3i offset = point - draft->first;
    const WallAxis axis = LongestAxis(offset);
    const int length = offset[static_cast<int>(axis)];
    return {.block = StickBlock(draft->first, axis, length), .makeable = length != 0};
  }
  const auto click = ClickWall(draft, point, thickness);
  if (const auto block = FacingEye(*click->wall, eye, palette)) {
    return {.block = block->block, .makeable = true};
  }
  return {.block = StickBlock(draft->first, draft->stick_axis, draft->stick_length), .makeable = false};
}

}  // namespace z13::building
