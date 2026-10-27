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

#include <format>

#include <flatbuffers/idl.h>
#include <flatbuffers/reflection.h>

#include <blueprint_generated.h>

namespace z13::building::primitives {

namespace {

namespace fbs_station = fbs::station;

using z13::station::Orientation;

static_assert(static_cast<size_t>(fbs_station::Orientation::MAX) == static_cast<size_t>(Orientation::kFaceNegZUpNegY));
static_assert(static_cast<size_t>(fbs_station::WallAxis::MAX) == static_cast<size_t>(z13::station::WallAxis::kZ));
static_assert(static_cast<size_t>(fbs_station::WallDiagonal::MAX) ==
              static_cast<size_t>(z13::station::WallDiagonal::kTipToBase));

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

}  // namespace

std::expected<std::vector<z13::station::Block>, std::string> ParseBlueprint(
    std::string_view json, const Palette& palette) {
  const auto parsed = ParseSource(json);
  if (!parsed) {
    return std::unexpected(parsed.error());
  }
  const fbs_station::BlueprintT& source = *parsed;

  std::vector<z13::station::Block> blocks;
  blocks.reserve(source.blocks.size());
  for (size_t i = 0; i < source.blocks.size(); ++i) {
    const fbs_station::BlueprintBlockT& entry = *source.blocks[i];
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
    });
  }
  return blocks;
}

std::expected<std::vector<WallGeometry>, std::string> ParseBlueprintWalls(std::string_view json) {
  const auto parsed = ParseSource(json);
  if (!parsed) {
    return std::unexpected(parsed.error());
  }
  std::vector<WallGeometry> walls;
  walls.reserve(parsed->walls.size());
  for (size_t i = 0; i < parsed->walls.size(); ++i) {
    const fbs_station::BlueprintWallT& entry = *parsed->walls[i];
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

}  // namespace z13::building::primitives
