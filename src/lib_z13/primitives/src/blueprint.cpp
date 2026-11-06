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

#include <primitives/blueprint.h>

#include <algorithm>
#include <format>
#include <memory>

#include <flatbuffers/idl.h>
#include <flatbuffers/reflection.h>

#include <blueprint_generated.h>
#include <primitives/palette.h>

namespace z13::building::primitives {

namespace {

namespace fbs_station = fbs::station;

using z13::station::Orientation;

static_assert(static_cast<size_t>(fbs_station::Orientation::MAX) == static_cast<size_t>(Orientation::kFaceNegZUpNegY));
static_assert(static_cast<size_t>(fbs_station::WallAxis::MAX) == static_cast<size_t>(z13::station::WallAxis::kZ));
static_assert(static_cast<size_t>(fbs_station::WallDiagonal::MAX) ==
              static_cast<size_t>(z13::station::WallDiagonal::kTipToBase));
static_assert(static_cast<size_t>(fbs_station::OpeningKind::MAX) ==
              static_cast<size_t>(z13::station::OpeningKind::kWindow));

std::expected<fbs_station::BlueprintT, std::string> ParseSource(std::string_view json) {
  flatbuffers::Parser parser;
  if (!parser.Deserialize(reflection::GetSchema(fbs_station::BlueprintBinarySchema::data()))) {
    return std::unexpected("blueprint: failed to deserialize the binary schema");
  }
  if (!parser.Parse(std::string(json).c_str())) {
    return std::unexpected(std::format("blueprint: {}", parser.error_));
  }
  fbs_station::BlueprintT source;
  fbs_station::GetBlueprint(parser.builder_.GetBufferPointer())->UnPackTo(&source);
  return source;
}

using BlockEntries = std::vector<std::unique_ptr<fbs_station::BlueprintBlockT>>;
using WallEntries = std::vector<std::unique_ptr<fbs_station::BlueprintWallT>>;

std::expected<std::vector<z13::station::Block>, std::string> BlocksOf(
    const BlockEntries& entries, const Palette& palette, z13::station::GridId grid) {
  std::vector<z13::station::Block> blocks;
  blocks.reserve(entries.size());
  for (size_t i = 0; i < entries.size(); ++i) {
    const fbs_station::BlueprintBlockT& entry = *entries[i];
    const auto primitive = palette.Find(entry.primitive);
    if (!primitive) {
      return std::unexpected(std::format("blueprint: block {}: no primitive '{}' in the palette", i, entry.primitive));
    }
    if (!entry.cell || !entry.size) {
      return std::unexpected(std::format("blueprint: block {}: no cell or size", i));
    }
    // The JSON parser takes any number that fits the enum's ubyte.
    if (entry.orientation > fbs_station::Orientation::MAX) {
      return std::unexpected(std::format("blueprint: block {}: no orientation {}", i, static_cast<int>(entry.orientation)));
    }
    blocks.push_back({
        .spec = {.type_id = primitive->get().id,
                 .size = {entry.size->x(), entry.size->y(), entry.size->z()},
                 .orientation = static_cast<Orientation>(entry.orientation),
                 .slant = entry.slant},
        .cell = {entry.cell->x(), entry.cell->y(), entry.cell->z()},
        .grid = grid,
    });
  }
  return blocks;
}

std::expected<std::vector<WallGeometry>, std::string> WallsOf(const WallEntries& entries) {
  std::vector<WallGeometry> walls;
  walls.reserve(entries.size());
  for (size_t i = 0; i < entries.size(); ++i) {
    const fbs_station::BlueprintWallT& entry = *entries[i];
    if (!entry.node_a || !entry.node_b) {
      return std::unexpected(std::format("blueprint: wall {}: no nodes", i));
    }
    walls.push_back({
        .node_a = {entry.node_a->x(), entry.node_a->y(), entry.node_a->z()},
        .node_b = {entry.node_b->x(), entry.node_b->y(), entry.node_b->z()},
        .stick_a = {.axis = static_cast<z13::station::WallAxis>(entry.axis_a), .length = entry.length_a},
        .stick_b = {.axis = static_cast<z13::station::WallAxis>(entry.axis_b), .length = entry.length_b},
        .thickness = entry.thickness,
        .diagonal = static_cast<z13::station::WallDiagonal>(entry.diagonal),
    });
  }
  return walls;
}

std::vector<std::vector<z13::station::WallOpening>> OpeningsOf(const WallEntries& entries) {
  std::vector<std::vector<z13::station::WallOpening>> openings;
  openings.reserve(entries.size());
  for (const auto& wall : entries) {
    std::vector<z13::station::WallOpening>& of_wall = openings.emplace_back();
    for (const auto& entry : wall->openings) {
      of_wall.push_back({.kind = static_cast<z13::station::OpeningKind>(entry->kind),
                         .offset = entry->offset,
                         .width = entry->width,
                         .sill = entry->sill,
                         .height = entry->height});
    }
  }
  return openings;
}

}  // namespace

std::expected<std::vector<z13::station::Block>, std::string> ParseBlueprint(
    std::string_view json, const Palette& palette) {
  const auto parsed = ParseSource(json);
  if (!parsed) {
    return std::unexpected(parsed.error());
  }
  return BlocksOf(parsed->blocks, palette, z13::station::kMainGrid);
}

std::expected<std::vector<WallGeometry>, std::string> ParseBlueprintWalls(std::string_view json) {
  const auto parsed = ParseSource(json);
  if (!parsed) {
    return std::unexpected(parsed.error());
  }
  return WallsOf(parsed->walls);
}

std::expected<std::vector<std::vector<z13::station::WallOpening>>, std::string> ParseBlueprintOpenings(
    std::string_view json) {
  const auto parsed = ParseSource(json);
  if (!parsed) {
    return std::unexpected(parsed.error());
  }
  return OpeningsOf(parsed->walls);
}

std::expected<std::vector<BlueprintGridPart>, std::string> ParseBlueprintGrids(
    std::string_view json, const Palette& palette) {
  const auto parsed = ParseSource(json);
  if (!parsed) {
    return std::unexpected(parsed.error());
  }
  std::vector<BlueprintGridPart> grids;
  for (size_t i = 0; i < parsed->grids.size(); ++i) {
    const fbs_station::BlueprintGridT& entry = *parsed->grids[i];
    if (entry.id == z13::station::kMainGrid ||
        std::ranges::any_of(grids, [&entry](const BlueprintGridPart& other) { return other.grid.id == entry.id; })) {
      return std::unexpected(std::format("blueprint: grid {}: id {} is the main grid's or repeats", i, entry.id));
    }
    auto blocks = BlocksOf(entry.blocks, palette, entry.id);
    auto walls = WallsOf(entry.walls);
    if (!blocks || !walls) {
      return std::unexpected(std::format("grid {}: {}", entry.id, blocks ? walls.error() : blocks.error()));
    }
    const Eigen::Vector3i origin =
        entry.origin ? Eigen::Vector3i(entry.origin->x(), entry.origin->y(), entry.origin->z()) : Eigen::Vector3i::Zero();
    grids.push_back({.grid = {.id = entry.id,
                              .origin = origin,
                              .yaw_degrees = entry.yaw_degrees,
                              .pitch_degrees = entry.pitch_degrees},
                     .blocks = std::move(*blocks),
                     .walls = std::move(*walls),
                     .openings = OpeningsOf(entry.walls)});
  }
  return grids;
}

std::expected<std::vector<z13::station::Dock>, std::string> ParseBlueprintDocks(std::string_view json) {
  const auto parsed = ParseSource(json);
  if (!parsed) {
    return std::unexpected(parsed.error());
  }
  const auto known = [&parsed](z13::station::GridId id) {
    return id == z13::station::kMainGrid ||
           std::ranges::any_of(parsed->grids, [id](const auto& grid) { return grid->id == id; });
  };
  const auto end = [](z13::station::GridId grid, const fbs_station::Cell* cell) {
    return z13::station::DockEnd {.grid = grid,
                                  .cell = cell ? Eigen::Vector3i(cell->x(), cell->y(), cell->z()) : Eigen::Vector3i::Zero()};
  };
  std::vector<z13::station::Dock> docks;
  for (size_t i = 0; i < parsed->docks.size(); ++i) {
    const fbs_station::BlueprintDockT& entry = *parsed->docks[i];
    if (!known(entry.a_grid) || !known(entry.b_grid)) {
      return std::unexpected(std::format("blueprint: dock {}: grid {} or {} is not in the blueprint", i, entry.a_grid,
                                         entry.b_grid));
    }
    docks.push_back({.a = end(entry.a_grid, entry.a_cell ? &*entry.a_cell : nullptr),
                     .b = end(entry.b_grid, entry.b_cell ? &*entry.b_cell : nullptr)});
  }
  return docks;
}

std::expected<bool, std::string> ParseBlueprintDoorsOpen(std::string_view json) {
  const auto parsed = ParseSource(json);
  if (!parsed) {
    return std::unexpected(parsed.error());
  }
  return parsed->doors_open;
}

}  // namespace z13::building::primitives
