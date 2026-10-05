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

#include <z13_primitives/palette.h>

#include <algorithm>
#include <format>
#include <span>
#include <unordered_set>
#include <utility>

#include <flatbuffers/idl.h>
#include <flatbuffers/reflection.h>

#include <lib_core/utils/fnv_hash.h>
#include <z13_primitives/geometry.h>

namespace z13::primitives {

namespace {

namespace fbs_station = fbs::station;

using Error = std::unexpected<std::string>;

Eigen::Vector3i ToCells(const fbs_station::Cells& cells) {
  return {cells.x(), cells.y(), cells.z()};
}

Rgba ToRgba(const fbs_station::Rgba& color) {
  return {color.r(), color.g(), color.b(), color.a()};
}

std::expected<Shape, std::string> ToShape(const fbs_station::ShapeUnion& shape) {
  // The JSON parser accepts a shape_type without its shape object.
  if (shape.value == nullptr) {
    return Error("no shape");
  }
  switch (shape.type) {
    case fbs_station::Shape::Box:
      return Shape {.kind = ShapeKind::kBox};
    case fbs_station::Shape::Wedge:
      return Shape {.kind = ShapeKind::kWedge};
    case fbs_station::Shape::CornerWedge:
      return Shape {.kind = ShapeKind::kCornerWedge};
    case fbs_station::Shape::DoorFrame: {
      const fbs_station::DoorFrameT& door = *shape.AsDoorFrame();
      return Shape {.kind = ShapeKind::kDoorFrame, .door_opening = {door.opening_width, door.opening_height}};
    }
    case fbs_station::Shape::NONE:
      break;
  }
  return Error("no shape");
}

std::expected<Primitive, std::string> ToPrimitive(const fbs_station::PrimitiveT& source, int max_size_cells) {
  if (source.name.empty()) {
    return Error("no name");
  }
  if (!source.min_size || !source.max_size) {
    return Error("no size limits");
  }
  if (!source.material || !source.material->first || !source.material->second) {
    return Error("no checker colors");
  }
  const auto shape = ToShape(source.shape);
  if (!shape) {
    return Error(shape.error());
  }

  Primitive primitive {
      .id = source.id,
      .name = source.name,
      .shape = *shape,
      .min_size = ToCells(*source.min_size),
      .max_size = ToCells(*source.max_size),
      .material = {.first = ToRgba(*source.material->first), .second = ToRgba(*source.material->second)},
      .flags = source.flags,
  };
  if ((primitive.min_size.array() < 1).any() || (primitive.max_size.array() > max_size_cells).any()) {
    return Error(std::format("sizes must be 1..{} cells", max_size_cells));
  }
  if ((primitive.min_size.array() > primitive.max_size.array()).any()) {
    return Error("min_size above max_size");
  }
  // Only the limits are test-built: a door's posts fit just some widths in a range.
  if (primitive.shape.kind == ShapeKind::kDoorFrame && primitive.min_size != primitive.max_size) {
    return Error("a door frame has a fixed size");
  }
  for (const Eigen::Vector3i& size : {primitive.min_size, primitive.max_size}) {
    if (const auto solids = BuildSolids(primitive.shape, size); !solids) {
      return Error(solids.error());
    }
  }
  return primitive;
}

// Of the content alone: Pack writes the same bytes however the JSON was laid out.
uint64_t HashContent(const fbs_station::PaletteT& palette) {
  flatbuffers::FlatBufferBuilder builder;
  builder.ForceDefaults(true);
  builder.Finish(fbs_station::Palette::Pack(builder, &palette));
  return FnvHash(std::as_bytes(std::span(builder.GetBufferPointer(), builder.GetSize())));
}

}  // namespace

std::optional<std::reference_wrapper<const Primitive>> Palette::Find(uint32_t id) const {
  const auto it = std::ranges::find(primitives, id, &Primitive::id);
  if (it == primitives.end()) {
    return std::nullopt;
  }
  return std::cref(*it);
}

std::expected<Palette, std::string> ParsePalette(std::string_view json) {
  flatbuffers::Parser parser;
  if (!parser.Deserialize(reflection::GetSchema(fbs_station::PaletteBinarySchema::data()))) {
    return Error("palette: failed to deserialize the binary schema");
  }
  if (!parser.Parse(std::string(json).c_str())) {
    return Error(std::format("palette: {}", parser.error_));
  }
  fbs_station::PaletteT source;
  fbs_station::GetPalette(parser.builder_.GetBufferPointer())->UnPackTo(&source);
  if (source.primitives.empty()) {
    return Error("palette: no primitives");
  }

  Palette palette {.max_size_cells = source.max_size_cells};
  std::unordered_set<uint32_t> ids;
  std::unordered_set<std::string> names;
  for (const auto& entry : source.primitives) {
    auto primitive = ToPrimitive(*entry, palette.max_size_cells);
    if (!primitive) {
      return Error(std::format("palette: primitive {} '{}': {}", entry->id, entry->name, primitive.error()));
    }
    if (!ids.insert(primitive->id).second) {
      return Error(std::format("palette: duplicate id {}", primitive->id));
    }
    if (!names.insert(primitive->name).second) {
      return Error(std::format("palette: duplicate name '{}'", primitive->name));
    }
    palette.primitives.push_back(std::move(*primitive));
  }
  // Only once every entry is valid: packing a shape_type without its shape crashes.
  palette.hash = HashContent(source);
  return palette;
}

}  // namespace z13::primitives
