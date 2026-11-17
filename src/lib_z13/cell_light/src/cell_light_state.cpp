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
#include <numeric>
#include <optional>
#include <set>
#include <utility>

#include "cell_borrow.h"

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
// In cubes: the side of the pieces the renderer uploads, a balance between the calls and the texels sent.
constexpr int kUploadTile = 4;
// Lamps are measured in this many chunks, each with its own marks: enough to keep every thread busy.
constexpr size_t kFloodChunks = 16;

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
  std::ranges::for_each(edits.glass_added, [&bounds](const GlassBox& glass) { Extend(bounds, glass.cells); });
  std::ranges::for_each(edits.lamps_added, [&bounds](const Lamp& lamp) { Extend(bounds, LampCell(lamp)); });
  return bounds;
}

}  // namespace

bool LightEdits::Empty() const {
  return opaque_added.empty() && opaque_removed.empty() && glass_added.empty() && glass_removed.empty() &&
         lamps_added.empty() && lamps_removed.empty();
}

CellLight::State::State(std::span<const CellBox> opaque, std::span<const GlassBox> glass, std::span<const Lamp> lamps,
                        z13::station::rooms::ParallelFor parallel)
    : parallel_(std::move(parallel)) {
  const LightEdits all {.opaque_added = {opaque.begin(), opaque.end()},
                        .glass_added = {glass.begin(), glass.end()},
                        .lamps_added = {lamps.begin(), lamps.end()}};
  Rebuild(FitLayout(Bounds(all), kOuterMargin), {}, all, {});
}

void CellLight::State::Rebuild(
    const LightLayout& layout, const LightNodes& nodes, const LightEdits& edits, std::vector<Lamp> lamps) {
  nodes_ = LightNodes(layout);
  nodes_.CopyOpaque(nodes);
  std::ranges::for_each(edits.opaque_removed, [this](const CellBox& box) { nodes_.SetOpaque(box, false); });
  std::ranges::for_each(edits.opaque_added, [this](const CellBox& box) { nodes_.SetOpaque(box, true); });
  for (const GlassBox& glass : edits.glass_removed) {
    nodes_.SetOpaque(glass.cells, false);
    bridges_.Remove(glass);
  }
  for (const GlassBox& glass : edits.glass_added) {
    nodes_.SetOpaque(glass.cells, true);
    bridges_.Add(glass);
  }
  nodes_.Relabel({.min = Eigen::Vector3i::Zero(), .extent = layout.cubes});
  bridges_.RefreshAll(nodes_);

  const size_t node_count = layout.CubeCount() * kPiecesPerNode;
  light_.assign(node_count, Eigen::Vector3f::Zero());
  directions_.assign(node_count, Eigen::Vector3f::Zero());
  luminance_.assign(node_count, 0.f);
  touched_.assign(layout.CubeCount(), 0);
  marks_.assign(kFloodChunks, {});
  std::erase_if(lamps, [&edits](const Lamp& lamp) { return std::ranges::contains(edits.lamps_removed, lamp); });
  lamps.insert(lamps.end(), edits.lamps_added.begin(), edits.lamps_added.end());
  lamps_.clear();
  for (const Lamp& lamp : lamps) {
    lamps_.push_back({.lamp = lamp});
  }
  std::vector<size_t> all(lamps_.size());
  std::iota(all.begin(), all.end(), size_t {});
  Flood(all);
  std::ranges::fill(touched_, uint8_t {});
  lit_.resize(light_.size());
  std::ranges::transform(light_, lit_.begin(), [](const Eigen::Vector3f& light) { return light.maxCoeff() > 0.f; });
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
  for (const GlassBox& glass : edits.glass_removed) {
    nodes_.SetOpaque(glass.cells, false);
    bridges_.Remove(glass);
    Extend(changed_cells, glass.cells);
  }
  for (const GlassBox& glass : edits.glass_added) {
    nodes_.SetOpaque(glass.cells, true);
    bridges_.Add(glass);
    Extend(changed_cells, glass.cells);
  }
  // Nodes whose ids now mean other pieces: cleared, and flooded again by every lamp reaching them.
  const CellBox relabelled = changed_cells ? CubesOf(layout, *changed_cells) : CellBox {};
  nodes_.Relabel(relabelled);
  if (changed_cells) {
    bridges_.Refresh(nodes_, CellsOfCubes(layout, relabelled));
  }
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
    if (changed_cells && lamp.reached.Overlaps(reaching) && ReachesCubes(lamp, reaching)) {
      TakeBack(lamp, relabelled);
      reflooded.push_back(kept.size());
      Extend(lit, lamp.reached);
    }
    kept.push_back(std::move(lamp));
  }
  lamps_ = std::move(kept);
  ClearCubes(relabelled);
  for (const Lamp& lamp : edits.lamps_added) {
    reflooded.push_back(lamps_.size());
    lamps_.push_back({.lamp = lamp});
  }
  Flood(reflooded);
  for (const size_t index : reflooded) {
    Extend(lit, lamps_[index].reached);
  }
  // A borrowed cell follows both the labels and the light around it.
  std::vector<CellBox> tiles;
  if (lit) {
    tiles = TakeTouchedTiles(*lit);
  }
  std::vector<CellBox> cell_tiles;
  if (changed_cells) {
    cell_tiles.push_back(CellsOfCubes(layout, relabelled));
  }
  if (changed_cells) {
    cell_tiles.back() = cell_tiles.back().Grown(Eigen::Vector3i::Constant(kBorrowReach)).Intersection(layout.CellBounds());
  }
  for (const CellBox& flips : LitFlips(tiles)) {
    cell_tiles.push_back(CellsOfCubes(layout, flips));
  }
  return {
      .light_tiles = std::move(tiles),
      .cell_tiles = std::move(cell_tiles),
  };
}

std::vector<CellBox> CellLight::State::LitFlips(std::span<const CellBox> tiles) {
  const LightLayout& layout = nodes_.Layout();
  const CellBox all {.min = Eigen::Vector3i::Zero(), .extent = layout.cubes};
  // The upload tiles, aligned to the layout, that hold a cube a flip is next to: a borrow reaches less than a cube.
  std::set<std::array<int, 3>> aligned;
  for (const CellBox& tile : tiles) {
    for (int z = tile.min.z(); z < tile.End().z(); ++z) {
      for (int y = tile.min.y(); y < tile.End().y(); ++y) {
        for (int x = tile.min.x(); x < tile.End().x(); ++x) {
          const size_t cube = layout.CubeIndex({x, y, z});
          bool flipped = false;
          for (uint8_t piece = 0; piece < kPiecesPerNode; ++piece) {
            const size_t node = LightLayout::NodeIndex(cube, piece);
            const uint8_t now = light_[node].maxCoeff() > 0.f;
            flipped = flipped || lit_[node] != now;
            lit_[node] = now;
          }
          if (!flipped) {
            continue;
          }
          const Eigen::Vector3i low = Eigen::Vector3i(x - 1, y - 1, z - 1).cwiseMax(0);
          const Eigen::Vector3i high = Eigen::Vector3i(x + 1, y + 1, z + 1).cwiseMin(layout.cubes - Eigen::Vector3i::Ones());
          for (int tz = FloorDiv(low.z(), kUploadTile); tz <= FloorDiv(high.z(), kUploadTile); ++tz) {
            for (int ty = FloorDiv(low.y(), kUploadTile); ty <= FloorDiv(high.y(), kUploadTile); ++ty) {
              for (int tx = FloorDiv(low.x(), kUploadTile); tx <= FloorDiv(high.x(), kUploadTile); ++tx) {
                aligned.insert({tx, ty, tz});
              }
            }
          }
        }
      }
    }
  }
  std::vector<CellBox> flips;
  std::ranges::transform(aligned, std::back_inserter(flips), [&all](const std::array<int, 3>& tile) {
    const Eigen::Vector3i min(tile[0] * kUploadTile, tile[1] * kUploadTile, tile[2] * kUploadTile);
    return CellBox {.min = min, .extent = Eigen::Vector3i::Constant(kUploadTile)}.Intersection(all);
  });
  return flips;
}

std::vector<CellBox> CellLight::State::TakeTouchedTiles(const CellBox& cubes) {
  const LightLayout& layout = nodes_.Layout();
  std::vector<CellBox> tiles;
  const Eigen::Vector3i tile_size = Eigen::Vector3i::Constant(kUploadTile);
  for (int z = cubes.min.z(); z < cubes.End().z(); z += kUploadTile) {
    for (int y = cubes.min.y(); y < cubes.End().y(); y += kUploadTile) {
      for (int x = cubes.min.x(); x < cubes.End().x(); x += kUploadTile) {
        const CellBox tile = CellBox {.min = {x, y, z}, .extent = tile_size}.Intersection(cubes);
        bool touched = false;
        for (int tz = tile.min.z(); tz < tile.End().z(); ++tz) {
          for (int ty = tile.min.y(); ty < tile.End().y(); ++ty) {
            const size_t row = layout.CubeIndex({tile.min.x(), ty, tz});
            const auto first = touched_.begin() + static_cast<std::ptrdiff_t>(row);
            const auto last = first + tile.extent.x();
            touched = touched || std::any_of(first, last, [](uint8_t mark) { return mark != 0; });
            std::fill(first, last, uint8_t {});
          }
        }
        if (touched) {
          tiles.push_back(tile);
        }
      }
    }
  }
  return tiles;
}

// Light only gets past an edit through the nodes beside it, so a lamp that reaches none of them is untouched, however
// far its box reaches.
bool CellLight::State::ReachesCubes(const LampState& lamp, const CellBox& cubes) const {
  return std::ranges::any_of(lamp.contributions,
                             [this, &cubes](const Contribution& reached) { return nodes_.InCubes(reached.node, cubes); });
}

void CellLight::State::Flood(std::span<const size_t> indices) {
  if (marks_.empty()) {
    marks_.resize(kFloodChunks);
  }
  const size_t chunks = std::min(indices.size(), marks_.size());
  parallel_(chunks, [this, indices, chunks](size_t chunk) {
    for (size_t next = chunk; next < indices.size(); next += chunks) {
      Measure(lamps_[indices[next]], marks_[chunk]);
    }
  });
  for (const size_t index : indices) {
    Add(lamps_[index], kAdded, {});
  }
}

void CellLight::State::Measure(LampState& lamp, FloodMarks& marks) const {
  lamp.emitter = EmitterOf(nodes_, lamp.lamp);
  lamp.contributions =
      lamp.emitter ? FloodLamp(nodes_, bridges_, lamp.lamp, *lamp.emitter, marks) : std::vector<Contribution> {};
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
}

void CellLight::State::TakeBack(const LampState& lamp, const CellBox& kept_cubes) {
  Add(lamp, kTakenBack, kept_cubes);
}

void CellLight::State::Add(const LampState& lamp, float sign, const CellBox& kept_cubes) {
  const Eigen::Vector3f& color = lamp.lamp.color;
  if (!lamp.emitter) {
    return;
  }
  const Eigen::Vector3f& position = *lamp.emitter;
  const float luminance = color.mean();
  const bool keeps = (kept_cubes.extent.array() > 0).all();
  for (const Contribution& reached : lamp.contributions) {
    if (keeps && nodes_.InCubes(reached.node, kept_cubes)) {
      continue;
    }
    const float weight = sign * reached.weight;
    touched_[reached.node / kPiecesPerNode] = 1;
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
        touched_[cube] = 1;
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

std::vector<CellTexel> CellLight::State::CellTexels(const CellBox& cells) const {
  return CellTexelsOf(nodes_, light_, cells, parallel_);
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

std::vector<Lamp> CellLight::State::ShiningLamps() const {
  std::vector<Lamp> lamps;
  for (const LampState& lamp : lamps_) {
    if (lamp.emitter) {
      lamps.push_back(lamp.lamp);
      lamps.back().position = *lamp.emitter;
    }
  }
  return lamps;
}

}  // namespace z13::station::light
