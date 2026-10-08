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

}  // namespace

std::expected<std::vector<z13::station::Block>, std::string> ParseBlueprint(
    std::string_view json, const Palette& palette) {
  flatbuffers::Parser parser;
  if (!parser.Deserialize(reflection::GetSchema(fbs_station::BlueprintBinarySchema::data()))) {
    return std::unexpected("blueprint: failed to deserialize the binary schema");
  }
  if (!parser.Parse(std::string(json).c_str())) {
    return std::unexpected(std::format("blueprint: {}", parser.error_));
  }
  fbs_station::BlueprintT source;
  fbs_station::GetBlueprint(parser.builder_.GetBufferPointer())->UnPackTo(&source);

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
                 .orientation = static_cast<Orientation>(entry.orientation)},
        .cell = {entry.cell->x(), entry.cell->y(), entry.cell->z()},
    });
  }
  return blocks;
}

}  // namespace z13::building::primitives
