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

#include "cell_light_state.h"

#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <iterator>
#include <optional>
#include <utility>

namespace z13::station::light {

namespace {

using z13::building::primitives::CellBox;

// Below this a node's luminance is no light, and its direction none.
constexpr float kNoLight = 1e-6f;
constexpr float kAdded = 1.f;
// In cubes: one around the station, and 8 m more once blocks are built past it.
constexpr int kOuterMargin = 1;
constexpr int kGrowthMargin = 8;
constexpr float kTakenBack = -1.f;

int FloorDiv(int value, int divisor) {
  return value >= 0 ? value / divisor : -((-value + divisor - 1) / divisor);
}

Eigen::Vector3i FloorDiv(const Eigen::Vector3i& value, int divisor) {
  return {FloorDiv(value.x(), divisor), FloorDiv(value.y(), divisor), FloorDiv(value.z(), divisor)};
}

Eigen::Vector3i CeilDiv(const Eigen::Vector3i& value, int divisor) {
  return -FloorDiv(-value, divisor);
}

CellBox Union(const CellBox& a, const CellBox& b) {
  const Eigen::Vector3i low = a.min.cwiseMin(b.min);
  const Eigen::Vector3i high = a.End().cwiseMax(b.End());
  return {.min = low, .extent = high - low};
}

void Extend(std::optional<CellBox>& box, const CellBox& more) {
  if ((more.extent.array() > 0).all()) {
    box = box ? Union(*box, more) : more;
  }
}

CellBox LampCell(const Lamp& lamp) {
  return {.min = lamp.position.array().floor().cast<int>(), .extent = Eigen::Vector3i::Ones()};
}

bool Within(const CellBox& inner, const CellBox& outer) {
  return (inner.min.array() >= outer.min.array()).all() && (inner.End().array() <= outer.End().array()).all();
}

// Everything given, with `margin` cubes of free space around it: one for the faces turned
// outward, more when the station grows so the next blocks outside don't relayout it again.
LightLayout FitLayout(const std::optional<CellBox>& cells, int margin) {
  if (!cells) {
    return {};
  }
  const CellBox grown = cells->Grown(Eigen::Vector3i::Constant(margin * kNodeCells));
  const Eigen::Vector3i low = FloorDiv(grown.min, kNodeCells);
  const Eigen::Vector3i high = CeilDiv(grown.End(), kNodeCells);
  return {.origin = low * kNodeCells, .cubes = high - low};
}

// The cubes of the layout holding any of `cells`.
CellBox CubesOf(const LightLayout& layout, const CellBox& cells) {
  const Eigen::Vector3i low = FloorDiv(cells.min - layout.origin, kNodeCells);
  const Eigen::Vector3i high = CeilDiv(cells.End() - layout.origin, kNodeCells);
  return CellBox {.min = low, .extent = high - low}.Intersection({.min = Eigen::Vector3i::Zero(), .extent = layout.cubes});
}

CellBox CellsOfCubes(const LightLayout& layout, const CellBox& cubes) {
  return {.min = layout.origin + (cubes.min * kNodeCells), .extent = cubes.extent * kNodeCells};
}

std::optional<CellBox> Bounds(const LightEdits& edits) {
  std::optional<CellBox> bounds;
  std::ranges::for_each(edits.opaque_added, [&bounds](const CellBox& box) { Extend(bounds, box); });
  std::ranges::for_each(edits.lamps_added, [&bounds](const Lamp& lamp) { Extend(bounds, LampCell(lamp)); });
  return bounds;
}

}  // namespace

bool LightEdits::Empty() const {
  return opaque_added.empty() && opaque_removed.empty() && lamps_added.empty() && lamps_removed.empty();
}

CellLight::State::State(std::span<const CellBox> opaque, std::span<const Lamp> lamps) {
  const LightEdits all {.opaque_added = {opaque.begin(), opaque.end()}, .lamps_added = {lamps.begin(), lamps.end()}};
  Rebuild(FitLayout(Bounds(all), kOuterMargin), {}, all, {});
}

void CellLight::State::Rebuild(
    const LightLayout& layout, const LightNodes& nodes, const LightEdits& edits, std::vector<Lamp> lamps) {
  nodes_ = LightNodes(layout);
  nodes_.CopyOpaque(nodes);
  std::ranges::for_each(edits.opaque_removed, [this](const CellBox& box) { nodes_.SetOpaque(box, false); });
  std::ranges::for_each(edits.opaque_added, [this](const CellBox& box) { nodes_.SetOpaque(box, true); });
  nodes_.Relabel({.min = Eigen::Vector3i::Zero(), .extent = layout.cubes});

  const size_t node_count = layout.CubeCount() * kPiecesPerNode;
  light_.assign(node_count, Eigen::Vector3f::Zero());
  directions_.assign(node_count, Eigen::Vector3f::Zero());
  luminance_.assign(node_count, 0.f);
  marks_ = {};
  std::erase_if(lamps, [&edits](const Lamp& lamp) { return std::ranges::contains(edits.lamps_removed, lamp); });
  lamps.insert(lamps.end(), edits.lamps_added.begin(), edits.lamps_added.end());
  lamps_.clear();
  for (const Lamp& lamp : lamps) {
    Flood(lamps_.emplace_back(LampState {.lamp = lamp}));
  }
}

LightChanges CellLight::State::Apply(const LightEdits& edits) {
  if (edits.Empty()) {
    return {};
  }
  const LightLayout& layout = nodes_.Layout();
  if (const auto needed = Bounds(edits);
      needed && !Within(needed->Grown(Eigen::Vector3i::Constant(kNodeCells)), layout.CellBounds())) {
    const LightNodes old = std::move(nodes_);
    const std::optional<CellBox> bounds =
        old.Layout().CubeCount() > 0 ? Union(old.Layout().CellBounds(), *needed) : *needed;
    Rebuild(FitLayout(bounds, kGrowthMargin), old, edits, Lamps());
    return {.resized = true};
  }

  std::optional<CellBox> changed_cells;
  for (const CellBox& box : edits.opaque_removed) {
    nodes_.SetOpaque(box, false);
    Extend(changed_cells, box);
  }
  for (const CellBox& box : edits.opaque_added) {
    nodes_.SetOpaque(box, true);
    Extend(changed_cells, box);
  }
  // Nodes whose ids now mean other pieces: cleared, and flooded again by every lamp reaching them.
  const CellBox relabelled = changed_cells ? CubesOf(layout, *changed_cells) : CellBox {};
  nodes_.Relabel(relabelled);
  const CellBox reaching = relabelled.Grown(Eigen::Vector3i::Ones());

  std::optional<CellBox> lit;
  if (changed_cells) {
    lit = relabelled;
  }
  std::vector<LampState> kept;
  std::vector<size_t> reflooded;
  for (LampState& lamp : lamps_) {
    if (std::ranges::contains(edits.lamps_removed, lamp.lamp)) {
      TakeBack(lamp, relabelled);
      Extend(lit, lamp.reached);
      continue;
    }
    if (changed_cells && lamp.reached.Overlaps(reaching)) {
      TakeBack(lamp, relabelled);
      reflooded.push_back(kept.size());
      Extend(lit, lamp.reached);
    }
    kept.push_back(std::move(lamp));
  }
  lamps_ = std::move(kept);
  ClearCubes(relabelled);
  for (const size_t index : reflooded) {
    Flood(lamps_[index]);
    Extend(lit, lamps_[index].reached);
  }
  for (const Lamp& lamp : edits.lamps_added) {
    Flood(lamps_.emplace_back(LampState {.lamp = lamp}));
    Extend(lit, lamps_.back().reached);
  }
  return {
      .labels = changed_cells ? std::optional(CellsOfCubes(layout, relabelled)) : std::nullopt,
      .light = lit,
  };
}

void CellLight::State::Flood(LampState& lamp) {
  lamp.contributions = FloodLamp(nodes_, lamp.lamp, marks_);
  std::array low {INT_MAX, INT_MAX, INT_MAX};
  std::array high {INT_MIN, INT_MIN, INT_MIN};
  for (const Contribution& reached : lamp.contributions) {
    const std::array cube = nodes_.CubeCoordinates(reached.node);
    for (size_t axis = 0; axis < cube.size(); ++axis) {
      low[axis] = std::min(low[axis], cube[axis]);
      high[axis] = std::max(high[axis], cube[axis] + 1);
    }
  }
  lamp.reached = lamp.contributions.empty()
                     ? CellBox {}
                     : CellBox {.min = {low[0], low[1], low[2]},
                                .extent = {high[0] - low[0], high[1] - low[1], high[2] - low[2]}};
  Add(lamp, kAdded, {});
}

void CellLight::State::TakeBack(const LampState& lamp, const CellBox& kept_cubes) {
  Add(lamp, kTakenBack, kept_cubes);
}

void CellLight::State::Add(const LampState& lamp, float sign, const CellBox& kept_cubes) {
  const Eigen::Vector3f& color = lamp.lamp.color;
  const Eigen::Vector3f& position = lamp.lamp.position;
  const float luminance = color.mean();
  const bool keeps = (kept_cubes.extent.array() > 0).all();
  for (const Contribution& reached : lamp.contributions) {
    if (keeps && nodes_.InCubes(reached.node, kept_cubes)) {
      continue;
    }
    const float weight = sign * reached.weight;
    Eigen::Vector3f& light = light_[reached.node];
    light.x() += color.x() * weight;
    light.y() += color.y() * weight;
    light.z() += color.z() * weight;
    const Eigen::Vector3f& centre = nodes_.Centre(reached.node);
    const float dx = position.x() - centre.x();
    const float dy = position.y() - centre.y();
    const float dz = position.z() - centre.z();
    const float length = std::sqrt((dx * dx) + (dy * dy) + (dz * dz));
    const float lit = luminance * weight;
    if (length > 0.f) {
      Eigen::Vector3f& direction = directions_[reached.node];
      direction.x() += dx / length * lit;
      direction.y() += dy / length * lit;
      direction.z() += dz / length * lit;
    }
    luminance_[reached.node] += lit;
  }
}

void CellLight::State::ClearCubes(const CellBox& cubes) {
  const LightLayout& layout = nodes_.Layout();
  for (int z = cubes.min.z(); z < cubes.End().z(); ++z) {
    for (int y = cubes.min.y(); y < cubes.End().y(); ++y) {
      for (int x = cubes.min.x(); x < cubes.End().x(); ++x) {
        const size_t cube = layout.CubeIndex({x, y, z});
        for (uint8_t piece = 0; piece < kPiecesPerNode; ++piece) {
          const size_t node = LightLayout::NodeIndex(cube, piece);
          light_[node] = Eigen::Vector3f::Zero();
          directions_[node] = Eigen::Vector3f::Zero();
          luminance_[node] = 0.f;
        }
      }
    }
  }
}

Eigen::Vector3f CellLight::State::LightAt(size_t node) const {
  return light_[node].cwiseMax(0.f);
}

Eigen::Vector3f CellLight::State::DirectionAt(size_t node) const {
  return luminance_[node] > kNoLight ? Eigen::Vector3f(directions_[node] / luminance_[node]) : Eigen::Vector3f::Zero();
}

std::vector<Eigen::Vector4f> CellLight::State::Texels(const CellBox& cubes, uint8_t piece, NodeTexel texel) const {
  std::vector<Eigen::Vector4f> texels(static_cast<size_t>(cubes.extent.cast<int64_t>().prod()), Eigen::Vector4f::Zero());
  const LightLayout& layout = nodes_.Layout();
  size_t next = 0;
  for (int z = cubes.min.z(); z < cubes.End().z(); ++z) {
    for (int y = cubes.min.y(); y < cubes.End().y(); ++y) {
      const size_t row = layout.CubeIndex({cubes.min.x(), y, z});
      for (int x = 0; x < cubes.extent.x(); ++x) {
        const size_t node = LightLayout::NodeIndex(row + static_cast<size_t>(x), piece);
        Eigen::Vector4f& out = texels[next++];
        if (texel == NodeTexel::kLight) {
          const Eigen::Vector3f& light = light_[node];
          out.x() = std::max(light.x(), 0.f);
          out.y() = std::max(light.y(), 0.f);
          out.z() = std::max(light.z(), 0.f);
        } else if (luminance_[node] > kNoLight) {
          const Eigen::Vector3f& direction = directions_[node];
          out.x() = direction.x() / luminance_[node];
          out.y() = direction.y() / luminance_[node];
          out.z() = direction.z() / luminance_[node];
        }
      }
    }
  }
  return texels;
}

std::vector<Lamp> CellLight::State::Lamps() const {
  std::vector<Lamp> lamps;
  std::ranges::transform(lamps_, std::back_inserter(lamps), &LampState::lamp);
  return lamps;
}

}  // namespace z13::station::light
