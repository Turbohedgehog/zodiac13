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

#include <primitives/wall_opening.h>

#include <algorithm>
#include <cstdlib>
#include <format>
#include <numeric>
#include <optional>
#include <string>
#include <utility>

#include <primitives/wall_bend.h>

namespace z13::building::primitives {

namespace {

using z13::station::Block;
using z13::station::OpeningKind;
using z13::station::WallOpening;

// What an opening leaves of the wall: a step at each end, a cell above.
constexpr int kMinSideSteps = 1;
constexpr int kMinLintelCells = 1;

int Sign(int value) {
  return (value > 0) - (value < 0);
}

// A part of the wall: between two steps, and two heights up the stick.
struct Stretch {
  int from_step {};
  int to_step {};
  int from_cell {};
  int to_cell {};
  WallEnds ends;
  bool glass {};
};

std::optional<std::string> CheckOpenings(
    const WallGeometry& wall, int steps, std::span<const WallOpening> sorted) {
  if (IsBent(wall)) {
    return "a bent wall takes no openings";
  }
  if (wall.stick_a.length != wall.stick_b.length) {
    return "a wall with sticks of two lengths takes no openings";
  }
  const int length = std::abs(wall.stick_a.length);
  int cursor = kMinSideSteps;
  for (const WallOpening& opening : sorted) {
    if (opening.width < 1 || opening.height < 1 || opening.sill < 0) {
      return "an opening needs a width and a height, and no negative sill";
    }
    if (opening.kind == OpeningKind::kDoor && opening.sill != 0) {
      return "a door reaches the floor";
    }
    if (opening.offset < cursor) {
      return "openings overlap or crowd the end of the wall";
    }
    cursor = opening.offset + opening.width;
    if (cursor > steps - kMinSideSteps) {
      return std::format("the opening ends at step {} of a wall {} steps long", cursor, steps);
    }
    if (opening.sill + opening.height > length - kMinLintelCells) {
      return std::format("the opening reaches {} cells up a wall {} cells high", opening.sill + opening.height, length);
    }
  }
  return std::nullopt;
}

std::vector<Stretch> StretchesOf(
    int steps, int length, const WallEnds& ends, std::span<const WallOpening> sorted) {
  std::vector<Stretch> stretches;
  int cursor = 0;
  for (const WallOpening& opening : sorted) {
    stretches.push_back({.from_step = cursor, .to_step = opening.offset, .to_cell = length,
                         .ends = {.at_a = cursor == 0 ? ends.at_a : std::nullopt}});
    const int top = opening.sill + opening.height;
    if (opening.sill > 0) {
      stretches.push_back({.from_step = opening.offset, .to_step = opening.offset + opening.width,
                           .to_cell = opening.sill});
    }
    if (opening.kind == OpeningKind::kWindow) {
      stretches.push_back({.from_step = opening.offset, .to_step = opening.offset + opening.width,
                           .from_cell = opening.sill, .to_cell = top, .glass = true});
    }
    stretches.push_back({.from_step = opening.offset, .to_step = opening.offset + opening.width,
                         .from_cell = top, .to_cell = length});
    cursor = opening.offset + opening.width;
  }
  stretches.push_back({.from_step = cursor, .to_step = steps, .to_cell = length, .ends = {.at_b = ends.at_b}});
  return stretches;
}

}  // namespace

int WallSteps(const WallGeometry& wall) {
  Eigen::Vector3i span = wall.node_b - wall.node_a;
  span[static_cast<int>(wall.stick_a.axis)] = 0;
  return std::gcd(std::gcd(std::abs(span.x()), std::abs(span.y())), std::abs(span.z()));
}

std::expected<std::vector<Block>, std::string> OpeningPanels(
    const WallGeometry& wall, const WallEnds& ends, std::span<const WallOpening> openings,
    const Palette& palette) {
  const int steps = WallSteps(wall);
  if (steps == 0) {
    return std::unexpected(std::string {"the wall's nodes coincide in its plane"});
  }
  std::vector<WallOpening> sorted(openings.begin(), openings.end());
  std::ranges::sort(sorted, {}, [](const WallOpening& opening) { return std::pair(opening.offset, opening.id); });
  if (const auto problem = CheckOpenings(wall, steps, sorted)) {
    return std::unexpected(*problem);
  }
  if (!palette.Find(z13::station::kGlassPanelPrimitiveId)) {
    return std::unexpected(std::string {"the palette has no wall glass"});
  }

  Eigen::Vector3i span = wall.node_b - wall.node_a;
  const int axis = static_cast<int>(wall.stick_a.axis);
  span[axis] = 0;
  const Eigen::Vector3i step = span / steps;
  const int direction = Sign(wall.stick_a.length);
  std::vector<Block> blocks;
  for (const Stretch& stretch : StretchesOf(steps, std::abs(wall.stick_a.length), ends, sorted)) {
    if (stretch.to_step == stretch.from_step) {
      continue;
    }
    Eigen::Vector3i raised = Eigen::Vector3i::Zero();
    raised[axis] = direction * stretch.from_cell;
    WallGeometry piece = wall;
    piece.node_a = wall.node_a + (step * stretch.from_step) + raised;
    piece.node_b = wall.node_a + (step * stretch.to_step) + raised;
    piece.stick_a.length = piece.stick_b.length = direction * (stretch.to_cell - stretch.from_cell);
    auto block = WallPanelBlock(piece, palette, stretch.ends);
    if (!block) {
      return std::unexpected(std::move(block.error()));
    }
    if (stretch.glass) {
      block->spec.type_id = z13::station::kGlassPanelPrimitiveId;
    }
    blocks.push_back(*block);
  }
  return blocks;
}

}  // namespace z13::building::primitives
