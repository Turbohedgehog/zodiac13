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

#include "wall_opening_brush.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <vector>

#include <Eigen/Dense>

#include <primitives/wall_bend.h>
#include <primitives/wall_opening.h>
#include <primitives/wall_panel.h>

#include "wall_brush.h"
#include "wall_entities.h"

namespace z13::building {

namespace {

using z13::building::primitives::kMinLintelCells;
using z13::building::primitives::kMinSideSteps;
using z13::station::BrushLine;
using z13::station::OpeningKind;
using z13::station::WallOpening;

// An aim counts as on a wall within this many cells of its faces and its edges.
constexpr float kPickSlackCells = 0.5f;

// Where the aim is on a wall that takes openings: in steps from node a.
struct WallHit {
  const JointWall* wall {};
  int steps {};
  Eigen::Vector3i step = Eigen::Vector3i::Zero();
  Eigen::Vector3i up = Eigen::Vector3i::Zero();
  Eigen::Vector3i thicken = Eigen::Vector3i::Zero();
  float along {};
};

// The point's place on the wall, if it is on it; `distance` is how far off the wall's faces it lies.
std::optional<WallHit> HitOf(const JointWall& wall, const Eigen::Vector3f& point, float& distance) {
  using z13::building::primitives::PlanAxesOf;
  const auto& geometry = wall.geometry;
  const int steps = z13::building::primitives::WallSteps(geometry);
  if (steps == 0 || z13::building::primitives::IsBent(geometry)) {
    return std::nullopt;
  }
  const int stick = static_cast<int>(geometry.stick_a.axis);
  Eigen::Vector3i span = geometry.node_b - geometry.node_a;
  span[stick] = 0;
  const Eigen::Vector3i step = span / steps;
  const auto axes = PlanAxesOf(geometry.stick_a.axis, span);
  const int direction = std::clamp(geometry.stick_a.length, -1, 1);

  const Eigen::Vector3f rel = point - geometry.node_a.cast<float>();
  const float along = rel[axes.major] / static_cast<float>(step[axes.major]);
  const float across = rel[axes.minor] - (along * static_cast<float>(step[axes.minor]));
  const float low = static_cast<float>(std::min(0, geometry.thickness));
  const float high = static_cast<float>(std::max(0, geometry.thickness));
  distance = std::max({low - across, across - high, 0.f});
  const float height = static_cast<float>(direction) * rel[stick];
  const float length = static_cast<float>(std::max(std::abs(geometry.stick_a.length), std::abs(geometry.stick_b.length)));
  const bool within = distance <= kPickSlackCells && along >= -kPickSlackCells &&
                      along <= static_cast<float>(steps) + kPickSlackCells && height >= -kPickSlackCells &&
                      height <= length + kPickSlackCells;
  if (!within) {
    return std::nullopt;
  }
  WallHit hit {.wall = &wall, .steps = steps, .step = step, .along = along};
  hit.up[stick] = direction;
  hit.thicken[axes.minor] = geometry.thickness;
  return hit;
}

std::optional<WallHit> WallUnder(const std::vector<JointWall>& walls, const Eigen::Vector3f& point) {
  std::optional<WallHit> best;
  float best_distance = 0.f;
  for (const JointWall& wall : walls) {
    float distance = 0.f;
    const std::optional<WallHit> hit = HitOf(wall, point, distance);
    if (hit && (!best || distance < best_distance)) {
      best = hit;
      best_distance = distance;
    }
  }
  return best;
}

std::vector<BrushLine> OutlineOf(const WallHit& hit, const WallOpening& opening) {
  const Eigen::Vector3f origin = hit.wall->geometry.node_a.cast<float>();
  const auto corner = [&](int steps, int cells) -> Eigen::Vector3f {
    return origin + (hit.step * steps).cast<float>() + (hit.up * cells).cast<float>();
  };
  const std::array corners = {
      corner(opening.offset, opening.sill), corner(opening.offset + opening.width, opening.sill),
      corner(opening.offset + opening.width, opening.sill + opening.height),
      corner(opening.offset, opening.sill + opening.height)};
  std::vector<BrushLine> lines;
  for (const Eigen::Vector3i& shift : {Eigen::Vector3i::Zero().eval(), hit.thicken}) {
    for (size_t i = 0; i < corners.size(); ++i) {
      lines.push_back({.from = corners[i] + shift.cast<float>(),
                       .to = corners[(i + 1) % corners.size()] + shift.cast<float>()});
    }
  }
  return lines;
}

}  // namespace

std::optional<AimedOpening> AimOpening(
    flecs::world world, const Aim& aim, const BuildTool& tool, const BuildingTuning& tuning) {
  const std::vector<JointWall> walls = JointWallsOf(world);
  const std::optional<WallHit> hit = WallUnder(walls, aim.point);
  if (!hit) {
    return std::nullopt;
  }
  const int width = std::clamp(tool.opening_width, 1, hit->steps);
  const int last_offset = std::max(kMinSideSteps, hit->steps - width - kMinSideSteps);
  WallOpening opening {
      .segment_id = hit->wall->id,
      .kind = tool.opening_kind,
      .offset = std::clamp(static_cast<int>(std::lround(hit->along - (static_cast<float>(width) / 2.f))),
                           kMinSideSteps, last_offset),
      .width = width};
  const auto& geometry = hit->wall->geometry;
  const int length = std::min(
      z13::building::primitives::WallHeightAt(geometry, hit->steps, opening.offset),
      z13::building::primitives::WallHeightAt(geometry, hit->steps, opening.offset + width));
  const int headroom = std::max(1, length - kMinLintelCells);
  if (tool.opening_kind == OpeningKind::kDoor) {
    opening.height = std::clamp(tuning.opening_door_height_cells, 1, headroom);
  } else {
    opening.sill = std::clamp(tuning.opening_window_sill_cells, 1, std::max(1, headroom - 1));
    opening.height = std::clamp(tuning.opening_window_height_cells, 1, std::max(1, headroom - opening.sill));
  }
  return AimedOpening {.opening = opening, .lines = OutlineOf(*hit, opening)};
}

std::optional<flecs::entity> OpeningUnder(flecs::world world, const Aim& aim) {
  const Eigen::Vector3f ray = aim.point - aim.eye;
  if (ray.norm() < kParallelEpsilon) {
    return std::nullopt;
  }
  std::optional<uint32_t> found;
  float nearest = 0.f;
  for (const JointWall& wall : JointWallsOf(world)) {
    const auto& geometry = wall.geometry;
    const int steps = z13::building::primitives::WallSteps(geometry);
    if (wall.openings.empty() || steps == 0) {
      continue;
    }
    const int stick = static_cast<int>(geometry.stick_a.axis);
    Eigen::Vector3i span = geometry.node_b - geometry.node_a;
    span[stick] = 0;
    const Eigen::Vector3f step = (span / steps).cast<float>();
    Eigen::Vector3f up = Eigen::Vector3f::Zero();
    up[stick] = static_cast<float>(std::clamp(geometry.stick_a.length, -1, 1));
    // The plane through the middle of the wall, which the ray meets whichever face it came by.
    Eigen::Vector3f thickness = Eigen::Vector3f::Zero();
    thickness[primitives::PlanAxesOf(geometry.stick_a.axis, span).minor] = static_cast<float>(geometry.thickness);
    const Eigen::Vector3f origin = geometry.node_a.cast<float>() + (thickness / 2.f);
    const Eigen::Vector3f normal = step.cross(up);
    const float facing = ray.dot(normal);
    if (std::abs(facing) < kParallelEpsilon) {
      continue;
    }
    const float distance = (origin - aim.eye).dot(normal) / facing;
    const Eigen::Vector3f met = aim.eye + (ray * distance) - origin;
    const float along = met.dot(step) / step.squaredNorm();
    const float height = met.dot(up);
    for (const WallOpening& opening : wall.openings) {
      const bool inside = distance > 0.f && along >= static_cast<float>(opening.offset) &&
                          along <= static_cast<float>(opening.offset + opening.width) &&
                          height >= static_cast<float>(opening.sill) &&
                          height <= static_cast<float>(opening.sill + opening.height);
      if (inside && (!found || distance < nearest)) {
        found = opening.id;
        nearest = distance;
      }
    }
  }
  return found ? FindWallOpening(world, *found) : std::nullopt;
}

}  // namespace z13::building
