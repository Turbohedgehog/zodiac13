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

#include "cell_lighting_state.h"

#include <algorithm>
#include <array>
#include <format>
#include <functional>
#include <iterator>
#include <ranges>
#include <unordered_set>
#include <utility>

#include <primitives/draw_order.h>
#include <primitives/lights.h>
#include <primitives/occupancy.h>

#include "../platform/sdl_platform.h"
#include "../tools/math_convert.h"

namespace z13::raylib {

namespace {

using z13::building::primitives::CellBox;
using z13::station::light::CellLight;
using z13::station::light::GlassBox;
using z13::station::light::Lamp;
using z13::station::light::LightEdits;
using z13::station::light::NodeTexel;
using z13::station::light::kPiecesPerNode;

// An odd prime, so the turn spreads the shape's hash instead of adding to it.
constexpr size_t kOrientationHashStride = 31;

// The usual hash combine's constants.
constexpr uint64_t kHashCombine = 0x9e3779b97f4a7c15ULL;
constexpr unsigned kHashShiftLeft = 6;
constexpr unsigned kHashShiftRight = 2;

void Mix(uint64_t& hash, int64_t value) {
  hash ^= static_cast<uint64_t>(value) + kHashCombine + (hash << kHashShiftLeft) + (hash >> kHashShiftRight);
}

uint64_t FingerprintOf(const z13::station::Block& block) {
  uint64_t hash {};
  const auto& spec = block.spec;
  Mix(hash, spec.type_id);
  Mix(hash, static_cast<int64_t>(spec.orientation));
  Mix(hash, spec.slant);
  for (const Eigen::Index axis : {0, 1, 2}) {
    Mix(hash, spec.size[axis]);
    Mix(hash, block.cell[axis]);
  }
  std::ranges::for_each(spec.corners, [&hash](int value) { Mix(hash, value); });
  std::ranges::for_each(spec.face_cuts, [&hash](int value) { Mix(hash, value); });
  Mix(hash, static_cast<int64_t>(block.grid));
  return hash;
}

// Lamps lit by the first build, and by each sync after it: a flood costs a few milliseconds, so the rest wait for the
// next frames instead of holding up one.
constexpr size_t kFirstLampBatch = 48;
// The lamps a sync lights at first, and at most per call after (a call takes fewer when lamps cost much).
constexpr size_t kLampBatch = 6;
constexpr size_t kMaxLampBatch = 24;
// What lighting lamps may take of a frame: a lamp costs about a millisecond in Release and six in Debug.
constexpr double kLampBudgetMs = 6.0;
constexpr double kLampCostSmoothing = 0.5;
constexpr double kMinLampCostMs = 1e-3;

constexpr std::array kNodeTexels {NodeTexel::kLight, NodeTexel::kDirection};

bool BoxBefore(const CellBox& a, const CellBox& b) {
  const auto key = [](const CellBox& box) {
    return std::array {box.min.x(), box.min.y(), box.min.z(), box.extent.x(), box.extent.y(), box.extent.z()};
  };
  return key(a) < key(b);
}

bool GlassBefore(const GlassBox& a, const GlassBox& b) {
  return BoxBefore(a.cells, b.cells) || (a.cells == b.cells && a.transmission < b.transmission);
}

bool LampBefore(const Lamp& a, const Lamp& b) {
  const auto key = [](const Lamp& lamp) {
    return std::array {lamp.position.x(), lamp.position.y(), lamp.position.z(), lamp.behind.x(),
                       lamp.behind.y(),   lamp.behind.z(),   lamp.color.x(),    lamp.color.y(),
                       lamp.color.z(),    lamp.radius};
  };
  return key(a) < key(b);
}

// At most `count` of the lamps, the nearest to `focus` (a lamp stands where it shines from, in cells), in lamp order.
std::vector<Lamp> NearestLamps(std::vector<Lamp> lamps, const Eigen::Vector3f& focus, size_t count) {
  if (lamps.size() > count) {
    const auto distance = [&focus](const Lamp& lamp) { return (lamp.position - focus).squaredNorm(); };
    std::ranges::nth_element(lamps, lamps.begin() + static_cast<std::ptrdiff_t>(count), {}, distance);
    lamps.resize(count);
  }
  std::ranges::sort(lamps, LampBefore);
  return lamps;
}

// What `after` has that `before` lacks; both sorted by `before_order`.
template <typename T, typename Order>
std::vector<T> Missing(const std::vector<T>& after, const std::vector<T>& before, Order before_order) {
  std::vector<T> missing;
  std::ranges::set_difference(after, before, std::back_inserter(missing), before_order);
  return missing;
}

// The light's volume of colors: per texel kind and piece, the cubes stacked along z.
CellBox ColorRegion(const CellBox& cubes, const Eigen::Vector3i& all_cubes, size_t kind, uint8_t piece) {
  CellBox region = cubes;
  region.min.z() += static_cast<int>((kind * kPiecesPerNode) + piece) * all_cubes.z();
  return region;
}

}  // namespace

CellLighting::State::~State() {
  if (IsGlContextAlive()) {
    ReleaseVolumes();
  }
}

bool CellLighting::State::BuiltFrom(z13::building::primitives::OptionalPalette palette) const {
  return light_ && palette_hash_ == (palette ? std::optional(palette->get().hash) : std::nullopt);
}

bool CellLighting::State::SyncedWith(z13::building::primitives::OptionalPalette palette,
                                     const EmergencyLamps& emergency) const {
  return BuiltFrom(palette) && !lamps_pending_ && emergency_percent_ == emergency.percent &&
         std::ranges::equal(dimmed_, emergency.dimmed);
}

bool CellLighting::State::OnlyLampsPending(z13::building::primitives::OptionalPalette palette,
                                           const EmergencyLamps& emergency) const {
  return BuiltFrom(palette) && lamps_pending_ && emergency_percent_ == emergency.percent &&
         std::ranges::equal(dimmed_, emergency.dimmed);
}

size_t SealedBoxesKeyHash::operator()(const SealedBoxesKey& key) const {
  return z13::building::primitives::BlockShapeKeyHash {}(key.shape) * kOrientationHashStride +
         static_cast<size_t>(key.orientation);
}

SealedBoxesKey CellLighting::State::SealedKeyOf(const z13::station::Block& block) {
  return {.shape = z13::building::primitives::ShapeKeyOf(block.spec), .orientation = block.spec.orientation};
}

std::vector<CellBox> CellLighting::State::SealedBoxesOf(const z13::station::Block& block) const {
  std::vector<CellBox> boxes = sealed_.at(SealedKeyOf(block));
  for (CellBox& box : boxes) {
    box.min += block.cell;
  }
  return boxes;
}

void CellLighting::State::BoxesOf(std::span<const KeyedBlock> blocks,
                                  z13::building::primitives::OptionalPalette palette,
                                  const z13::station::rooms::ParallelFor& parallel, std::vector<CellBox>& opaque,
                                  std::vector<GlassBox>& glass) {
  std::unordered_set<SealedBoxesKey, SealedBoxesKeyHash> missing;
  std::vector<size_t> samples;
  for (size_t index = 0; index < blocks.size(); ++index) {
    const SealedBoxesKey key = SealedKeyOf(blocks[index].block);
    if (!sealed_.contains(key) && missing.insert(key).second) {
      samples.push_back(index);
    }
  }
  std::vector<std::vector<CellBox>> shapes(samples.size());
  parallel(samples.size(), [&](size_t sample) {
    shapes[sample] = z13::building::primitives::SealedBoxes({.spec = blocks[samples[sample]].block.spec}, palette);
  });
  for (size_t sample = 0; sample < samples.size(); ++sample) {
    sealed_.emplace(SealedKeyOf(blocks[samples[sample]].block), std::move(shapes[sample]));
  }
  for (const KeyedBlock& keyed : blocks) {
    const z13::station::Block& block = keyed.block;
    // The cells its solids hold, not its box: a slope or a leaning panel leaves the rest to others.
    const std::vector<CellBox> boxes = SealedBoxesOf(block);
    if (z13::building::primitives::IsTransparent(block, palette)) {
      const float transmission = palette->get().Find(block.spec.type_id)->get().light_transmission;
      std::ranges::transform(boxes, std::back_inserter(glass), [transmission](const CellBox& cells) {
        return GlassBox {.cells = cells, .transmission = transmission};
      });
    } else {
      opaque.insert(opaque.end(), boxes.begin(), boxes.end());
    }
  }
}

Status CellLighting::State::Sync(std::span<const z13::station::Block> blocks,
                                 z13::building::primitives::OptionalPalette palette,
                                 const EmergencyLamps& emergency, const SyncContext& context) {
  if (failure_) {
    return {};
  }
  std::vector<KeyedBlock> keyed;
  keyed.reserve(blocks.size());
  std::ranges::transform(blocks, std::back_inserter(keyed), [](const z13::station::Block& block) {
    return KeyedBlock {.fingerprint = FingerprintOf(block), .block = block};
  });
  // Blocks flagged as changed without differing (a door changing table) must not redo the light.
  const uint64_t fingerprint = std::ranges::fold_left(
      keyed | std::views::transform(&KeyedBlock::fingerprint), uint64_t {}, std::plus<> {});
  if (BuiltFrom(palette) && fingerprint == blocks_fingerprint_ && SyncedWith(palette, emergency)) {
    return {};
  }
  blocks_fingerprint_ = fingerprint;
  if (!BuiltFrom(palette)) {
    sealed_.clear();
  }
  const auto by_fingerprint = [](const KeyedBlock& a, const KeyedBlock& b) { return a.fingerprint < b.fingerprint; };
  std::ranges::sort(keyed, by_fingerprint);
  std::vector<Lamp> lamps;
  const std::vector<z13::utility::DimmedLamp> dimmed(emergency.dimmed.begin(), emergency.dimmed.end());
  std::ranges::transform(z13::building::primitives::LightsOf(blocks, palette, dimmed, emergency.percent),
                         std::back_inserter(lamps), z13::station::light::LampOf);
  std::ranges::sort(lamps, LampBefore);
  wanted_lamps_ = lamps;

  dimmed_ = dimmed;
  emergency_percent_ = emergency.percent;
  if (!BuiltFrom(palette)) {
    std::vector<CellBox> opaque;
    std::vector<GlassBox> glass;
    BoxesOf(keyed, palette, context.parallel, opaque, glass);
    std::ranges::sort(glass, GlassBefore);
      std::vector<Lamp> first = NearestLamps(lamps, context.focus, kFirstLampBatch);
    light_.emplace(opaque, glass, first, context.parallel);
    palette_hash_ = palette ? std::optional(palette->get().hash) : std::nullopt;
    blocks_ = std::move(keyed);
    lamps_pending_ = first.size() < lamps.size();
    lamps_ = std::move(first);
    shining_ = light_->ShiningLamps();
    return Remake();
  }
  // Only the blocks that came or went are measured: the rest of the light's boxes stand as they are.
  std::vector<KeyedBlock> added;
  std::vector<KeyedBlock> removed;
  std::ranges::set_difference(keyed, blocks_, std::back_inserter(added), by_fingerprint);
  std::ranges::set_difference(blocks_, keyed, std::back_inserter(removed), by_fingerprint);
  LightEdits edits;
  BoxesOf(added, palette, context.parallel, edits.opaque_added, edits.glass_added);
  BoxesOf(removed, palette, context.parallel, edits.opaque_removed, edits.glass_removed);
  edits.lamps_removed = Missing(lamps_, lamps, LampBefore);
  // A lamp that changed colour comes back as it goes, whatever the batch.
  edits.lamps_added =
      NearestLamps(Missing(lamps, lamps_, LampBefore), context.focus, edits.lamps_removed.size() + lamp_batch_);
  blocks_ = std::move(keyed);
  std::vector<Lamp> lit = Missing(lamps_, edits.lamps_removed, LampBefore);
  lit.insert(lit.end(), edits.lamps_added.begin(), edits.lamps_added.end());
  std::ranges::sort(lit, LampBefore);
  lamps_pending_ = lit.size() < lamps.size();
  lamps_ = std::move(lit);
  return Present(light_->Apply(edits), context);
}

Status CellLighting::State::LightLamps(const SyncContext& context) {
  if (failure_ || !light_) {
    return {};
  }
  const auto start = std::chrono::steady_clock::now();
  LightEdits edits;
  edits.lamps_added = NearestLamps(Missing(wanted_lamps_, lamps_, LampBefore), context.focus, lamp_batch_);
  std::vector<Lamp> lit = lamps_;
  lit.insert(lit.end(), edits.lamps_added.begin(), edits.lamps_added.end());
  std::ranges::sort(lit, LampBefore);
  lamps_pending_ = lit.size() < wanted_lamps_.size();
  lamps_ = std::move(lit);
  const Status presented = Present(light_->Apply(edits), context);
  if (!edits.lamps_added.empty()) {
    TuneLampBatch(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count(),
                  edits.lamps_added.size());
  }
  return presented;
}

void CellLighting::State::TuneLampBatch(double batch_ms, size_t lamps) {
  const double per_lamp = batch_ms / static_cast<double>(lamps);
  lamp_cost_ms_ =
      lamp_cost_ms_ ? (kLampCostSmoothing * per_lamp) + ((1 - kLampCostSmoothing) * *lamp_cost_ms_) : per_lamp;
  lamp_batch_ = std::clamp(static_cast<size_t>(kLampBudgetMs / std::max(*lamp_cost_ms_, kMinLampCostMs)), size_t {1},
                           kMaxLampBatch);
}

Status CellLighting::State::Present(const z13::station::light::LightChanges& changes, const SyncContext& context) {
  shining_ = light_->ShiningLamps();
  if (changes.resized) {
    return Remake();
  }
  UploadLabels(changes.cell_tiles, context.parallel);
  std::ranges::for_each(changes.light_tiles, [this](const CellBox& tile) { UploadLight(tile); });
  return {};
}

Status CellLighting::State::Remake() {
  if (!gl_) {
    gl_.emplace();
  }
  ReleaseVolumes();
  const z13::station::light::LightLayout& layout = light_->Layout();
  if (layout.CubeCount() == 0) {
    return {};
  }
  if (gl_->MaxTextureUnits() <= kCellLightUnit) {
    failure_ = std::format("cell light needs {} texture units, the driver has {}", kCellLightUnit + 1,
                           gl_->MaxTextureUnits());
    return std::unexpected(*failure_);
  }
  const Eigen::Vector3i colors_size(layout.cubes.x(), layout.cubes.y(),
                                    layout.cubes.z() * kPiecesPerNode * static_cast<int>(kNodeTexels.size()));
  labels_ = gl_->Create(VolumeFormat::kLabels, layout.Cells());
  colors_ = gl_->Create(VolumeFormat::kColors, colors_size);
  if (!labels_ || !colors_) {
    ReleaseVolumes();
    failure_ = std::format("can't make the cell light textures for {} x {} x {} cubes", layout.cubes.x(),
                           layout.cubes.y(), layout.cubes.z());
    return std::unexpected(*failure_);
  }
  UploadLabels(layout.CellBounds());
  UploadLight({.min = Eigen::Vector3i::Zero(), .extent = layout.cubes});
  return {};
}

void CellLighting::State::UploadLabels(const CellBox& cells) const {
  if (!labels_) {
    return;
  }
  const z13::station::light::LightLayout& layout = light_->Layout();
  const CellBox local {.min = cells.min - layout.origin, .extent = cells.extent};
  gl_->UploadLabels(*labels_, local, light_->CellTexels(cells));
}

void CellLighting::State::UploadLabels(std::span<const CellBox> tiles,
                                       const z13::station::rooms::ParallelFor& parallel) const {
  if (!labels_) {
    return;
  }
  std::vector<std::vector<z13::station::light::CellTexel>> texels(tiles.size());
  parallel(tiles.size(), [&](size_t tile) { texels[tile] = light_->CellTexels(tiles[tile]); });
  const z13::station::light::LightLayout& layout = light_->Layout();
  for (size_t tile = 0; tile < tiles.size(); ++tile) {
    gl_->UploadLabels(*labels_, {.min = tiles[tile].min - layout.origin, .extent = tiles[tile].extent}, texels[tile]);
  }
}

void CellLighting::State::UploadLight(const CellBox& cubes) const {
  if (!colors_) {
    return;
  }
  const Eigen::Vector3i& all_cubes = light_->Layout().cubes;
  for (size_t kind = 0; kind < kNodeTexels.size(); ++kind) {
    for (uint8_t piece = 0; piece < kPiecesPerNode; ++piece) {
      gl_->UploadColors(*colors_, ColorRegion(cubes, all_cubes, kind, piece),
                        light_->Texels(cubes, piece, kNodeTexels[kind]));
    }
  }
}

void CellLighting::State::ReleaseVolumes() {
  const auto release = [this](std::optional<Volume>& volume) {
    if (volume && gl_) {
      gl_->Delete(*volume);
    }
    volume.reset();
  };
  release(labels_);
  release(colors_);
}

void CellLighting::State::Upload(const ::Shader& shader, const LightUniforms& uniforms,
                                 const Eigen::Matrix4f& from_world) const {
  const int enabled = labels_ && colors_ ? 1 : 0;
  SetShaderValue(shader, uniforms.cell_light_enabled, &enabled, SHADER_UNIFORM_INT);
  if (enabled == 0) {
    return;
  }
  const z13::station::light::LightLayout& layout = light_->Layout();
  SetShaderValue(shader, uniforms.cell_origin, layout.origin.data(), SHADER_UNIFORM_IVEC3);
  SetShaderValue(shader, uniforms.cell_cubes, layout.cubes.data(), SHADER_UNIFORM_IVEC3);
  SetShaderValueMatrix(shader, uniforms.cell_from_world, EigenToRaylibMatrix(from_world));
}

void CellLighting::State::Bind() const {
  if (labels_ && colors_) {
    gl_->Bind(*labels_, kCellLabelsUnit);
    gl_->Bind(*colors_, kCellLightUnit);
  }
}

}  // namespace z13::raylib
